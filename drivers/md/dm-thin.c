/*
 * Copyright (C) 2011-2012 Red Hat UK.
 *
 * This file is released under the GPL.
 */

/*
 * Feature: thick, preremove, thin-to-thick, write_same(undone this),
 *          GET_LBA_STATUS, thin_discard_passdown
 */

#include <linux/fast_clone.h>
#include <linux/delay.h>

#include "dm-thin-metadata.h"
#include "dm-bio-prison.h"
#include "dm.h"

#include <linux/device-mapper.h>
#include <linux/dm-io.h>
#include <linux/dm-kcopyd.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/rculist.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/rbtree.h>
#include <linux/kthread.h>
#include <linux/sysfs.h>

#ifdef QNAP_HAL
#include <qnap/hal_event.h>
extern int send_hal_netlink(NETLINK_EVT *event);
#endif


#define	DM_MSG_PREFIX	"thin"

/*
 * Tunable constants
 */
#define ENDIO_HOOK_POOL_SIZE 1024
#define MAPPING_POOL_SIZE 1024
#define PRISON_CELLS 1024
#define COMMIT_PERIOD HZ

DECLARE_DM_KCOPYD_THROTTLE_WITH_MODULE_PARM(snapshot_copy_throttle,
        "A percentage of time allocated for copy on write");

/*
 * The block size of the device holding pool data must be
 * between 64KB and 1GB.
 */
#define DATA_DEV_BLOCK_SIZE_MIN_SECTORS (64 * 1024 >> SECTOR_SHIFT)
#define DATA_DEV_BLOCK_SIZE_MAX_SECTORS (1024 * 1024 * 1024 >> SECTOR_SHIFT)

/*
 * Device id is restricted to 24 bits.
 */
#define MAX_DEV_ID ((1 << 24) - 1)

/*
 * Reserved sectors constants
 */
#define MAX_QNAP_RESERVED_SECTORS 67108864
#define MIN_QNAP_RESERVED_SECTORS 2097152

/*
 * How do we handle breaking sharing of data blocks?
 * =================================================
 *
 * We use a standard copy-on-write btree to store the mappings for the
 * devices (note I'm talking about copy-on-write of the metadata here, not
 * the data).  When you take an internal snapshot you clone the root node
 * of the origin btree.  After this there is no concept of an origin or a
 * snapshot.  They are just two device trees that happen to point to the
 * same data blocks.
 *
 * When we get a write in we decide if it's to a shared data block using
 * some timestamp magic.  If it is, we have to break sharing.
 *
 * Let's say we write to a shared block in what was the origin.  The
 * steps are:
 *
 * i) plug io further to this physical block. (see bio_prison code).
 *
 * ii) quiesce any read io to that shared data block.  Obviously
 * including all devices that share this block.  (see dm_deferred_set code)
 *
 * iii) copy the data block to a newly allocate block.  This step can be
 * missed out if the io covers the block. (schedule_copy).
 *
 * iv) insert the new mapping into the origin's btree
 * (process_prepared_mapping).  This act of inserting breaks some
 * sharing of btree nodes between the two devices.  Breaking sharing only
 * effects the btree of that specific device.  Btrees for the other
 * devices that share the block never change.  The btree for the origin
 * device as it was after the last commit is untouched, ie. we're using
 * persistent data structures in the functional programming sense.
 *
 * v) unplug io to this physical block, including the io that triggered
 * the breaking of sharing.
 *
 * Steps (ii) and (iii) occur in parallel.
 *
 * The metadata _doesn't_ need to be committed before the io continues.  We
 * get away with this because the io is always written to a _new_ block.
 * If there's a crash, then:
 *
 * - The origin mapping will point to the old origin block (the shared
 * one).  This will contain the data as it was before the io that triggered
 * the breaking of sharing came in.
 *
 * - The snap mapping still points to the old block.  As it would after
 * the commit.
 *
 * The downside of this scheme is the timestamp magic isn't perfect, and
 * will continue to think that data block in the snapshot device is shared
 * even after the write to the origin has broken sharing.  I suspect data
 * blocks will typically be shared by many different devices, so we're
 * breaking sharing n + 1 times, rather than n, where n is the number of
 * devices that reference this data block.  At the moment I think the
 * benefits far, far outweigh the disadvantages.
 */

/*----------------------------------------------------------------*/

/*
 * Key building.
 */
static void build_data_key(struct dm_thin_device *td,
                           dm_block_t b, struct dm_cell_key *key)
{
	key->virtual = 0;
	key->dev = dm_thin_dev_id(td);
	key->block = b;
}

static void build_virtual_key(struct dm_thin_device *td, dm_block_t b,
                              struct dm_cell_key *key)
{
	key->virtual = 1;
	key->dev = dm_thin_dev_id(td);
	key->block = b;
}

/*----------------------------------------------------------------*/

/*
 * A pool device ties together a metadata device and a data device.  It
 * also provides the interface for creating and destroying internal
 * devices.
 */
struct dm_thin_new_mapping;

/*
 * The pool runs in 3 modes.  Ordered in degraded order for comparisons.
 */
enum pool_mode {
	PM_WRITE,		/* metadata may be changed */
	PM_READ_ONLY,		/* metadata may not be changed */
	PM_FAIL,		/* all I/O fails */
};

struct pool_features {
	enum pool_mode mode;

	bool zero_new_blocks: 1;
	bool discard_enabled: 1;
	bool discard_passdown: 1;
};

struct thin_c;
typedef void (*process_bio_fn)(struct thin_c *tc, struct bio *bio);
typedef void (*process_mapping_fn)(struct dm_thin_new_mapping *m);

struct pool {
	struct list_head list;
	struct dm_target *ti;	/* Only set if a pool target is bound */
	struct kobject kobj;

	struct mapped_device *pool_md;
	struct block_device *md_dev;
	struct dm_pool_metadata *pmd;

	dm_block_t sync_io_threshold;
	dm_block_t low_water_blocks;
	uint32_t sectors_per_block;
	int sectors_per_block_shift;

	struct pool_features pf;
	unsigned low_water_triggered: 1;	/* A dm event has been sent */
	unsigned no_free_space: 1;	/* A -ENOSPC warning has been issued */
	unsigned sb_backup_fail_reported: 1;
	unsigned sync_io_triggered: 1;

	struct dm_bio_prison *prison;
	struct dm_kcopyd_client *copier;

	struct workqueue_struct *wq;
	struct work_struct worker;
	struct delayed_work waker;

	unsigned long last_commit_jiffies;
	unsigned ref_count;

	spinlock_t lock;
	struct workqueue_struct *convert_wq;

	struct bio_list deferred_flush_bios;
	struct list_head prepared_mappings;
	struct list_head prepared_discards;

	struct list_head active_thins;

	struct dm_deferred_set *shared_read_ds;
	struct dm_deferred_set *all_io_ds;

	struct dm_thin_new_mapping *next_mapping;
	mempool_t *mapping_pool;

	process_bio_fn process_bio;
	process_bio_fn process_discard;

	process_mapping_fn process_prepared_mapping;
	process_mapping_fn process_prepared_discard;
};

static enum pool_mode get_pool_mode(struct pool *pool);
static void set_pool_mode(struct pool *pool, enum pool_mode mode);

/*
 * Target context for a pool.
 */
struct pool_c {
	struct dm_target *ti;
	struct pool *pool;
	struct dm_dev *data_dev;
	struct dm_dev *metadata_dev;
	struct dm_target_callbacks callbacks;

	dm_block_t low_water_blocks;
	struct pool_features requested_pf; /* Features requested during table load */
	struct pool_features adjusted_pf;  /* Features used after adjusting for constituent devices */
};

#define THIN  0
#define THICK 1

enum T2T_STATE {
	T2T_READY,
	/* WORK_BUSY_PENDING = 1 */
	/* WORK_BUSY_RUNNING = 2 */
	T2T_FAIL = 3,
	T2T_CANCEL,
	T2T_SUCCESS,
	__MAX_NR_STATE
};

static char *const t2t_state_name[__MAX_NR_STATE + 1] = {
	"READY",
	"PENDING",
	"RUNNING",
	"FAIL",
	"CANCEL",
	"SUCCESS",
	"UNKNOWN",
};

struct convert_work {
	enum T2T_STATE status;
	int cancel;
	struct work_struct work;
	spinlock_t lock;
};

/*
 * Target context for a thin.
 */
struct thin_c {
	struct list_head list;
	struct dm_dev *pool_dev;
	struct dm_dev *origin_dev;
	dm_thin_id dev_id;

	struct pool *pool;
	struct dm_thin_device *td;

	sector_t len;

	struct convert_work thick_work;
	struct convert_work remove_work;

	void (*dm_monitor_fn)(void *, int);
	void *lundev;

	bool is_thick;
	bool is_lun;
	bool discard_passdown;

	spinlock_t lock;
	struct bio_list deferred_bio_list;
	struct bio_list retry_on_resume_list;
	struct rb_root sort_bio_list; /* sorted list of deferred bios */
};

/*----------------------------------------------------------------*/

/*
 * wake_worker() is used when new work is queued and when pool_resume is
 * ready to continue deferred IO processing.
 */
static void wake_worker(struct pool *pool)
{
	queue_work(pool->wq, &pool->worker);
}

/*----------------------------------------------------------------*/

static int bio_detain(struct pool *pool, struct dm_cell_key *key, struct bio *bio,
                      struct dm_bio_prison_cell **cell_result)
{
	int r;
	struct dm_bio_prison_cell *cell_prealloc;

	/*
	 * Allocate a cell from the prison's mempool.
	 * This might block but it can't fail.
	 */
	cell_prealloc = dm_bio_prison_alloc_cell(pool->prison, GFP_NOIO);

	r = dm_bio_detain(pool->prison, key, bio, cell_prealloc, cell_result);
	if (r)
		/*
		 * We reused an old cell; we can get rid of
		 * the new one.
		 */
		dm_bio_prison_free_cell(pool->prison, cell_prealloc);

	return r;
}

static void cell_release(struct pool *pool,
                         struct dm_bio_prison_cell *cell,
                         struct bio_list *bios)
{
	dm_cell_release(pool->prison, cell, bios);
	dm_bio_prison_free_cell(pool->prison, cell);
}

static void cell_release_no_holder(struct pool *pool,
                                   struct dm_bio_prison_cell *cell,
                                   struct bio_list *bios)
{
	dm_cell_release_no_holder(pool->prison, cell, bios);
	dm_bio_prison_free_cell(pool->prison, cell);
}

static void cell_defer_no_holder_no_free(struct thin_c *tc,
        struct dm_bio_prison_cell *cell)
{
	struct pool *pool = tc->pool;
	unsigned long flags;

	spin_lock_irqsave(&tc->lock, flags);
	dm_cell_release_no_holder(pool->prison, cell, &tc->deferred_bio_list);
	spin_unlock_irqrestore(&tc->lock, flags);

	wake_worker(pool);
}

static void cell_error(struct pool *pool,
                       struct dm_bio_prison_cell *cell)
{
	dm_cell_error(pool->prison, cell);
	dm_bio_prison_free_cell(pool->prison, cell);
}

/*----------------------------------------------------------------*/

/*
 * A global list of pools that uses a struct mapped_device as a key.
 */
static struct dm_thin_pool_table {
	struct mutex mutex;
	struct list_head pools;
} dm_thin_pool_table;

static void pool_table_init(void)
{
	mutex_init(&dm_thin_pool_table.mutex);
	INIT_LIST_HEAD(&dm_thin_pool_table.pools);
}

static void __pool_table_insert(struct pool *pool)
{
	BUG_ON(!mutex_is_locked(&dm_thin_pool_table.mutex));
	list_add(&pool->list, &dm_thin_pool_table.pools);
}

static void __pool_table_remove(struct pool *pool)
{
	BUG_ON(!mutex_is_locked(&dm_thin_pool_table.mutex));
	list_del(&pool->list);
}

static struct pool *__pool_table_lookup(struct mapped_device *md)
{
	struct pool *pool = NULL, *tmp;

	BUG_ON(!mutex_is_locked(&dm_thin_pool_table.mutex));

	list_for_each_entry(tmp, &dm_thin_pool_table.pools, list) {
		if (tmp->pool_md == md) {
			pool = tmp;
			break;
		}
	}

	return pool;
}

static struct pool *__pool_table_lookup_metadata_dev(struct block_device *md_dev)
{
	struct pool *pool = NULL, *tmp;

	BUG_ON(!mutex_is_locked(&dm_thin_pool_table.mutex));

	list_for_each_entry(tmp, &dm_thin_pool_table.pools, list) {
		if (tmp->md_dev == md_dev) {
			pool = tmp;
			break;
		}
	}

	return pool;
}

/*----------------------------------------------------------------*/

#define HAL_SB_BACKUP_FAIL    1
#define HAL_THIN_ERR_VERSION  2

/*
 * FIXME: if there would be times we need more hal event, refactor this one
 */
static void send_hal_msg(void *context, int type)
{
#ifdef QNAP_HAL
	NETLINK_EVT hal_event;
	struct pool *pool;
	struct mapped_device *md;

	switch (type) {
	case HAL_SB_BACKUP_FAIL:
		pool = (struct pool *)context;
		md = pool->pool_md;

		if (pool->sb_backup_fail_reported)
			return;
		else
			pool->sb_backup_fail_reported = 1;

		hal_event.arg.action = THIN_SB_BACKUP_FAIL;
		break;
	case HAL_THIN_ERR_VERSION:
		md = (struct mapped_device *)context;
		hal_event.arg.action = THIN_ERR_VERSION_DETECT;
		break;
	default:
		DMERR("%s: unknown hal message type: %d", __func__, type);
		return;
	};

	hal_event.type = HAL_EVENT_THIN;
	dm_copy_name_and_uuid(md, hal_event.arg.param.pool_message.pool_name, NULL);
	send_hal_netlink(&hal_event);
#endif
}

/*----------------------------------------------------------------*/

struct dm_thin_endio_hook {
	struct thin_c *tc;
	struct dm_deferred_entry *shared_read_entry;
	struct dm_deferred_entry *all_io_entry;
	struct dm_thin_new_mapping *overwrite_mapping;
	struct rb_node rb_node;
};

static void __requeue_bio_list(struct thin_c *tc, struct bio_list *master)
{
	struct bio *bio;
	struct bio_list bios;
	unsigned long flags;

	bio_list_init(&bios);
	spin_lock_irqsave(&tc->lock, flags);
	bio_list_merge(&bios, master);
	bio_list_init(master);
	spin_unlock_irqrestore(&tc->lock, flags);

	while ((bio = bio_list_pop(&bios)))
		bio_endio(bio, DM_ENDIO_REQUEUE);
}

static void requeue_io(struct thin_c *tc)
{
	__requeue_bio_list(tc, &tc->deferred_bio_list);
	__requeue_bio_list(tc, &tc->retry_on_resume_list);
}

/*
 * This section of code contains the logic for processing a thin device's IO.
 * Much of the code depends on pool object resources (lists, workqueues, etc)
 * but most is exclusively called from the thin target rather than the thin-pool
 * target.
 */

static bool block_size_is_power_of_two(struct pool *pool)
{
	return pool->sectors_per_block_shift >= 0;
}

static dm_block_t get_bio_block(struct thin_c *tc, struct bio *bio)
{
	struct pool *pool = tc->pool;
	sector_t block_nr = bio->bi_sector;

	if (block_size_is_power_of_two(pool))
		block_nr >>= pool->sectors_per_block_shift;
	else
		(void) sector_div(block_nr, pool->sectors_per_block);

	return block_nr;
}

static void remap(struct thin_c *tc, struct bio *bio, dm_block_t block)
{
	struct pool *pool = tc->pool;
	sector_t bi_sector = bio->bi_sector;

	bio->bi_bdev = tc->pool_dev->bdev;
	if (block_size_is_power_of_two(pool))
		bio->bi_sector = (block << pool->sectors_per_block_shift) |
		                 (bi_sector & (pool->sectors_per_block - 1));
	else
		bio->bi_sector = (block * pool->sectors_per_block) +
		                 sector_div(bi_sector, pool->sectors_per_block);
}

static void remap_to_origin(struct thin_c *tc, struct bio *bio)
{
	bio->bi_bdev = tc->origin_dev->bdev;
}

static int bio_triggers_commit(struct thin_c *tc, struct bio *bio)
{
	return (bio->bi_rw & (REQ_FLUSH | REQ_FUA)) &&
	       dm_thin_changed_this_transaction(tc->td);
}

static void inc_all_io_entry(struct pool *pool, struct bio *bio)
{
	struct dm_thin_endio_hook *h;

	if (bio->bi_rw & REQ_DISCARD)
		return;

	h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));
	h->all_io_entry = dm_deferred_entry_inc(pool->all_io_ds);
}

static void issue(struct thin_c *tc, struct bio *bio)
{
	struct pool *pool = tc->pool;
	unsigned long flags;

	if (!bio_triggers_commit(tc, bio)) {
		generic_make_request(bio);
		return;
	}

	/*
	 * Complete bio with an error if earlier I/O caused changes to
	 * the metadata that can't be committed e.g, due to I/O errors
	 * on the metadata device.
	 */
	if (dm_thin_aborted_changes(tc->td)) {
		bio_io_error(bio);
		return;
	}

	/*
	 * Batch together any bios that trigger commits and then issue a
	 * single commit for them in process_deferred_bios().
	 */
	spin_lock_irqsave(&pool->lock, flags);
	bio_list_add(&pool->deferred_flush_bios, bio);
	spin_unlock_irqrestore(&pool->lock, flags);
}

static void remap_to_origin_and_issue(struct thin_c *tc, struct bio *bio)
{
	remap_to_origin(tc, bio);
	issue(tc, bio);
}

static void remap_and_issue(struct thin_c *tc, struct bio *bio,
                            dm_block_t block)
{
	remap(tc, bio, block);
	issue(tc, bio);
}

/*----------------------------------------------------------------*/

/*
 * Bio endio functions.
 */
struct dm_thin_new_mapping {
	struct list_head list;

	unsigned quiesced: 1;
	unsigned prepared: 1;
	unsigned pass_discard: 1;
	unsigned definitely_not_shared: 1;

	struct thin_c *tc;
	dm_block_t virt_block;
	dm_block_t data_block;
	struct dm_bio_prison_cell *cell, *cell2;
	int err;

	/*
	 * If the bio covers the whole area of a block then we can avoid
	 * zeroing or copying.  Instead this bio is hooked.  The bio will
	 * still be in the cell, so care has to be taken to avoid issuing
	 * the bio twice.
	 */
	struct bio *bio;
	bio_end_io_t *saved_bi_end_io;
};

static void __maybe_add_mapping(struct dm_thin_new_mapping *m)
{
	struct pool *pool = m->tc->pool;

	if (m->quiesced && m->prepared) {
		list_add_tail(&m->list, &pool->prepared_mappings);
		wake_worker(pool);
	}
}

static void copy_complete(int read_err, unsigned long write_err, void *context)
{
	unsigned long flags;
	struct dm_thin_new_mapping *m = context;
	struct pool *pool = m->tc->pool;

	m->err = read_err || write_err ? -EIO : 0;

	spin_lock_irqsave(&pool->lock, flags);
	m->prepared = 1;
	__maybe_add_mapping(m);
	spin_unlock_irqrestore(&pool->lock, flags);
}

static void overwrite_endio(struct bio *bio, int err)
{
	unsigned long flags;
	struct dm_thin_endio_hook *h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));
	struct dm_thin_new_mapping *m = h->overwrite_mapping;
	struct pool *pool = m->tc->pool;

	m->err = err;

	spin_lock_irqsave(&pool->lock, flags);
	m->prepared = 1;
	__maybe_add_mapping(m);
	spin_unlock_irqrestore(&pool->lock, flags);
}

/*----------------------------------------------------------------*/

/*
 * Workqueue.
 */

/*
 * Prepared mapping jobs.
 */

/*
 * This sends the bios in the cell back to the deferred_bios list.
 */
static void cell_defer(struct thin_c *tc, struct dm_bio_prison_cell *cell)
{
	struct pool *pool = tc->pool;
	unsigned long flags;

	spin_lock_irqsave(&tc->lock, flags);
	cell_release(pool, cell, &tc->deferred_bio_list);
	spin_unlock_irqrestore(&tc->lock, flags);

	wake_worker(pool);
}

/*
 * Same as cell_defer above, except it omits the original holder of the cell.
 */
static void cell_defer_no_holder(struct thin_c *tc, struct dm_bio_prison_cell *cell)
{
	struct pool *pool = tc->pool;
	unsigned long flags;

	spin_lock_irqsave(&tc->lock, flags);
	cell_release_no_holder(pool, cell, &tc->deferred_bio_list);
	spin_unlock_irqrestore(&tc->lock, flags);

	wake_worker(pool);
}

static void process_prepared_mapping_fail(struct dm_thin_new_mapping *m)
{
	if (m->bio)
		m->bio->bi_end_io = m->saved_bi_end_io;
	cell_error(m->tc->pool, m->cell);
	list_del(&m->list);
	mempool_free(m, m->tc->pool->mapping_pool);
}

static void process_prepared_mapping(struct dm_thin_new_mapping *m)
{
	struct thin_c *tc = m->tc;
	struct pool *pool = tc->pool;
	struct bio *bio;
	int r;

	bio = m->bio;
	if (bio)
		bio->bi_end_io = m->saved_bi_end_io;

	if (m->err) {
		cell_error(pool, m->cell);
		goto out;
	}

	/*
	 * Commit the prepared block into the mapping btree.
	 * Any I/O for this block arriving after this point will get
	 * remapped to it directly.
	 */
	r = dm_thin_insert_block(tc->td, m->virt_block, m->data_block, 0);
	if (r) {
		DMERR_LIMIT("dm_thin_insert_block() failed");
		cell_error(pool, m->cell);
		goto out;
	}

	/*
	 * Release any bios held while the block was being provisioned.
	 * If we are processing a write bio that completely covers the block,
	 * we already processed it so can ignore it now when processing
	 * the bios in the cell.
	 */
	if (bio) {
		cell_defer_no_holder(tc, m->cell);
		bio_endio(bio, 0);
	} else
		cell_defer(tc, m->cell);

out:
	list_del(&m->list);
	mempool_free(m, pool->mapping_pool);
}

static void process_prepared_discard_fail(struct dm_thin_new_mapping *m)
{
	struct thin_c *tc = m->tc;

	bio_io_error(m->bio);
	cell_defer_no_holder(tc, m->cell);
	cell_defer_no_holder(tc, m->cell2);
	mempool_free(m, tc->pool->mapping_pool);
}

static void process_prepared_discard_passdown(struct dm_thin_new_mapping *m)
{
	struct thin_c *tc = m->tc;

	inc_all_io_entry(tc->pool, m->bio);
	cell_defer_no_holder(tc, m->cell);
	cell_defer_no_holder(tc, m->cell2);

	if (m->pass_discard) {
		if (m->definitely_not_shared) {
			remap_and_issue(tc, m->bio, m->data_block);
		} else {
			bool used = false;
			if (dm_pool_block_is_used(tc->pool->pmd, m->data_block, &used) || used) {
				bio_endio(m->bio, 0);
			} else {
				remap_and_issue(tc, m->bio, m->data_block);
			}
		}
	} else
		bio_endio(m->bio, 0);

	mempool_free(m, tc->pool->mapping_pool);
}

static void process_prepared_discard(struct dm_thin_new_mapping *m)
{
	int r;
	struct thin_c *tc = m->tc;

	r = dm_thin_remove_block(tc->td, m->virt_block);
	if (r)
		DMERR_LIMIT("dm_thin_remove_block() failed");

	process_prepared_discard_passdown(m);
}

static void process_prepared(struct pool *pool, struct list_head *head,
                             process_mapping_fn *fn)
{
	unsigned long flags;
	struct list_head maps;
	struct dm_thin_new_mapping *m, *tmp;

	INIT_LIST_HEAD(&maps);
	spin_lock_irqsave(&pool->lock, flags);
	list_splice_init(head, &maps);
	spin_unlock_irqrestore(&pool->lock, flags);

	list_for_each_entry_safe(m, tmp, &maps, list)
	(*fn)(m);
}

/*
 * Deferred bio jobs.
 */
static int io_overlaps_block(struct pool *pool, struct bio *bio)
{
	return bio->bi_size == (pool->sectors_per_block << SECTOR_SHIFT);
}

static int io_overwrites_block(struct pool *pool, struct bio *bio)
{
	return (bio_data_dir(bio) == WRITE) &&
	       io_overlaps_block(pool, bio);
}

static int fast_zeroed(struct pool *pool, struct bio *bio)
{
	return io_overwrites_block(pool, bio) && (bio->bi_rw & REQ_QNAP_MAP_ZERO);
}

static void save_and_set_endio(struct bio *bio, bio_end_io_t **save,
                               bio_end_io_t *fn)
{
	*save = bio->bi_end_io;
	bio->bi_end_io = fn;
}

static int ensure_next_mapping(struct pool *pool)
{
	if (pool->next_mapping)
		return 0;

	pool->next_mapping = mempool_alloc(pool->mapping_pool, GFP_ATOMIC);

	return pool->next_mapping ? 0 : -ENOMEM;
}

static struct dm_thin_new_mapping *get_next_mapping(struct pool *pool)
{
	struct dm_thin_new_mapping *r = pool->next_mapping;

	BUG_ON(!pool->next_mapping);

	pool->next_mapping = NULL;

	return r;
}

static void schedule_copy(struct thin_c *tc, dm_block_t virt_block,
                          struct dm_dev *origin, dm_block_t data_origin,
                          dm_block_t data_dest,
                          struct dm_bio_prison_cell *cell, struct bio *bio, unsigned bypass_copy)
{
	int r;
	struct pool *pool = tc->pool;
	struct dm_thin_new_mapping *m = get_next_mapping(pool);

	INIT_LIST_HEAD(&m->list);
	m->quiesced = 0;
	m->prepared = 0;
	m->tc = tc;
	m->virt_block = virt_block;
	m->data_block = data_dest;
	m->cell = cell;
	m->err = 0;
	m->bio = NULL;

	if (!dm_deferred_set_add_work(pool->shared_read_ds, &m->list))
		m->quiesced = 1;

	/*
	 * IO to pool_dev remaps to the pool target's data_dev.
	 *
	 * If the whole block of data is being overwritten, we can issue the
	 * bio immediately. Otherwise we use kcopyd to clone the data first.
	 */
	if (io_overwrites_block(pool, bio) || bypass_copy) {
		struct dm_thin_endio_hook *h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));

		h->overwrite_mapping = m;
		m->bio = bio;
		save_and_set_endio(bio, &m->saved_bi_end_io, overwrite_endio);
		inc_all_io_entry(pool, bio);
		remap_and_issue(tc, bio, data_dest);
	} else {
		struct dm_io_region from, to;

		from.bdev = origin->bdev;
		from.sector = data_origin * pool->sectors_per_block;
		from.count = pool->sectors_per_block;

		to.bdev = tc->pool_dev->bdev;
		to.sector = data_dest * pool->sectors_per_block;
		to.count = pool->sectors_per_block;

		r = dm_kcopyd_copy(pool->copier, &from, 1, &to,
		                   0, copy_complete, m);
		if (r < 0) {
			mempool_free(m, pool->mapping_pool);
			DMERR_LIMIT("dm_kcopyd_copy() failed");
			cell_error(pool, cell);
		}
	}
}

static void schedule_internal_copy(struct thin_c *tc, dm_block_t virt_block,
                                   dm_block_t data_origin, dm_block_t data_dest,
                                   struct dm_bio_prison_cell *cell, struct bio *bio, unsigned bypass_copy)
{
	schedule_copy(tc, virt_block, tc->pool_dev,
	              data_origin, data_dest, cell, bio, bypass_copy);
}

static void schedule_external_copy(struct thin_c *tc, dm_block_t virt_block,
                                   dm_block_t data_dest,
                                   struct dm_bio_prison_cell *cell, struct bio *bio)
{
	schedule_copy(tc, virt_block, tc->origin_dev,
	              virt_block, data_dest, cell, bio, 0);
}

static void schedule_zero(struct thin_c *tc, dm_block_t virt_block,
                          dm_block_t data_block, struct dm_bio_prison_cell *cell,
                          struct bio *bio, unsigned zeroed)
{
	struct pool *pool = tc->pool;
	struct dm_thin_new_mapping *m = get_next_mapping(pool);

	INIT_LIST_HEAD(&m->list);
	m->quiesced = 1;
	m->prepared = 0;
	m->tc = tc;
	m->virt_block = virt_block;
	m->data_block = data_block;
	m->cell = cell;
	m->err = 0;
	m->bio = NULL;

	/*
	 * If the whole block of data is being overwritten or we are not
	 * zeroing pre-existing data, we can issue the bio immediately.
	 * Otherwise we use kcopyd to zero the data first.
	 */
	if (!pool->pf.zero_new_blocks && !zeroed)
		process_prepared_mapping(m);

	else if (io_overwrites_block(pool, bio)) {
		struct dm_thin_endio_hook *h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));

		h->overwrite_mapping = m;
		m->bio = bio;
		save_and_set_endio(bio, &m->saved_bi_end_io, overwrite_endio);
		inc_all_io_entry(pool, bio);
		remap_and_issue(tc, bio, data_block);
	} else {
		int r;
		struct dm_io_region to;

		to.bdev = tc->pool_dev->bdev;
		to.sector = data_block * pool->sectors_per_block;
		to.count = pool->sectors_per_block;

		r = dm_kcopyd_zero(pool->copier, 1, &to, 0, copy_complete, m);
		if (r < 0) {
			mempool_free(m, pool->mapping_pool);
			DMERR_LIMIT("dm_kcopyd_zero() failed");
			cell_error(pool, cell);
		}
	}
}

static void clear_space_monitor_triggers(struct pool *pool)
{
	int r;
	unsigned long flags;
	dm_block_t free_blocks;

	r = dm_pool_get_free_block_count(pool->pmd, &free_blocks);
	if (r)
		DMWARN("check pool free block count failed");

	if (free_blocks) {
		spin_lock_irqsave(&pool->lock, flags);
		pool->no_free_space = 0;
		spin_unlock_irqrestore(&pool->lock, flags);
	}

	if (free_blocks > pool->sync_io_threshold) {
		spin_lock_irqsave(&pool->lock, flags);
		pool->sync_io_triggered = 0;
		spin_unlock_irqrestore(&pool->lock, flags);
	}

	if (free_blocks > pool->low_water_blocks) {
		spin_lock_irqsave(&pool->lock, flags);
		pool->low_water_triggered = 0;
		spin_unlock_irqrestore(&pool->lock, flags);
	}

	return;
}

static int commit(struct pool *pool)
{
	int r;
	dm_block_t free_blocks;

	r = dm_pool_commit_metadata(pool->pmd);
	if (r)
		DMERR_LIMIT("commit failed: error = %d", r);
	else if (!pool->sb_backup_fail_reported &&
	         report_sb_backup_fail(pool->pmd))
		send_hal_msg(pool, THIN_SB_BACKUP_FAIL);

	clear_space_monitor_triggers(pool);

	return r;
}

/*
 * A non-zero return indicates read_only or fail_io mode.
 * Many callers don't care about the return value.
 */
static int commit_or_fallback(struct pool *pool)
{
	int r;

	if (get_pool_mode(pool) != PM_WRITE)
		return -EINVAL;

	r = commit(pool);
	if (r)
		set_pool_mode(pool, PM_READ_ONLY);

	return r;
}

static int alloc_data_block(struct thin_c *tc, dm_block_t *result)
{
	int r;
	dm_block_t free_blocks;
	unsigned long flags;
	struct pool *pool = tc->pool;

	r = dm_pool_get_free_block_count(pool->pmd, &free_blocks);
	if (r)
		return r;

	if (free_blocks <= pool->low_water_blocks && !pool->low_water_triggered) {
		DMWARN("%s: reached low water mark for data device: sending event.",
		       dm_device_name(pool->pool_md));
		spin_lock_irqsave(&pool->lock, flags);
		pool->low_water_triggered = 1;
		spin_unlock_irqrestore(&pool->lock, flags);
		dm_table_event(pool->ti->table);
	}

	if (free_blocks <= pool->sync_io_threshold && !pool->sync_io_triggered) {
		DMWARN("%s: reached sync io threshold for data device: sending event.",
		       dm_device_name(pool->pool_md));
		spin_lock_irqsave(&pool->lock, flags);
		pool->sync_io_triggered = 1;
		spin_unlock_irqrestore(&pool->lock, flags);
		dm_table_event(pool->ti->table);
	}

	if (!free_blocks) {
		if (pool->no_free_space)
			return -ENOSPC;
		else {
			/*
			 * Try to commit to see if that will free up some
			 * more space.
			 */
			(void) commit_or_fallback(pool);

			r = dm_pool_get_free_block_count(pool->pmd, &free_blocks);
			if (r)
				return r;

			/*
			 * If we still have no space we set a flag to avoid
			 * doing all this checking and return -ENOSPC.
			 */
			if (!free_blocks) {
				DMWARN("%s: no free space available.",
				       dm_device_name(pool->pool_md));
				spin_lock_irqsave(&pool->lock, flags);
				pool->no_free_space = 1;
				spin_unlock_irqrestore(&pool->lock, flags);
				return -ENOSPC;
			}
		}
	}

	r = dm_pool_alloc_data_block(pool->pmd, result);
	if (r)
		return r;

	return 0;
}

/*
 * If we have run out of space, queue bios until the device is
 * resumed, presumably after having been reloaded with more space.
 */
static void retry_on_resume(struct bio *bio)
{
	struct dm_thin_endio_hook *h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));
	struct thin_c *tc = h->tc;
	unsigned long flags;

	spin_lock_irqsave(&tc->lock, flags);
	bio_list_add(&tc->retry_on_resume_list, bio);
	spin_unlock_irqrestore(&tc->lock, flags);
}

static void no_space(struct pool *pool, struct dm_bio_prison_cell *cell)
{
	struct bio *bio;
	struct bio_list bios;

	bio_list_init(&bios);
	cell_release(pool, cell, &bios);

	while ((bio = bio_list_pop(&bios)))
		bio_endio(bio, -ENOSPC);
}

static void process_fast_zeroing(struct thin_c *tc, struct bio *bio)
{
	int r;
	struct pool *pool = tc->pool;
	struct dm_bio_prison_cell *cell, *cell2;
	struct dm_cell_key key, key2;
	dm_block_t block = get_bio_block(tc, bio), new_block;
	struct dm_thin_lookup_result lookup_result;
	uint32_t map_and_zero = (bio->bi_rw & REQ_QNAP_MAP) ? 0 : 1;

	build_virtual_key(tc->td, block, &key);
	if (bio_detain(tc->pool, &key, bio, &cell)) {
		return;
	}

	r = dm_thin_find_block(tc->td, block, 1, &lookup_result);
	switch (r) {
	case 0:
		/* Nothing we can do, it has already been zeroed */
		if (lookup_result.zeroed || bio->bi_rw & REQ_QNAP_MAP) {
			cell_defer_no_holder(tc, cell);
			bio_endio(bio, 0);
			break;
		}

		/*
		 * Check nobody is fiddling with this pool block.  This can
		 * happen if someone's in the process of breaking sharing
		 * on this block.
		 */
		build_data_key(tc->td, lookup_result.block, &key2);
		if (bio_detain(tc->pool, &key2, bio, &cell2)) {
			cell_defer_no_holder(tc, cell);
			break;
		}

		BUG_ON(!io_overlaps_block(pool, bio));

		if (dm_thin_insert_block_with_time(tc->td, block, lookup_result.block, map_and_zero, &lookup_result.time)) {
			DMERR("%s: error when trying to write zero to block %llu with fast zeroing", __func__, lookup_result.block);
			cell_defer_no_holder(tc, cell);
			cell_defer_no_holder(tc, cell2);
			bio_io_error(bio);
			break;
		}

		cell_defer_no_holder(tc, cell);
		cell_defer_no_holder(tc, cell2);

		bio_endio(bio, 0);
		break;
	case -ENODATA:
		/*
		 * It isn't provisioned, just allocate space for it.
		 */
		r = alloc_data_block(tc, &new_block);
		if (r) {
			DMERR_LIMIT("%s: cannot provision new block to handle fast zeroing", __func__);
			if (r == -ENOSPC)
				no_space(pool, cell);
			else {
				cell_defer_no_holder(tc, cell);
				bio_io_error(bio);
			}
			break;
		}

		if (dm_thin_insert_block(tc->td, block, new_block, map_and_zero)) {
			DMERR_LIMIT("%s: cannot insert new block to handle fast zeroing", __func__);
			cell_defer_no_holder(tc, cell);
			bio_io_error(bio);
			break;
		}

		cell_defer_no_holder(tc, cell);
		bio_endio(bio, 0);
		break;

	default:
		DMERR_LIMIT("%s: dm_thin_find_block() failed: error = %d",
		            __func__, r);
		cell_defer_no_holder(tc, cell);
		bio_io_error(bio);
		break;
	}
}

static void process_discard(struct thin_c *tc, struct bio *bio)
{
	int r;
	unsigned long flags;
	struct pool *pool = tc->pool;
	struct dm_bio_prison_cell *cell, *cell2;
	struct dm_cell_key key, key2;
	dm_block_t block = get_bio_block(tc, bio);
	struct dm_thin_lookup_result lookup_result;
	struct dm_thin_new_mapping *m;

	build_virtual_key(tc->td, block, &key);
	if (bio_detain(tc->pool, &key, bio, &cell))
		return;

	r = dm_thin_find_block(tc->td, block, 1, &lookup_result);
	switch (r) {
	case 0:
		/*
		 * Check nobody is fiddling with this pool block.  This can
		 * happen if someone's in the process of breaking sharing
		 * on this block.
		 */
		build_data_key(tc->td, lookup_result.block, &key2);
		if (bio_detain(tc->pool, &key2, bio, &cell2)) {
			cell_defer_no_holder(tc, cell);
			break;
		}

		if (io_overlaps_block(pool, bio) && !tc->is_thick) {
			/*
			 * IO may still be going to the destination block.  We must
			 * quiesce before we can do the removal.
			 */
			m = get_next_mapping(pool);
			m->tc = tc;
			m->pass_discard = pool->pf.discard_passdown && tc->discard_passdown;
			m->definitely_not_shared = !lookup_result.shared;
			m->virt_block = block;
			m->data_block = lookup_result.block;
			m->cell = cell;
			m->cell2 = cell2;
			m->err = 0;
			m->bio = bio;

			if (!dm_deferred_set_add_work(pool->all_io_ds, &m->list)) {
				spin_lock_irqsave(&pool->lock, flags);
				list_add_tail(&m->list, &pool->prepared_discards);
				spin_unlock_irqrestore(&pool->lock, flags);
				wake_worker(pool);
			}
		} else {
			inc_all_io_entry(pool, bio);
			cell_defer_no_holder(tc, cell);
			cell_defer_no_holder(tc, cell2);

			/*
			 * The DM core makes sure that the discard doesn't span
			 * a block boundary.  So we submit the discard of a
			 * partial block appropriately.
			 */
			if ((!lookup_result.shared) && pool->pf.discard_passdown && tc->discard_passdown)
				remap_and_issue(tc, bio, lookup_result.block);
			else
				bio_endio(bio, 0);
		}
		break;

	case -ENODATA:
		/*
		 * It isn't provisioned, just forget it.
		 */
		cell_defer_no_holder(tc, cell);
		bio_endio(bio, 0);
		break;

	default:
		DMERR_LIMIT("%s: dm_thin_find_block() failed: error = %d",
		            __func__, r);
		cell_defer_no_holder(tc, cell);
		bio_io_error(bio);
		break;
	}
}

static void break_sharing(struct thin_c *tc, struct bio *bio, dm_block_t block,
                          struct dm_cell_key *key,
                          struct dm_thin_lookup_result *lookup_result,
                          struct dm_bio_prison_cell *cell)
{
	int r;
	dm_block_t data_block;

	r = alloc_data_block(tc, &data_block);
	switch (r) {
	case 0:
		schedule_internal_copy(tc, block, lookup_result->block,
		                       data_block, cell, bio, lookup_result->zeroed);
		break;

	case -ENOSPC:
		no_space(tc->pool, cell);
		break;

	default:
		DMERR_LIMIT("%s: alloc_data_block() failed: error = %d",
		            __func__, r);
		cell_error(tc->pool, cell);
		break;
	}
}

static void process_shared_bio(struct thin_c *tc, struct bio *bio,
                               dm_block_t block,
                               struct dm_thin_lookup_result *lookup_result)
{
	struct dm_bio_prison_cell *cell;
	struct pool *pool = tc->pool;
	struct dm_cell_key key;

	/*
	 * If cell is already occupied, then sharing is already in the process
	 * of being broken so we have nothing further to do here.
	 */
	build_data_key(tc->td, lookup_result->block, &key);
	if (bio_detain(pool, &key, bio, &cell))
		return;

	if (bio_data_dir(bio) == WRITE && bio->bi_size)
		break_sharing(tc, bio, block, &key, lookup_result, cell);
	else {
		struct dm_thin_endio_hook *h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));

		h->shared_read_entry = dm_deferred_entry_inc(pool->shared_read_ds);
		inc_all_io_entry(pool, bio);
		cell_defer_no_holder(tc, cell);

		remap_and_issue(tc, bio, lookup_result->block);
	}
}

static void provision_block(struct thin_c *tc, struct bio *bio, dm_block_t block,
                            struct dm_bio_prison_cell *cell)
{
	int r;
	dm_block_t data_block;
	struct pool *pool = tc->pool;

	/*
	 * Remap empty bios (flushes) immediately, without provisioning.
	 */
	if (!bio->bi_size) {
		inc_all_io_entry(pool, bio);
		cell_defer_no_holder(tc, cell);

		remap_and_issue(tc, bio, 0);
		return;
	}

	/*
	 * Fill read bios with zeroes and complete them immediately.
	 */
	if (bio_data_dir(bio) == READ) {
		zero_fill_bio(bio);
		cell_defer_no_holder(tc, cell);
		set_bit(BIO_THIN_UNMAPPED, &bio->bi_flags);
		bio_endio(bio, 0);
		return;
	}

	r = alloc_data_block(tc, &data_block);
	switch (r) {
	case 0:
		if (tc->origin_dev)
			schedule_external_copy(tc, block, data_block, cell, bio);
		else
			schedule_zero(tc, block, data_block, cell, bio, 0);
		break;

	case -ENOSPC:
		no_space(pool, cell);
		break;

	default:
		DMERR_LIMIT("%s: alloc_data_block() failed: error = %d",
		            __func__, r);
		set_pool_mode(pool, PM_READ_ONLY);
		cell_error(pool, cell);
		break;
	}
}

static void zero_block(struct thin_c *tc, struct bio *bio, dm_block_t block,
                       dm_block_t data_block, struct dm_bio_prison_cell *cell, unsigned shared)
{
	int r = 0;
	struct pool *pool = tc->pool;

	/*
	 * Remap empty bios (flushes) immediately, without zeroing.
	 */
	if (!bio->bi_size) {
		inc_all_io_entry(pool, bio);
		cell_defer_no_holder(tc, cell);
		remap_and_issue(tc, bio, 0);
		return;
	}

	/*
	 * Fill read bios with zeroes and complete them immediately.
	 */
	if (bio_data_dir(bio) == READ) {
		zero_fill_bio(bio);
		cell_defer_no_holder(tc, cell);
		bio_endio(bio, 0);
		return;
	}

	if (shared)
		r = alloc_data_block(tc, &data_block);

	switch (r) {
	case 0:
		schedule_zero(tc, block, data_block, cell, bio, 1);
		break;

	case -ENOSPC:
		no_space(pool, cell);
		break;

	default:
		DMERR_LIMIT("%s: alloc_data_block() failed: error = %d",
		            __func__, r);
		set_pool_mode(pool, PM_READ_ONLY);
		cell_error(pool, cell);
		break;
	}
}

static void process_bio(struct thin_c *tc, struct bio *bio)
{
	int r;
	struct pool *pool = tc->pool;
	dm_block_t block = get_bio_block(tc, bio);
	struct dm_bio_prison_cell *cell;
	struct dm_cell_key key;
	struct dm_thin_lookup_result lookup_result;

	/*
	 * If cell is already occupied, then the block is already
	 * being provisioned so we have nothing further to do here.
	 */
	build_virtual_key(tc->td, block, &key);
	if (bio_detain(pool, &key, bio, &cell))
		return;

	r = dm_thin_find_block(tc->td, block, 1, &lookup_result);
	switch (r) {
	case 0:
		if (lookup_result.zeroed)
			zero_block(tc, bio, block, lookup_result.block, cell, lookup_result.shared);
		else {
			if (lookup_result.shared) {
				process_shared_bio(tc, bio, block, &lookup_result);
				cell_defer_no_holder(tc, cell); /* FIXME: pass this cell into process_shared? */
			} else {
				inc_all_io_entry(pool, bio);
				cell_defer_no_holder(tc, cell);
				remap_and_issue(tc, bio, lookup_result.block);
			}
		}
		break;

	case -ENODATA:
		if (bio_data_dir(bio) == READ && tc->origin_dev) {
			inc_all_io_entry(pool, bio);
			cell_defer_no_holder(tc, cell);

			remap_to_origin_and_issue(tc, bio);
		} else
			provision_block(tc, bio, block, cell);
		break;

	default:
		DMERR_LIMIT("%s: dm_thin_find_block() failed: error = %d",
		            __func__, r);
		cell_defer_no_holder(tc, cell);
		bio_io_error(bio);
		break;
	}
}

static void process_bio_read_only(struct thin_c *tc, struct bio *bio)
{
	int r;
	int rw = bio_data_dir(bio);
	dm_block_t block = get_bio_block(tc, bio);
	struct dm_thin_lookup_result lookup_result;

	r = dm_thin_find_block(tc->td, block, 1, &lookup_result);
	switch (r) {
	case 0:
		if (lookup_result.shared && (rw == WRITE) && bio->bi_size)
			bio_io_error(bio);
		else {
			inc_all_io_entry(tc->pool, bio);
			remap_and_issue(tc, bio, lookup_result.block);
		}
		break;

	case -ENODATA:
		if (rw != READ) {
			bio_io_error(bio);
			break;
		}

		if (tc->origin_dev) {
			inc_all_io_entry(tc->pool, bio);
			remap_to_origin_and_issue(tc, bio);
			break;
		}

		zero_fill_bio(bio);
		bio_endio(bio, 0);
		break;

	default:
		DMERR_LIMIT("%s: dm_thin_find_block() failed: error = %d",
		            __func__, r);
		bio_io_error(bio);
		break;
	}
}

static void process_bio_fail(struct thin_c *tc, struct bio *bio)
{
	bio_io_error(bio);
}

/*
 * FIXME: should we also commit due to size of transaction, measured in
 * metadata blocks?
 */
static int need_commit_due_to_time(struct pool *pool)
{
	return jiffies < pool->last_commit_jiffies ||
	       jiffies > pool->last_commit_jiffies + COMMIT_PERIOD;
}

#define thin_pbd(node) rb_entry((node), struct dm_thin_endio_hook, rb_node)
#define thin_bio(pbd) dm_bio_from_per_bio_data((pbd), sizeof(struct dm_thin_endio_hook))

static void __thin_bio_rb_add(struct thin_c *tc, struct bio *bio)
{
	struct rb_node **rbp, *parent;
	struct dm_thin_endio_hook *pbd;
	sector_t bi_sector = bio->bi_sector;

	rbp = &tc->sort_bio_list.rb_node;
	parent = NULL;
	while (*rbp) {
		parent = *rbp;
		pbd = thin_pbd(parent);

		if (bi_sector < thin_bio(pbd)->bi_sector) {
			rbp = &(*rbp)->rb_left;
		} else {
			rbp = &(*rbp)->rb_right;
		}
	}

	pbd = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));
	rb_link_node(&pbd->rb_node, parent, rbp);
	rb_insert_color(&pbd->rb_node, &tc->sort_bio_list);
}

static void __extract_sorted_bios(struct thin_c *tc)
{
	struct rb_node *node;
	struct dm_thin_endio_hook *pbd;
	struct bio *bio;

	for (node = rb_first(&tc->sort_bio_list); node; node = rb_next(node)) {
		pbd = thin_pbd(node);
		bio = thin_bio(pbd);

		bio_list_add(&tc->deferred_bio_list, bio);
		rb_erase(&pbd->rb_node, &tc->sort_bio_list);
	}

	WARN_ON(!RB_EMPTY_ROOT(&tc->sort_bio_list));
}

static void __sort_thin_deferred_bios(struct thin_c *tc)
{
	struct bio *bio;
	struct bio_list bios;

	bio_list_init(&bios);
	bio_list_merge(&bios, &tc->deferred_bio_list);
	bio_list_init(&tc->deferred_bio_list);

	/* Sort deferred_bio_list using rb-tree */
	while ((bio = bio_list_pop(&bios))) {
		__thin_bio_rb_add(tc, bio);
	}

	/*
	 * Transfer the sorted bios in sort_bio_list back to
	 * deferred_bio_list to allow lockless submission of
	 * all bios.
	 */
	__extract_sorted_bios(tc);
}

static void process_thin_deferred_bios(struct thin_c *tc)
{
	struct pool *pool = tc->pool;
	unsigned long flags;
	struct bio *bio;
	struct bio_list bios;
	struct blk_plug plug;

	bio_list_init(&bios);

	spin_lock_irqsave(&tc->lock, flags);

	/*
	 * FIXME: allow sorting to be enabled/disabled via ctr and/or
	 * message (and auto-disable if data device is non-rotational?)
	 */
	__sort_thin_deferred_bios(tc);

	bio_list_merge(&bios, &tc->deferred_bio_list);
	bio_list_init(&tc->deferred_bio_list);
	spin_unlock_irqrestore(&tc->lock, flags);

	blk_start_plug(&plug);

	while ((bio = bio_list_pop(&bios))) {
		/*
		 * If we've got no free new_mapping structs, and processing
		 * this bio might require one, we pause until there are some
		 * prepared mappings to process.
		 */
		if (ensure_next_mapping(pool)) {
			spin_lock_irqsave(&tc->lock, flags);
			bio_list_merge(&tc->deferred_bio_list, &bios);
			spin_unlock_irqrestore(&tc->lock, flags);
			break;
		}

		if (fast_zeroed(pool, bio) || bio->bi_rw & REQ_QNAP_MAP)
			process_fast_zeroing(tc, bio);
		else if (bio->bi_rw & REQ_DISCARD)
			pool->process_discard(tc, bio);
		else
			pool->process_bio(tc, bio);
	}
	blk_finish_plug(&plug);
}

static void process_deferred_bios(struct pool *pool)
{
	unsigned long flags;
	struct bio *bio;
	struct bio_list bios;
	struct thin_c *tc;

	rcu_read_lock();
	list_for_each_entry_rcu(tc, &pool->active_thins, list)
	process_thin_deferred_bios(tc);
	rcu_read_unlock();

	/*
	 * If there are any deferred flush bios, we must commit
	 * the metadata before issuing them.
	 */
	bio_list_init(&bios);
	spin_lock_irqsave(&pool->lock, flags);
	bio_list_merge(&bios, &pool->deferred_flush_bios);
	bio_list_init(&pool->deferred_flush_bios);
	spin_unlock_irqrestore(&pool->lock, flags);

	if (bio_list_empty(&bios) && !need_commit_due_to_time(pool))
		return;

	if (commit_or_fallback(pool)) {
		while ((bio = bio_list_pop(&bios)))
			bio_io_error(bio);
		return;
	}
	pool->last_commit_jiffies = jiffies;

	while ((bio = bio_list_pop(&bios)))
		generic_make_request(bio);
}

static void do_worker(struct work_struct *ws)
{
	struct pool *pool = container_of(ws, struct pool, worker);

	process_prepared(pool, &pool->prepared_mappings, &pool->process_prepared_mapping);
	process_prepared(pool, &pool->prepared_discards, &pool->process_prepared_discard);
	process_deferred_bios(pool);
}

/*
 * We want to commit periodically so that not too much
 * unwritten data builds up.
 */
static void do_waker(struct work_struct *ws)
{
	struct pool *pool = container_of(to_delayed_work(ws), struct pool, waker);
	wake_worker(pool);
	queue_delayed_work(pool->wq, &pool->waker, COMMIT_PERIOD);
}

/*----------------------------------------------------------------*/

static enum pool_mode get_pool_mode(struct pool *pool)
{
	return pool->pf.mode;
}

static void set_pool_mode(struct pool *pool, enum pool_mode mode)
{
	int r;

	pool->pf.mode = mode;

	switch (mode) {
	case PM_FAIL:
		DMERR("switching pool to failure mode");
		pool->process_bio = process_bio_fail;
		pool->process_discard = process_bio_fail;
		pool->process_prepared_mapping = process_prepared_mapping_fail;
		pool->process_prepared_discard = process_prepared_discard_fail;
		break;

	case PM_READ_ONLY:
		DMERR("switching pool to read-only mode");
		r = dm_pool_abort_metadata(pool->pmd);
		if (r) {
			DMERR("aborting transaction failed");
			set_pool_mode(pool, PM_FAIL);
		} else {
			dm_pool_metadata_read_only(pool->pmd);
			pool->process_bio = process_bio_read_only;
			pool->process_discard = process_discard;
			pool->process_prepared_mapping = process_prepared_mapping_fail;
			pool->process_prepared_discard = process_prepared_discard_passdown;
		}
		break;

	case PM_WRITE:
		pool->process_bio = process_bio;
		pool->process_discard = process_discard;
		pool->process_prepared_mapping = process_prepared_mapping;
		pool->process_prepared_discard = process_prepared_discard;
		break;
	}
}

/*----------------------------------------------------------------*/

/*
 * Mapping functions.
 */

/*
 * Called only while mapping a thin bio to hand it over to the workqueue.
 */
static void thin_defer_bio(struct thin_c *tc, struct bio *bio)
{
	unsigned long flags;
	struct pool *pool = tc->pool;

	spin_lock_irqsave(&tc->lock, flags);
	bio_list_add(&tc->deferred_bio_list, bio);
	spin_unlock_irqrestore(&tc->lock, flags);

	wake_worker(pool);
}

static void thin_hook_bio(struct thin_c *tc, struct bio *bio)
{
	struct dm_thin_endio_hook *h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));

	h->tc = tc;
	h->shared_read_entry = NULL;
	h->all_io_entry = NULL;
	h->overwrite_mapping = NULL;
}

/*
 * Non-blocking function called from the thin target's map function.
 */
static int thin_bio_map(struct dm_target *ti, struct bio *bio)
{
	int r;
	struct thin_c *tc = ti->private;
	dm_block_t block = get_bio_block(tc, bio);
	struct dm_thin_device *td = tc->td;
	struct dm_thin_lookup_result result;
	struct dm_bio_prison_cell cell1, cell2;
	struct dm_bio_prison_cell *cell_result;
	struct dm_cell_key key;

	thin_hook_bio(tc, bio);

	if (get_pool_mode(tc->pool) == PM_FAIL) {
		bio_io_error(bio);
		return DM_MAPIO_SUBMITTED;
	}

	if (bio->bi_rw & (REQ_DISCARD | REQ_FLUSH | REQ_FUA | REQ_QNAP_MAP)) {
		thin_defer_bio(tc, bio);
		return DM_MAPIO_SUBMITTED;
	}

	r = dm_thin_find_block(td, block, 0, &result);

	/*
	 * Note that we defer readahead too.
	 */
	switch (r) {
	case 0:
		if (unlikely(result.shared) || fast_zeroed(tc->pool, bio) ||
		    (bio_data_dir(bio) == WRITE && result.zeroed)) {

			/*
			 * We have a race condition here between the
			 * result.shared value returned by the lookup and
			 * snapshot creation, which may cause new
			 * sharing.
			 *
			 * To avoid this always quiesce the origin before
			 * taking the snap.  You want to do this anyway to
			 * ensure a consistent application view
			 * (i.e. lockfs).
			 *
			 * More distant ancestors are irrelevant. The
			 * shared flag will be set in their case.
			 */
			thin_defer_bio(tc, bio);
			return DM_MAPIO_SUBMITTED;
		}

		if (bio_data_dir(bio) == READ && result.zeroed) {
			zero_fill_bio(bio);
			bio_endio(bio, 0);
			return DM_MAPIO_SUBMITTED;
		}

		build_virtual_key(tc->td, block, &key);
		if (dm_bio_detain(tc->pool->prison, &key, bio, &cell1, &cell_result))
			return DM_MAPIO_SUBMITTED;

		build_data_key(tc->td, result.block, &key);
		if (dm_bio_detain(tc->pool->prison, &key, bio, &cell2, &cell_result)) {
			cell_defer_no_holder_no_free(tc, &cell1);
			return DM_MAPIO_SUBMITTED;
		}

		inc_all_io_entry(tc->pool, bio);
		cell_defer_no_holder_no_free(tc, &cell2);
		cell_defer_no_holder_no_free(tc, &cell1);

		remap(tc, bio, result.block);
		return DM_MAPIO_REMAPPED;

	case -ENODATA:
		if (get_pool_mode(tc->pool) == PM_READ_ONLY) {
			/*
			 * This block isn't provisioned, and we have no way
			 * of doing so.  Just error it.
			 */
			bio_io_error(bio);
			return DM_MAPIO_SUBMITTED;
		}
	/* fall through */

	case -EWOULDBLOCK:
		/*
		 * In future, the failed dm_thin_find_block above could
		 * provide the hint to load the metadata into cache.
		 */
		thin_defer_bio(tc, bio);
		return DM_MAPIO_SUBMITTED;

	default:
		/*
		 * Must always call bio_io_error on failure.
		 * dm_thin_find_block can fail with -EINVAL if the
		 * pool is switched to fail-io mode.
		 */
		bio_io_error(bio);
		return DM_MAPIO_SUBMITTED;
	}
}

static int pool_is_congested(struct dm_target_callbacks *cb, int bdi_bits)
{
	int r;
	unsigned long flags;
	struct request_queue *q;
	struct pool_c *pt = container_of(cb, struct pool_c, callbacks);

	spin_lock_irqsave(&pt->pool->lock, flags);
	r = pt->pool->no_free_space;
	spin_unlock_irqrestore(&pt->pool->lock, flags);

	if (!r) {
		q = bdev_get_queue(pt->data_dev->bdev);
		return bdi_congested(&q->backing_dev_info, bdi_bits);
	}

	return r;
}

static void requeue_bios(struct pool *pool)
{
	unsigned long flags;
	struct thin_c *tc;

	rcu_read_lock();
	list_for_each_entry_rcu(tc, &pool->active_thins, list) {
		spin_lock_irqsave(&tc->lock, flags);
		bio_list_merge(&tc->deferred_bio_list, &tc->retry_on_resume_list);
		bio_list_init(&tc->retry_on_resume_list);
		spin_unlock_irqrestore(&tc->lock, flags);
	}
	rcu_read_unlock();
}

/*----------------------------------------------------------------
 * Binding of control targets to a pool object
 *--------------------------------------------------------------*/
static bool data_dev_supports_discard(struct pool_c *pt)
{
	struct request_queue *q = bdev_get_queue(pt->data_dev->bdev);

	return q && blk_queue_discard(q);
}

static bool is_factor(sector_t block_size, uint32_t n)
{
	return !sector_div(block_size, n);
}

/*
 * If discard_passdown was enabled verify that the data device
 * supports discards.  Disable discard_passdown if not.
 */
static void disable_passdown_if_not_supported(struct pool_c *pt)
{
	struct pool *pool = pt->pool;
	struct block_device *data_bdev = pt->data_dev->bdev;
	struct queue_limits *data_limits = &bdev_get_queue(data_bdev)->limits;
	sector_t block_size = pool->sectors_per_block << SECTOR_SHIFT;
	const char *reason = NULL;
	char buf[BDEVNAME_SIZE];

	if (!pt->adjusted_pf.discard_passdown)
		return;

	if (!data_dev_supports_discard(pt))
		reason = "discard unsupported";

	else if (data_limits->max_discard_sectors < pool->sectors_per_block)
		reason = "max discard sectors smaller than a block";

	else if (data_limits->discard_granularity > block_size)
		reason = "discard granularity larger than a block";

	else if (!is_factor(block_size, data_limits->discard_granularity))
		reason = "discard granularity not a factor of block size";

	if (reason) {
		DMWARN("Data device (%s) %s: Disabling discard passdown.", bdevname(data_bdev, buf), reason);
		pt->adjusted_pf.discard_passdown = false;
	}
}

static int bind_control_target(struct pool *pool, struct dm_target *ti)
{
	struct pool_c *pt = ti->private;

	/*
	 * We want to make sure that degraded pools are never upgraded.
	 */
	enum pool_mode old_mode = pool->pf.mode;
	enum pool_mode new_mode = pt->adjusted_pf.mode;

	if (old_mode > new_mode)
		new_mode = old_mode;

	pool->ti = ti;
	pool->low_water_blocks = pt->low_water_blocks;
	pool->pf = pt->adjusted_pf;

	set_pool_mode(pool, new_mode);

	return 0;
}

static void unbind_control_target(struct pool *pool, struct dm_target *ti)
{
	if (pool->ti == ti)
		pool->ti = NULL;
}

/*----------------------------------------------------------------
 * Pool creation
 *--------------------------------------------------------------*/
/* Initialize pool features. */
static void pool_features_init(struct pool_features *pf)
{
	pf->mode = PM_WRITE;
	pf->zero_new_blocks = true;
	pf->discard_enabled = true;
	pf->discard_passdown = true;
}

static void __pool_destroy(struct pool *pool)
{
	__pool_table_remove(pool);

	if (dm_pool_metadata_close(pool->pmd) < 0)
		DMWARN("%s: dm_pool_metadata_close() failed.", __func__);

	dm_bio_prison_destroy(pool->prison);
	dm_kcopyd_client_destroy(pool->copier);

	if (pool->wq)
		destroy_workqueue(pool->wq);

	if (pool->convert_wq)
		destroy_workqueue(pool->convert_wq);

	if (pool->next_mapping)
		mempool_free(pool->next_mapping, pool->mapping_pool);
	mempool_destroy(pool->mapping_pool);
	dm_deferred_set_destroy(pool->shared_read_ds);
	dm_deferred_set_destroy(pool->all_io_ds);
	kfree(pool);
}

static struct kmem_cache *_new_mapping_cache;

static struct pool *pool_create(struct mapped_device *pool_md,
                                struct block_device *metadata_dev,
                                unsigned long block_size,
                                int read_only, char **error)
{
	int r;
	void *err_p;
	struct pool *pool;
	struct dm_pool_metadata *pmd;
	bool format_device = read_only ? false : true;

	pmd = dm_pool_metadata_open(metadata_dev, block_size, format_device);
	if (IS_ERR(pmd)) {
		*error = "Error creating metadata object";
		return (struct pool *)pmd;
	}

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool) {
		*error = "Error allocating memory for pool";
		err_p = ERR_PTR(-ENOMEM);
		goto bad_pool;
	}

	pool->pmd = pmd;
	pool->sectors_per_block = block_size;
	if (block_size & (block_size - 1))
		pool->sectors_per_block_shift = -1;
	else
		pool->sectors_per_block_shift = __ffs(block_size);
	pool->low_water_blocks = 0;
	pool->sync_io_threshold = 0;
	pool_features_init(&pool->pf);
	pool->prison = dm_bio_prison_create(PRISON_CELLS);
	if (!pool->prison) {
		*error = "Error creating pool's bio prison";
		err_p = ERR_PTR(-ENOMEM);
		goto bad_prison;
	}

	pool->copier = dm_kcopyd_client_create(&dm_kcopyd_throttle);
	if (IS_ERR(pool->copier)) {
		r = PTR_ERR(pool->copier);
		*error = "Error creating pool's kcopyd client";
		err_p = ERR_PTR(r);
		goto bad_kcopyd_client;
	}

	/*
	 * Create singlethreaded workqueue that will service all devices
	 * that use this metadata.
	 */
	pool->wq = alloc_ordered_workqueue("dm-" DM_MSG_PREFIX, WQ_MEM_RECLAIM);
	if (!pool->wq) {
		*error = "Error creating pool's workqueue";
		err_p = ERR_PTR(-ENOMEM);
		goto bad_wq;
	}

	INIT_WORK(&pool->worker, do_worker);
	INIT_DELAYED_WORK(&pool->waker, do_waker);
	spin_lock_init(&pool->lock);

	pool->convert_wq = alloc_ordered_workqueue("dm-convert-" DM_MSG_PREFIX, WQ_MEM_RECLAIM);
	if (!pool->convert_wq) {
		*error = "Error creating pool's convert workqueue";
		err_p = ERR_PTR(-ENOMEM);
		goto bad_convert_wq;
	}

	bio_list_init(&pool->deferred_flush_bios);
	INIT_LIST_HEAD(&pool->prepared_mappings);
	INIT_LIST_HEAD(&pool->prepared_discards);
	INIT_LIST_HEAD_RCU(&pool->active_thins);
	pool->low_water_triggered = 0;
	pool->no_free_space = 0;
	pool->sb_backup_fail_reported = 0;
	pool->sync_io_triggered = 0;

	pool->shared_read_ds = dm_deferred_set_create();
	if (!pool->shared_read_ds) {
		*error = "Error creating pool's shared read deferred set";
		err_p = ERR_PTR(-ENOMEM);
		goto bad_shared_read_ds;
	}

	pool->all_io_ds = dm_deferred_set_create();
	if (!pool->all_io_ds) {
		*error = "Error creating pool's all io deferred set";
		err_p = ERR_PTR(-ENOMEM);
		goto bad_all_io_ds;
	}

	pool->next_mapping = NULL;
	pool->mapping_pool = mempool_create_slab_pool(MAPPING_POOL_SIZE,
	                     _new_mapping_cache);
	if (!pool->mapping_pool) {
		*error = "Error creating pool's mapping mempool";
		err_p = ERR_PTR(-ENOMEM);
		goto bad_mapping_pool;
	}

	pool->ref_count = 1;
	pool->last_commit_jiffies = jiffies;
	pool->pool_md = pool_md;
	pool->md_dev = metadata_dev;
	__pool_table_insert(pool);

	return pool;

bad_mapping_pool:
	dm_deferred_set_destroy(pool->all_io_ds);
bad_all_io_ds:
	dm_deferred_set_destroy(pool->shared_read_ds);
bad_shared_read_ds:
	destroy_workqueue(pool->convert_wq);
bad_convert_wq:
	destroy_workqueue(pool->wq);
bad_wq:
	dm_kcopyd_client_destroy(pool->copier);
bad_kcopyd_client:
	dm_bio_prison_destroy(pool->prison);
bad_prison:
	kfree(pool);
bad_pool:
	if (dm_pool_metadata_close(pmd))
		DMWARN("%s: dm_pool_metadata_close() failed.", __func__);

	return err_p;
}

static struct pool *__pool_find(struct mapped_device *pool_md,
                                struct block_device *metadata_dev,
                                unsigned long block_size, int read_only,
                                char **error, int *created)
{
	struct pool *pool = __pool_table_lookup_metadata_dev(metadata_dev);

	if (pool) {
		if (pool->pool_md != pool_md) {
			*error = "metadata device already in use by a pool";
			return ERR_PTR(-EBUSY);
		}
	} else {
		pool = __pool_table_lookup(pool_md);
		if (pool) {
			if (pool->md_dev != metadata_dev) {
				*error = "different pool cannot replace a pool";
				return ERR_PTR(-EINVAL);
			}
		} else {
			pool = pool_create(pool_md, metadata_dev, block_size, read_only, error);
			*created = 1;
		}
	}

	return pool;
}

/*----------------------------------------------------------------
 * Pool target methods
 *--------------------------------------------------------------*/
static void pool_dtr(struct dm_target *ti)
{
	struct pool_c *pt = ti->private;

	mutex_lock(&dm_thin_pool_table.mutex);

	unbind_control_target(pt->pool, ti);
	kobject_put(&pt->pool->kobj);
	dm_put_device(ti, pt->metadata_dev);
	dm_put_device(ti, pt->data_dev);
	kfree(pt);

	mutex_unlock(&dm_thin_pool_table.mutex);
}

static int parse_pool_features(struct dm_arg_set *as, struct pool_features *pf,
                               struct dm_target *ti)
{
	int r;
	unsigned argc;
	const char *arg_name;

	static struct dm_arg _args[] = {
		{0, 3, "Invalid number of pool feature arguments"},
	};

	/*
	 * No feature arguments supplied.
	 */
	if (!as->argc)
		return 0;

	r = dm_read_arg_group(_args, as, &argc, &ti->error);
	if (r)
		return -EINVAL;

	while (argc && !r) {
		arg_name = dm_shift_arg(as);
		argc--;

		if (!strcasecmp(arg_name, "skip_block_zeroing"))
			pf->zero_new_blocks = false;

		else if (!strcasecmp(arg_name, "ignore_discard"))
			pf->discard_enabled = false;

		else if (!strcasecmp(arg_name, "no_discard_passdown"))
			pf->discard_passdown = false;

		else if (!strcasecmp(arg_name, "read_only"))
			pf->mode = PM_READ_ONLY;

		else {
			ti->error = "Unrecognised pool feature requested";
			r = -EINVAL;
			break;
		}
	}

	return r;
}

static void metadata_low_callback(void *context)
{
	struct pool *pool = context;

	DMWARN("%s: reached low water mark for metadata device: sending event.",
	       dm_device_name(pool->pool_md));

	dm_table_event(pool->ti->table);
}

/*
 * When a metadata threshold is crossed a dm event is triggered, and
 * userland should respond by growing the metadata device.  We could let
 * userland set the threshold, like we do with the data threshold, but I'm
 * not sure they know enough to do this well.
 */
static dm_block_t calc_metadata_threshold(struct pool_c *pt)
{
	/*
	 * 4M is ample for all ops with the possible exception of thin
	 * device deletion which is harmless if it fails (just retry the
	 * delete after you've grown the device).
	 */
	dm_block_t quarter = get_metadata_dev_size_in_blocks(pt->pool->pmd, pt->metadata_dev->bdev) / 4;
	return min((dm_block_t)1024ULL /* 4M */, quarter);
}

/* -------------------------------------------------------------------- */

struct dm_sysfs_attr {
	struct attribute attr;
	ssize_t (*show)(struct pool *, char *);
	ssize_t (*store)(struct pool *, const char *, size_t);
};

#define DM_ATTR_RO(_name) \
struct dm_sysfs_attr dm_attr_##_name = \
       __ATTR(_name, S_IRUGO, dm_attr_##_name##_show, NULL)

#define DM_ATTR_WO(_name) \
struct dm_sysfs_attr dm_attr_##_name = \
       __ATTR(_name, S_IWUSR, NULL, dm_attr_##_name##_store)

#define DM_ATTR_WR(_name) \
struct dm_sysfs_attr dm_attr_##_name = \
       __ATTR(_name, S_IRUGO|S_IWUSR, dm_attr_##_name##_show, dm_attr_##_name##_store)

static ssize_t dm_attr_show(struct kobject *kobj, struct attribute *attr,
                            char *page)
{
	struct dm_sysfs_attr *dm_attr;
	struct pool *pool;
	ssize_t ret;

	dm_attr = container_of(attr, struct dm_sysfs_attr, attr);
	if (!dm_attr->show)
		return -EIO;

	mutex_lock(&dm_thin_pool_table.mutex);

	pool = container_of(kobj, struct pool, kobj);
	ret = dm_attr->show(pool, page);

	mutex_unlock(&dm_thin_pool_table.mutex);

	return ret;
}

static ssize_t dm_attr_store(struct kobject *kobj, struct attribute *attr,
                             const char *buf, size_t count)
{
	struct dm_sysfs_attr *dm_attr;
	struct pool *pool;
	ssize_t ret;

	dm_attr = container_of(attr, struct dm_sysfs_attr, attr);
	if (!dm_attr->show)
		return -EIO;

	mutex_lock(&dm_thin_pool_table.mutex);

	pool = container_of(kobj, struct pool, kobj);
	ret = dm_attr->store(pool, buf, count);

	mutex_unlock(&dm_thin_pool_table.mutex);

	return ret;
}

static ssize_t dm_attr_sync_io_threshold_show(struct pool *pool, char *buf)
{
	sprintf(buf, "%llu\n", pool->sync_io_threshold);

	return strlen(buf);
}

static ssize_t dm_attr_sync_io_threshold_store(struct pool *pool, const char *buf, size_t count)
{
	dm_block_t blocks;
	unsigned long flags;

	if (kstrtoull(buf, 10, &blocks))
		return -EINVAL;

	spin_lock_irqsave(&pool->lock, flags);
	pool->sync_io_threshold = blocks;
	spin_unlock_irqrestore(&pool->lock, flags);

	return count;
}

static void dm_pool_kobj_release(struct kobject *kobj)
{
	struct pool *pool = container_of(kobj, struct pool, kobj);

	BUG_ON(!mutex_is_locked(&dm_thin_pool_table.mutex));
	__pool_destroy(pool);

	return;
}

static DM_ATTR_WR(sync_io_threshold);

static struct attribute *dm_attrs[] = {
	&dm_attr_sync_io_threshold.attr,
	NULL,
};

static const struct sysfs_ops dm_sysfs_ops = {
	.show   = dm_attr_show,
	.store  = dm_attr_store,
};

static struct kobj_type dm_ktype = {
	.sysfs_ops      = &dm_sysfs_ops,
	.default_attrs  = dm_attrs,
	.release = dm_pool_kobj_release,
};

/* --------------------------------------------------------------------- */

/*
 * thin-pool <metadata dev> <data dev>
 *	     <data block size (sectors)>
 *	     <low water mark (blocks)>
 *	     [<#feature args> [<arg>]*]
 *
 * Optional feature arguments are:
 *	     skip_block_zeroing: skips the zeroing of newly-provisioned blocks.
 *	     ignore_discard: disable discard
 *	     no_discard_passdown: don't pass discards down to the data device
 */
static int pool_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
	int r, pool_created = 0;
	struct pool_c *pt;
	struct pool *pool;
	struct pool_features pf;
	struct dm_arg_set as;
	struct dm_dev *data_dev;
	unsigned long block_size;
	dm_block_t low_water_blocks;
	struct dm_dev *metadata_dev;
	fmode_t metadata_mode;

	/*
	 * FIXME Remove validation from scope of lock.
	 */
	mutex_lock(&dm_thin_pool_table.mutex);

	if (argc < 4) {
		ti->error = "Invalid argument count";
		r = -EINVAL;
		goto out_unlock;
	}

	as.argc = argc;
	as.argv = argv;

	/*
	 * Set default pool features.
	 */
	pool_features_init(&pf);

	dm_consume_args(&as, 4);
	r = parse_pool_features(&as, &pf, ti);
	if (r)
		goto out_unlock;

	metadata_mode = FMODE_READ | ((pf.mode == PM_READ_ONLY) ? 0 : FMODE_WRITE);
	r = dm_get_device(ti, argv[0], metadata_mode, &metadata_dev);
	if (r) {
		ti->error = "Error opening metadata block device";
		goto out_unlock;
	}

	/*
	 * Run for the side-effect of possibly issuing a warning if the
	 * device is too big.
	 * Mark this since we don't know the metadata size now
	 */
	//(void) get_metadata_dev_size(metadata_dev->bdev);

	r = dm_get_device(ti, argv[1], FMODE_READ | FMODE_WRITE, &data_dev);
	if (r) {
		ti->error = "Error getting data device";
		goto out_metadata;
	}

	if (kstrtoul(argv[2], 10, &block_size) || !block_size ||
	    block_size < DATA_DEV_BLOCK_SIZE_MIN_SECTORS ||
	    block_size > DATA_DEV_BLOCK_SIZE_MAX_SECTORS ||
	    block_size & (DATA_DEV_BLOCK_SIZE_MIN_SECTORS - 1)) {
		ti->error = "Invalid block size";
		r = -EINVAL;
		goto out;
	}

	if (kstrtoull(argv[3], 10, (unsigned long long *)&low_water_blocks)) {
		ti->error = "Invalid low water mark";
		r = -EINVAL;
		goto out;
	}

	pt = kzalloc(sizeof(*pt), GFP_KERNEL);
	if (!pt) {
		r = -ENOMEM;
		goto out;
	}

	pool = __pool_find(dm_table_get_md(ti->table), metadata_dev->bdev,
	                   block_size, pf.mode == PM_READ_ONLY, &ti->error, &pool_created);
	if (IS_ERR(pool)) {
		r = PTR_ERR(pool);
		if (r == -EVERSION)
			send_hal_msg(dm_table_get_md(ti->table), HAL_THIN_ERR_VERSION);
		goto out_free_pt;
	}

	if (report_sb_backup_fail(pool->pmd))
		send_hal_msg(pool, THIN_SB_BACKUP_FAIL);

	if (pool_created) {
		if (kobject_init_and_add(&pool->kobj, &dm_ktype,
		                         dm_kobject(pool->pool_md), "%s", "pool"))
			goto out_free_pt;
	} else
		kobject_get(&pool->kobj);

	/*
	 * 'pool_created' reflects whether this is the first table load.
	 * Top level discard support is not allowed to be changed after
	 * initial load.  This would require a pool reload to trigger thin
	 * device changes.
	 */
	if (!pool_created && pf.discard_enabled != pool->pf.discard_enabled) {
		ti->error = "Discard support cannot be disabled once enabled";
		r = -EINVAL;
		goto out_flags_changed;
	}

	pt->pool = pool;
	pt->ti = ti;
	pt->metadata_dev = metadata_dev;
	pt->data_dev = data_dev;
	pt->low_water_blocks = low_water_blocks;
	pt->adjusted_pf = pt->requested_pf = pf;
	ti->num_flush_bios = 1;

	/*
	 * Only need to enable discards if the pool should pass
	 * them down to the data device.  The thin device's discard
	 * processing will cause mappings to be removed from the btree.
	 */
	if (pf.discard_enabled && pf.discard_passdown) {
		ti->num_discard_bios = 1;

		/*
		 * Setting 'discards_supported' circumvents the normal
		 * stacking of discard limits (this keeps the pool and
		 * thin devices' discard limits consistent).
		 */
		ti->discards_supported = true;
		ti->discard_zeroes_data_unsupported = true;
	}
	ti->private = pt;

	r = dm_pool_register_metadata_threshold(pt->pool->pmd,
	                                        calc_metadata_threshold(pt),
	                                        metadata_low_callback,
	                                        pool);
	if (r)
		goto out_free_pt;

	pt->callbacks.congested_fn = pool_is_congested;
	dm_table_add_target_callbacks(ti->table, &pt->callbacks);

	mutex_unlock(&dm_thin_pool_table.mutex);

	return 0;

out_flags_changed:
	kobject_put(&pool->kobj);
out_free_pt:
	kfree(pt);
out:
	dm_put_device(ti, data_dev);
out_metadata:
	dm_put_device(ti, metadata_dev);
out_unlock:
	mutex_unlock(&dm_thin_pool_table.mutex);

	return r;
}

static int pool_map(struct dm_target *ti, struct bio *bio)
{
	int r;
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;
	unsigned long flags;

	/*
	 * As this is a singleton target, ti->begin is always zero.
	 */
	spin_lock_irqsave(&pool->lock, flags);
	bio->bi_bdev = pt->data_dev->bdev;
	r = DM_MAPIO_REMAPPED;
	spin_unlock_irqrestore(&pool->lock, flags);

	return r;
}

static int maybe_resize_data_dev(struct dm_target *ti, bool *need_commit)
{
	int r;
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;
	sector_t data_size = ti->len;
	dm_block_t sb_data_size;

	*need_commit = false;

	(void) sector_div(data_size, pool->sectors_per_block);

	r = dm_pool_get_data_dev_size(pool->pmd, &sb_data_size);
	if (r) {
		DMERR("failed to retrieve data device size");
		return r;
	}

	if (data_size < sb_data_size) {
		DMERR("pool target (%llu blocks) too small: expected %llu",
		      (unsigned long long)data_size, sb_data_size);
		return -EINVAL;

	} else if (data_size > sb_data_size) {
		r = dm_pool_resize_data_dev(pool->pmd, data_size);
		if (r) {
			DMERR("failed to resize data device");
			set_pool_mode(pool, PM_READ_ONLY);
			return r;
		}

		*need_commit = true;
	}

	return 0;
}

static int maybe_resize_metadata_dev(struct dm_target *ti, bool *need_commit)
{
	int r;
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;
	dm_block_t metadata_dev_size, sb_metadata_dev_size;

	*need_commit = false;

	metadata_dev_size = get_metadata_dev_size_in_blocks(pool->pmd, pool->md_dev);

	r = dm_pool_get_metadata_dev_size(pool->pmd, &sb_metadata_dev_size);
	if (r) {
		DMERR("failed to retrieve data device size");
		return r;
	}

	if (metadata_dev_size < sb_metadata_dev_size) {
		DMERR("metadata device (%llu blocks) too small: expected %llu",
		      metadata_dev_size, sb_metadata_dev_size);
		return -EINVAL;

	} else if (metadata_dev_size > sb_metadata_dev_size) {
		r = dm_pool_resize_metadata_dev(pool->pmd, metadata_dev_size);
		if (r) {
			DMERR("failed to resize metadata device");
			return r;
		}

		*need_commit = true;
	}

	return 0;
}

/*
 * Retrieves the number of blocks of the data device from
 * the superblock and compares it to the actual device size,
 * thus resizing the data device in case it has grown.
 *
 * This both copes with opening preallocated data devices in the ctr
 * being followed by a resume
 * -and-
 * calling the resume method individually after userspace has
 * grown the data device in reaction to a table event.
 */
static int pool_preresume(struct dm_target *ti)
{
	int r;
	bool need_commit1, need_commit2;
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;

	/*
	 * Take control of the pool object.
	 */
	r = bind_control_target(pool, ti);
	if (r)
		return r;

	r = maybe_resize_data_dev(ti, &need_commit1);
	if (r)
		return r;

	r = maybe_resize_metadata_dev(ti, &need_commit2);
	if (r)
		return r;

	if (need_commit1 || need_commit2)
		(void) commit_or_fallback(pool);

	return 0;
}

static void pool_resume(struct dm_target *ti)
{
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;
	unsigned long flags;

	spin_lock_irqsave(&pool->lock, flags);
	pool->low_water_triggered = 0;
	pool->no_free_space = 0;
	pool->sync_io_triggered = 0;
	requeue_bios(pool);
	spin_unlock_irqrestore(&pool->lock, flags);

	do_waker(&pool->waker.work);
}

static void pool_postsuspend(struct dm_target *ti)
{
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;

	cancel_delayed_work(&pool->waker);
	flush_workqueue(pool->wq);
	(void) commit_or_fallback(pool);
}

static int check_arg_count(unsigned argc, unsigned args_required)
{
	if (argc != args_required) {
		DMWARN("Message received with %u arguments instead of %u.",
		       argc, args_required);
		return -EINVAL;
	}

	return 0;
}

static int read_dev_id(char *arg, dm_thin_id *dev_id, int warning)
{
	if (!kstrtoull(arg, 10, (unsigned long long *)dev_id) &&
	    *dev_id <= MAX_DEV_ID)
		return 0;

	if (warning)
		DMWARN("Message received with invalid device id: %s", arg);

	return -EINVAL;
}

static int process_create_thin_mesg(unsigned argc, char **argv, struct pool *pool)
{
	dm_thin_id dev_id;
	int r;

	r = check_arg_count(argc, 2);
	if (r)
		return r;

	r = read_dev_id(argv[1], &dev_id, 1);
	if (r)
		return r;

	r = dm_pool_create_thin(pool->pmd, dev_id);
	if (r) {
		DMWARN("Creation of new thinly-provisioned device with id %s failed.",
		       argv[1]);
		return r;
	}

	return 0;
}

static int process_create_snap_mesg(unsigned argc, char **argv, struct pool *pool)
{
	dm_thin_id dev_id;
	dm_thin_id origin_dev_id;
	int r;

	r = check_arg_count(argc, 3);
	if (r)
		return r;

	r = read_dev_id(argv[1], &dev_id, 1);
	if (r)
		return r;

	r = read_dev_id(argv[2], &origin_dev_id, 1);
	if (r)
		return r;

	r = dm_pool_create_snap(pool->pmd, dev_id, origin_dev_id);
	if (r) {
		DMWARN("Creation of new snapshot %s of device %s failed.",
		       argv[1], argv[2]);
		return r;
	}

	return 0;
}

static int process_delete_mesg(unsigned argc, char **argv, struct pool *pool)
{
	unsigned long flags;
	dm_thin_id dev_id;
	int r;

	r = check_arg_count(argc, 2);
	if (r)
		return r;

	r = read_dev_id(argv[1], &dev_id, 1);
	if (r)
		return r;

	r = dm_pool_delete_thin_device(pool->pmd, dev_id);
	if (r)
		DMWARN("Deletion of thin device %s failed.", argv[1]);

	return r;
}

static int process_set_transaction_id_mesg(unsigned argc, char **argv, struct pool *pool)
{
	dm_thin_id old_id, new_id;
	int r;

	r = check_arg_count(argc, 3);
	if (r)
		return r;

	if (kstrtoull(argv[1], 10, (unsigned long long *)&old_id)) {
		DMWARN("set_transaction_id message: Unrecognised id %s.", argv[1]);
		return -EINVAL;
	}

	if (kstrtoull(argv[2], 10, (unsigned long long *)&new_id)) {
		DMWARN("set_transaction_id message: Unrecognised new id %s.", argv[2]);
		return -EINVAL;
	}

	r = dm_pool_set_metadata_transaction_id(pool->pmd, old_id, new_id);
	if (r) {
		DMWARN("Failed to change transaction id from %s to %s.",
		       argv[1], argv[2]);
		return r;
	}

	return 0;
}

static int process_reserve_metadata_snap_mesg(unsigned argc, char **argv, struct pool *pool)
{
	int r;

	r = check_arg_count(argc, 1);
	if (r)
		return r;

	(void) commit_or_fallback(pool);

	r = dm_pool_reserve_metadata_snap(pool->pmd);
	if (r)
		DMWARN("reserve_metadata_snap message failed.");

	return r;
}

static int process_release_metadata_snap_mesg(unsigned argc, char **argv, struct pool *pool)
{
	int r;

	r = check_arg_count(argc, 1);
	if (r)
		return r;

	r = dm_pool_release_metadata_snap(pool->pmd);
	if (r)
		DMWARN("release_metadata_snap message failed.");

	return r;
}

static int process_start_backup_sb_mesg(unsigned argc, char **argv, struct pool *pool)
{
	int r;

	r = check_arg_count(argc, 1);
	if (r) {
		return r;
	}

	r = dm_pool_start_backup_sb(pool->pmd);
	if (r) {
		DMWARN("start backup superblock failed");
	}

	return r;
}

static int process_stop_backup_sb_mesg(unsigned argc, char **argv, struct pool *pool)
{
	int r;

	r = check_arg_count(argc, 1);
	if (r) {
		return r;
	}

	r = dm_pool_stop_backup_sb(pool->pmd);
	if (r) {
		DMWARN("stop backup superblock failed");
	}

	return r;
}

static int process_thin_support_clone_mesg(unsigned argc, char **argv, struct pool *pool)
{
	int r;
	unsigned long block_size;
	THIN_BLOCKCLONE_DESC clone_desc;

	r = check_arg_count(argc, 6);
	if (r)
		return r;

	clone_desc.src_dev = lookup_bdev(argv[1]);
	if (IS_ERR(clone_desc.src_dev)) {
		DMERR("Cannot find block_device structure for path %s", argv[1]);
		return -EINVAL;
	}

	if (kstrtoull(argv[2], 10, (unsigned long long *)&clone_desc.src_block_addr)) {
		DMWARN("set_transaction_id message: Unrecognised id %s.", argv[2]);
		return -EINVAL;
	}

	clone_desc.dest_dev = lookup_bdev(argv[3]);
	if (IS_ERR(clone_desc.dest_dev)) {
		DMERR("Cannot find block_device structure for path %s", argv[3]);
		return -EINVAL;
	}

	if (kstrtoull(argv[4], 10, (unsigned long long *)&clone_desc.dest_block_addr)) {
		DMWARN("set_transaction_id message: Unrecognised id %s.", argv[4]);
		return -EINVAL;
	}

	if (kstrtoull(argv[5], 10, (unsigned long long *)&clone_desc.transfer_blocks)) {
		DMWARN("set_transaction_id message: Unrecognised id %s.", argv[5]);
		return -EINVAL;
	}

	DMINFO("\"%s\" and \"%s\" do%ssupport fast block cloning", argv[1], argv[3], thin_support_block_cloning(&clone_desc, &block_size) ? " not " : " ");
	DMINFO("Underlying pool block size is %lu", block_size);
	return 0;
}

static int process_thin_do_clone_mesg(unsigned argc, char **argv, struct pool *pool)
{
	int r;
	THIN_BLOCKCLONE_DESC *clone_desc;

	clone_desc = kzalloc(sizeof(THIN_BLOCKCLONE_DESC), GFP_KERNEL);
	if (!clone_desc)
		return -ENOMEM;

	r = check_arg_count(argc, 6);
	if (r)
		goto err_do_clone;

	clone_desc->src_dev = lookup_bdev(argv[1]);
	if (IS_ERR(clone_desc->src_dev)) {
		DMERR("Cannot find block_device structure for path %s", argv[1]);
		goto err_do_clone;
	}

	if (kstrtoull(argv[2], 10, (unsigned long long *)&clone_desc->src_block_addr)) {
		DMWARN("set_transaction_id message: Unrecognised id %s.", argv[2]);
		goto err_do_clone;
	}

	clone_desc->dest_dev = lookup_bdev(argv[3]);
	if (IS_ERR(clone_desc->dest_dev)) {
		DMERR("Cannot find block_device structure for path %s", argv[3]);
		goto err_do_clone;
	}

	if (kstrtoull(argv[4], 10, (unsigned long long *)&clone_desc->dest_block_addr)) {
		DMWARN("set_transaction_id message: Unrecognised id %s.", argv[4]);
		goto err_do_clone;
	}

	if (kstrtoull(argv[5], 10, (unsigned long long *)&clone_desc->transfer_blocks)) {
		DMWARN("set_transaction_id message: Unrecognised id %s.", argv[5]);
		goto err_do_clone;
	}

	return thin_do_block_cloning(clone_desc, NULL);

err_do_clone:
	kfree(clone_desc);
	return -EINVAL;
}

static int process_fast_block_clone_mesg(unsigned argc, char **argv, struct pool *pool)
{
	int r;

	r = check_arg_count(argc, 2);
	if (r) {
		DMERR("fast_block_clone message take exactly two arguments");
		return -EINVAL;
	}

	if (!strcasecmp(argv[1], "enable"))
		r = dm_pool_enable_block_clone(pool->pmd);
	else if (!strcasecmp(argv[1], "disable"))
		r = dm_pool_disable_block_clone(pool->pmd);
	else {
		DMERR("fast_block_clone message command %s unrecognised", argv[1]);
		r = -EINVAL;
	}

	return r;
}

static int process_get_count_mesg(unsigned argc, char **argv, struct pool *pool)
{
	int r;
	dm_block_t block;
	uint32_t refcount;

	r = check_arg_count(argc, 2);
	if (r) {
		DMERR("get count message take exactly two arguments");
		return -EINVAL;
	}

	if (kstrtoull(argv[1], 10, (unsigned long long *)&block)) {
		DMWARN("cannot identify block number %s", argv[1]);
		return -EINVAL;
	}

	r = dm_pool_get_refcount(pool->pmd, block, &refcount);
	if (!r)
		DMERR("%s: block %llu refcount = %u", __func__, block, refcount);

	return r;
}

/*
 * Messages supported:
 *   create_thin	<dev_id>
 *   create_snap	<dev_id> <origin_id>
 *   delete		<dev_id>
 *   trim		<dev_id> <new_size_in_sectors>
 *   set_transaction_id <current_trans_id> <new_trans_id>
 *   reserve_metadata_snap
 *   release_metadata_snap
 */
static int pool_message(struct dm_target *ti, unsigned argc, char **argv)
{
	int r = -EINVAL;
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;

	if (!strcasecmp(argv[0], "create_thin"))
		r = process_create_thin_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "create_snap"))
		r = process_create_snap_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "delete"))
		r = process_delete_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "set_transaction_id"))
		r = process_set_transaction_id_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "reserve_metadata_snap"))
		r = process_reserve_metadata_snap_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "release_metadata_snap"))
		r = process_release_metadata_snap_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "thin_support_clone"))
		r = process_thin_support_clone_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "thin_do_clone"))
		r = process_thin_do_clone_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "fast_block_clone"))
		r = process_fast_block_clone_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "get_count"))
		r = process_get_count_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "start_backup_sb"))
		r = process_start_backup_sb_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "stop_backup_sb"))
		r = process_stop_backup_sb_mesg(argc, argv, pool);

	else
		DMWARN("Unrecognised thin pool target message received: %s", argv[0]);

	if (!r)
		(void) commit_or_fallback(pool);

	return r;
}

static void emit_flags(struct pool_features *pf, char *result,
                       unsigned sz, unsigned maxlen)
{
	unsigned count = !pf->zero_new_blocks + !pf->discard_enabled +
	                 !pf->discard_passdown + (pf->mode == PM_READ_ONLY);

	DMEMIT("%u ", count);

	if (!pf->zero_new_blocks)
		DMEMIT("skip_block_zeroing ");

	if (!pf->discard_enabled)
		DMEMIT("ignore_discard ");

	if (!pf->discard_passdown)
		DMEMIT("no_discard_passdown ");

	if (pf->mode == PM_READ_ONLY)
		DMEMIT("read_only ");

}

/*
 * Status line is:
 *    <transaction id> <used metadata sectors>/<total metadata sectors>
 *    <used data sectors>/<total data sectors> <held metadata root>
 */
static void pool_status(struct dm_target *ti, status_type_t type,
                        unsigned status_flags, char *result, unsigned maxlen)
{
	int r;
	unsigned sz = 0;
	uint64_t transaction_id;
	dm_block_t nr_free_blocks_data;
	dm_block_t nr_free_blocks_metadata;
	dm_block_t nr_blocks_data;
	dm_block_t nr_blocks_metadata;
	dm_block_t held_root;
	char buf[BDEVNAME_SIZE];
	char buf2[BDEVNAME_SIZE];
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;

	switch (type) {
	case STATUSTYPE_INFO:
		if (get_pool_mode(pool) == PM_FAIL) {
			DMEMIT("Fail");
			break;
		}

		/* Commit to ensure statistics aren't out-of-date */
		if (!(status_flags & DM_STATUS_NOFLUSH_FLAG) && !dm_suspended(ti))
			(void) commit_or_fallback(pool);

		r = dm_pool_get_metadata_transaction_id(pool->pmd, &transaction_id);
		if (r) {
			DMERR("dm_pool_get_metadata_transaction_id returned %d", r);
			goto err;
		}

		r = dm_pool_get_free_metadata_block_count(pool->pmd, &nr_free_blocks_metadata);
		if (r) {
			DMERR("dm_pool_get_free_metadata_block_count returned %d", r);
			goto err;
		}

		r = dm_pool_get_metadata_dev_size(pool->pmd, &nr_blocks_metadata);
		if (r) {
			DMERR("dm_pool_get_metadata_dev_size returned %d", r);
			goto err;
		}

		r = dm_pool_get_free_block_count(pool->pmd, &nr_free_blocks_data);
		if (r) {
			DMERR("dm_pool_get_free_block_count returned %d", r);
			goto err;
		}

		r = dm_pool_get_data_dev_size(pool->pmd, &nr_blocks_data);
		if (r) {
			DMERR("dm_pool_get_data_dev_size returned %d", r);
			goto err;
		}

		r = dm_pool_get_metadata_snap(pool->pmd, &held_root);
		if (r) {
			DMERR("dm_pool_get_metadata_snap returned %d", r);
			goto err;
		}

		DMEMIT("%llu %llu/%llu %llu/%llu ",
		       (unsigned long long)transaction_id,
		       (unsigned long long)(nr_blocks_metadata - nr_free_blocks_metadata),
		       (unsigned long long)nr_blocks_metadata,
		       (unsigned long long)(nr_blocks_data - nr_free_blocks_data),
		       (unsigned long long)nr_blocks_data);

		if (held_root)
			DMEMIT("%llu ", held_root);
		else
			DMEMIT("- ");

		if (pool->pf.mode == PM_READ_ONLY)
			DMEMIT("ro ");
		else
			DMEMIT("rw ");

		if (!pool->pf.discard_enabled)
			DMEMIT("ignore_discard ");
		else if (pool->pf.discard_passdown)
			DMEMIT("discard_passdown ");
		else
			DMEMIT("no_discard_passdown ");

		if (support_fast_block_clone(pool->pmd))
			DMEMIT("fast_block_clone ");

		if (dm_pool_support_superblock_backup(pool->pmd))
			DMEMIT("sb_backup ");
		break;

	case STATUSTYPE_TABLE:
		DMEMIT("%s %s %lu %llu ",
		       format_dev_t(buf, pt->metadata_dev->bdev->bd_dev),
		       format_dev_t(buf2, pt->data_dev->bdev->bd_dev),
		       (unsigned long)pool->sectors_per_block,
		       (unsigned long long)pt->low_water_blocks);
		emit_flags(&pt->requested_pf, result, sz, maxlen);

		break;
	}
	return;

err:
	DMEMIT("Error");
}

static int pool_iterate_devices(struct dm_target *ti,
                                iterate_devices_callout_fn fn, void *data)
{
	struct pool_c *pt = ti->private;

	return fn(ti, pt->data_dev, 0, ti->len, data);
}

static int pool_merge(struct dm_target *ti, struct bvec_merge_data *bvm,
                      struct bio_vec *biovec, int max_size)
{
	struct pool_c *pt = ti->private;
	struct request_queue *q = bdev_get_queue(pt->data_dev->bdev);

	if (!q->merge_bvec_fn)
		return max_size;

	bvm->bi_bdev = pt->data_dev->bdev;

	return min(max_size, q->merge_bvec_fn(q, bvm, biovec));
}

static int do_block_clone(struct pool *pool, dm_thin_id src_dev_id, sector_t src_addr, dm_thin_id dst_dev_id, sector_t dst_addr, sector_t length)
{
	int i, r;
	struct dm_thin_device *src_td, *dst_td;
	struct dm_thin_lookup_result sresult, dresult;
	dm_block_t src_blk = src_addr, dst_blk = dst_addr, blk_num = length;

	(void) sector_div(src_blk, pool->sectors_per_block);
	(void) sector_div(dst_blk, pool->sectors_per_block);

	if (sector_div(blk_num, pool->sectors_per_block))
		return -EINVAL;

	r = dm_pool_open_thin_device(pool->pmd, src_dev_id, &src_td);
	if (r)
		return r;

	r = dm_pool_open_thin_device(pool->pmd, dst_dev_id, &dst_td);
	if (r)
		goto close_src_dev;

	DMDEBUG("%s: clone from dev-%u to dev-%u for %llu blocks", __func__, src_dev_id, dst_dev_id, blk_num);

	for (i = 0; i < (uint64_t)blk_num; i++) {
		r = dm_thin_find_block(src_td, src_blk, 1, &sresult);
		switch (r) {
		case 0:
			DMDEBUG("%s: src find block from %llu to %llu", __func__, src_blk, sresult.block);
			r = dm_thin_find_block(dst_td, dst_blk, 1, &dresult);
			if (!r && dresult.block == sresult.block) {
				if (dresult.zeroed == sresult.zeroed) {
					DMDEBUG("%s: copy block to the same position, bypass");
				} else {
					r = dm_thin_insert_block_with_time(dst_td, dst_blk, dresult.block, sresult.zeroed, &sresult.time);
					if (r)
						goto close_dst_dev;
				}
				break;
			}

			if (!sresult.shared) {
				sresult.time -= 1;
				r = dm_thin_insert_block_with_time(src_td, src_blk, sresult.block, sresult.zeroed, &sresult.time);
				DMDEBUG("%s: insert on src to %llu", __func__, sresult.block);
				if (r)
					goto close_dst_dev;
			}
			// Additional Check
			BUG_ON(!sresult.shared && dm_get_current_time(pool->pmd) == sresult.time);

			r = dm_thin_insert_block_with_time(dst_td, dst_blk, sresult.block, sresult.zeroed, &sresult.time);
			if (r)
				goto close_dst_dev;

			dm_pool_inc_refcount(pool->pmd, sresult.block);
			DMDEBUG("%s: increase %llu reference count", __func__, sresult.block);
			break;
		case -ENODATA:
			r = 0;
			break;
		default:
			goto close_dst_dev;
		}

		src_blk += 1;
		dst_blk += 1;
	}
close_dst_dev:
	dm_pool_close_thin_device(dst_td);
close_src_dev:
	dm_pool_close_thin_device(src_td);

	DMDEBUG("%s: close all device, ready to return", __func__);

	return r;
}

static void set_discard_limits(struct pool_c *pt, struct queue_limits *limits)
{
	struct pool *pool = pt->pool;
	struct queue_limits *data_limits;

	limits->max_discard_sectors = pool->sectors_per_block;

	/*
	 * discard_granularity is just a hint, and not enforced.
	 */
	if (pt->adjusted_pf.discard_passdown) {
		data_limits = &bdev_get_queue(pt->data_dev->bdev)->limits;
		limits->discard_granularity = data_limits->discard_granularity;
	} else
		limits->discard_granularity = pool->sectors_per_block << SECTOR_SHIFT;
}

static int pool_do_fast_block_clone(struct dm_target * ti, THIN_REMAP_DESC * srd, THIN_REMAP_DESC * drd, sector_t len)
{
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;

	return do_block_clone(pool, srd->dev_id, srd->addr, drd->dev_id, drd->addr, len);
}


static void pool_io_hints(struct dm_target *ti, struct queue_limits *limits)
{
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;

	blk_limits_io_min(limits, 0);
	blk_limits_io_opt(limits, pool->sectors_per_block << SECTOR_SHIFT);

	/*
	 * pt->adjusted_pf is a staging area for the actual features to use.
	 * They get transferred to the live pool in bind_control_target()
	 * called from pool_preresume().
	 */
	if (!pt->adjusted_pf.discard_enabled)
		return;

	disable_passdown_if_not_supported(pt);

	set_discard_limits(pt, limits);
}

static struct target_type pool_target = {
	.name = "thin-pool",
	.features = DM_TARGET_SINGLETON | DM_TARGET_ALWAYS_WRITEABLE |
	DM_TARGET_IMMUTABLE,
	.version = {1, 8, 0},
	.module = THIS_MODULE,
	.ctr = pool_ctr,
	.dtr = pool_dtr,
	.map = pool_map,
	.postsuspend = pool_postsuspend,
	.preresume = pool_preresume,
	.resume = pool_resume,
	.message = pool_message,
	.status = pool_status,
	.merge = pool_merge,
	.iterate_devices = pool_iterate_devices,
	.io_hints = pool_io_hints,
	.fast_block_clone = pool_do_fast_block_clone,
};

/*----------------------------------------------------------------
 * Thin target methods
 *--------------------------------------------------------------*/

static void thin_to_thick(struct work_struct *ws);
static void thin_clean_all(struct work_struct *ws);

static int add_job(struct thin_c *tc, struct convert_work *cw)
{
	int r = 0;
	unsigned long flags;

	spin_lock_irqsave(&cw->lock, flags);
	/*
	 * Since we check if this work is work busy, we should never found
	 * the same job in the workqueue.
	 */
	if (!work_busy(&cw->work)) {
		cw->status = T2T_READY;
		WARN_ON(!queue_work(tc->pool->convert_wq, &cw->work));
	} else
		r = -EINVAL;

	spin_unlock_irqrestore(&cw->lock, flags);

	return r;
}

static void cancel_job(struct thin_c *tc, struct convert_work *cw)
{
	unsigned long flags;

	spin_lock_irqsave(&cw->lock, flags);
	cw->cancel = 1;
	spin_unlock_irqrestore(&cw->lock, flags);

	cancel_work_sync(&cw->work);

	spin_lock_irqsave(&cw->lock, flags);
	cw->cancel = 0;
	spin_unlock_irqrestore(&cw->lock, flags);
}

static void thin_dtr(struct dm_target *ti)
{
	struct thin_c *tc = ti->private;
	unsigned long flags;

	spin_lock_irqsave(&tc->pool->lock, flags);
	list_del_rcu(&tc->list);
	spin_unlock_irqrestore(&tc->pool->lock, flags);
	synchronize_rcu();

	cancel_job(tc, &tc->thick_work);

	WARN_ON(flush_work(&(tc->remove_work.work)));

	mutex_lock(&dm_thin_pool_table.mutex);

	if (tc->dm_monitor_fn)
		tc->dm_monitor_fn(tc->lundev, 1);

	kobject_put(&tc->pool->kobj);
	dm_pool_close_thin_device(tc->td);
	dm_put_device(ti, tc->pool_dev);
	if (tc->origin_dev)
		dm_put_device(ti, tc->origin_dev);
	kfree(tc);

	mutex_unlock(&dm_thin_pool_table.mutex);
}

static void init_convert_work(struct convert_work *cw, work_func_t func)
{
	spin_lock_init(&cw->lock);
	cw->cancel = 0;
	cw->status = T2T_READY;
	INIT_WORK(&cw->work, func);
}

/*
 * Thin target parameters:
 *
 * <pool_dev> <dev_id> [origin_dev]
 *
 * pool_dev: the path to the pool (eg, /dev/mapper/my_pool)
 * dev_id: the internal device identifier
 * origin_dev: a device external to the pool that should act as the origin
 *
 * If the pool device has discards disabled, they get disabled for the thin
 * device as well.
 */
static int thin_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
	int r;
	struct thin_c *tc;
	struct dm_dev *pool_dev, *origin_dev;
	struct mapped_device *pool_md;

	mutex_lock(&dm_thin_pool_table.mutex);


	/*
	 * FIXME: remove the thin_prealloc
	 */
	if (argc != 2 && argc != 3) {
		ti->error = "Invalid argument count";
		r = -EINVAL;
		goto out_unlock;
	}

	tc = ti->private = kzalloc(sizeof(*tc), GFP_KERNEL);
	if (!tc) {
		ti->error = "Out of memory";
		r = -ENOMEM;
		goto out_unlock;
	}

	spin_lock_init(&tc->lock);
	bio_list_init(&tc->deferred_bio_list);
	bio_list_init(&tc->retry_on_resume_list);
	tc->sort_bio_list = RB_ROOT;

	if (argc == 3) {
		r = dm_get_device(ti, argv[2], FMODE_READ, &origin_dev);
		if (r) {
			ti->error = "Error opening origin device";
			goto bad_origin_dev;
		}
		tc->origin_dev = origin_dev;
	}

	r = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &pool_dev);
	if (r) {
		ti->error = "Error opening pool device";
		goto bad_pool_dev;
	}
	tc->pool_dev = pool_dev;

	if (read_dev_id(argv[1], (unsigned long long *)&tc->dev_id, 0)) {
		ti->error = "Invalid device id";
		r = -EINVAL;
		goto bad_common;
	}

	pool_md = dm_get_md(tc->pool_dev->bdev->bd_dev);
	if (!pool_md) {
		ti->error = "Couldn't get pool mapped device";
		r = -EINVAL;
		goto bad_common;
	}

	tc->pool = __pool_table_lookup(pool_md);
	if (!tc->pool) {
		ti->error = "Couldn't find pool object";
		r = -EINVAL;
		goto bad_pool_lookup;
	}
	kobject_get(&tc->pool->kobj);

	if (get_pool_mode(tc->pool) == PM_FAIL) {
		ti->error = "Couldn't open thin device, Pool is in fail mode";
		goto bad_thin_open;
	}

	r = dm_pool_open_thin_device(tc->pool->pmd, tc->dev_id, &tc->td);
	if (r) {
		ti->error = "Couldn't open thin internal device";
		goto bad_thin_open;
	}

	r = dm_set_target_max_io_len(ti, tc->pool->sectors_per_block);
	if (r)
		goto bad_thin_open;

	ti->num_flush_bios = 1;
	ti->flush_supported = true;
	ti->per_bio_data_size = sizeof(struct dm_thin_endio_hook);

	ti->discard_zeroes_data_unsupported = true;
	/* In case the pool supports discards, pass them on. */
	if (tc->pool->pf.discard_enabled) {
		ti->discards_supported = true;
		ti->num_discard_bios = 1;
		/* Discard bios must be split on a block boundary */
		ti->split_discard_bios = true;
	} else
		ti->discards_supported = false;

	tc->len = ti->len;
	init_convert_work(&tc->thick_work, thin_to_thick);
	init_convert_work(&tc->remove_work, thin_clean_all);

	tc->dm_monitor_fn = NULL;
	tc->lundev = NULL;
	tc->is_lun = false;

	tc->discard_passdown = tc->pool->pf.discard_passdown;

	dm_put(pool_md);

	mutex_unlock(&dm_thin_pool_table.mutex);

	spin_lock(&tc->pool->lock);
	list_add_tail_rcu(&tc->list, &tc->pool->active_thins);
	spin_unlock(&tc->pool->lock);
	/*
	 * This synchronize_rcu() call is needed here otherwise we risk a
	 * wake_worker() call finding no bios to process (because the newly
	 * added tc isn't yet visible).  So this reduces latency since we
	 * aren't then dependent on the periodic commit to wake_worker().
	 */
	synchronize_rcu();
	// FIXME: We should remove thick target
	if (!strcasecmp(ti->type->name, "thick")) {
		tc->is_thick = true;
		add_job(tc, &tc->thick_work);
	} else
		tc->is_thick = false;

	return 0;

bad_thin_open:
	kobject_put(&tc->pool->kobj);
bad_pool_lookup:
	dm_put(pool_md);
bad_common:
	dm_put_device(ti, tc->pool_dev);
bad_pool_dev:
	if (tc->origin_dev)
		dm_put_device(ti, tc->origin_dev);
bad_origin_dev:
	kfree(tc);
out_unlock:
	mutex_unlock(&dm_thin_pool_table.mutex);

	return r;
}

static int thin_map(struct dm_target *ti, struct bio *bio)
{
	bio->bi_sector = dm_target_offset(ti, bio->bi_sector);

	return thin_bio_map(ti, bio);
}

static int thin_endio(struct dm_target *ti, struct bio *bio, int err)
{
	unsigned long flags;
	struct dm_thin_endio_hook *h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));
	struct list_head work;
	struct dm_thin_new_mapping *m, *tmp;
	struct pool *pool = h->tc->pool;

	if (h->shared_read_entry) {
		INIT_LIST_HEAD(&work);
		dm_deferred_entry_dec(h->shared_read_entry, &work);

		spin_lock_irqsave(&pool->lock, flags);
		list_for_each_entry_safe(m, tmp, &work, list) {
			list_del(&m->list);
			m->quiesced = 1;
			__maybe_add_mapping(m);
		}
		spin_unlock_irqrestore(&pool->lock, flags);
	}

	if (h->all_io_entry) {
		INIT_LIST_HEAD(&work);
		dm_deferred_entry_dec(h->all_io_entry, &work);
		if (!list_empty(&work)) {
			spin_lock_irqsave(&pool->lock, flags);
			list_for_each_entry_safe(m, tmp, &work, list)
			list_add_tail(&m->list, &pool->prepared_discards);
			spin_unlock_irqrestore(&pool->lock, flags);
			wake_worker(pool);
		}
	}

	return 0;
}

static void thin_postsuspend(struct dm_target *ti)
{
	if (dm_noflush_suspending(ti))
		requeue_io((struct thin_c *)ti->private);
}

static void set_work_status(struct convert_work *cw, enum T2T_STATE status)
{
	unsigned long flags;

	spin_lock_irqsave(&cw->lock, flags);
	cw->status = status;
	spin_unlock_irqrestore(&cw->lock, flags);
}

static char *report_work_status(struct convert_work *cw)
{
	unsigned int index;
	unsigned long flags;

	index = work_busy(&cw->work);
	index &= (WORK_BUSY_RUNNING | WORK_BUSY_PENDING);
	if (index)
		goto status_confirm;

	spin_lock_irqsave(&cw->lock, flags);
	index = cw->status;
	spin_unlock_irqrestore(&cw->lock, flags);

	if (index > __MAX_NR_STATE)
		index = __MAX_NR_STATE;

status_confirm:
	return t2t_state_name[index];
}

/*
 * <nr mapped sectors> <highest mapped sector>
 */
static void thin_status(struct dm_target *ti, status_type_t type,
                        unsigned status_flags, char *result, unsigned maxlen)
{
	int r;
	ssize_t sz = 0;
	dm_block_t mapped, highest, root;
	char buf[BDEVNAME_SIZE];
	struct thin_c *tc = ti->private;

	if (get_pool_mode(tc->pool) == PM_FAIL) {
		DMEMIT("Fail");
		return;
	}

	if (!tc->td)
		DMEMIT("-");
	else {
		switch (type) {
		case STATUSTYPE_INFO:
			r = dm_thin_get_mapped_count(tc->td, &mapped);
			if (r) {
				DMERR("dm_thin_get_mapped_count returned %d", r);
				goto err;
			}

			r = dm_thin_get_highest_mapped_block(tc->td, &highest);
			if (r < 0) {
				DMERR("dm_thin_get_highest_mapped_block returned %d", r);
				goto err;
			}

			DMEMIT("%llu ", mapped * tc->pool->sectors_per_block);
			if (r)
				DMEMIT("%llu", ((highest + 1) *
				                tc->pool->sectors_per_block) - 1);
			else
				DMEMIT("-");

			DMEMIT(" %s %s ", report_work_status(&tc->thick_work),
			       report_work_status(&tc->remove_work));

			r = dm_pool_get_snap_root(tc->pool->pmd, tc->td, &root);
			if (r) {
				DMERR("dm_pool_get_snap_root returned %d", r);
				goto err;
			}
			DMEMIT("%llu ", root);

			break;

		case STATUSTYPE_TABLE:
			DMEMIT("%s %lu",
			       format_dev_t(buf, tc->pool_dev->bdev->bd_dev),
			       (unsigned long) tc->dev_id);
			if (tc->origin_dev)
				DMEMIT(" %s", format_dev_t(buf, tc->origin_dev->bdev->bd_dev));
			break;
		}
	}

	return;

err:
	DMEMIT("Error");
}

static int thin_iterate_devices(struct dm_target *ti,
                                iterate_devices_callout_fn fn, void *data)
{
	sector_t blocks;
	struct thin_c *tc = ti->private;
	struct pool *pool = tc->pool;

	/*
	 * We can't call dm_pool_get_data_dev_size() since that blocks.  So
	 * we follow a more convoluted path through to the pool's target.
	 */
	if (!pool->ti)
		return 0;	/* nothing is bound */

	blocks = pool->ti->len;
	(void) sector_div(blocks, pool->sectors_per_block);
	if (blocks)
		return fn(ti, tc->pool_dev, 0, pool->sectors_per_block * blocks, data);

	return 0;
}

static void thin_to_thick(struct work_struct *ws)
{
	int r = 0, cancel = 0;
	unsigned long flags;
	struct convert_work *cw = container_of(ws, struct convert_work, work);
	struct thin_c *tc = container_of(cw, struct thin_c, thick_work);
	struct pool *pool = tc->pool;
	struct dm_bio_prison_cell *cell;
	struct dm_cell_key key;
	struct dm_thin_device *td = tc->td;
	sector_t len = tc->len;
	dm_block_t i, result, granu = 100, start = 0;

	do_div(len, pool->sectors_per_block);

	DMDEBUG("%s: volume %llu thin_to_thick thread start running", __func__, tc->dev_id);

	do {
		for (i = 0; i < granu; i++, start++) {
			if (start >= len)
				goto out;

retry:
			build_virtual_key(td, start, &key);
			if (bio_detain(pool, &key, NULL, &cell)) {
				msleep(300);
				goto retry;
			}

			r = dm_thin_deploy(td, start, &result);
			if (!r) {
				DMDEBUG("%s: block %llu deployed", __func__, start);
				cell_defer_no_holder(tc, cell);
				continue;
			}

			if (pool->sync_io_triggered) {
				DMERR("%s: sync io triggered, thick create failed when allocating %llu",
						__func__, start);
				r = -ENOSPC;
				goto err_out;
			}

			r = alloc_data_block(tc, &result);
			if (r) {
				cell_defer_no_holder(tc, cell);
				goto err_out;
			}

			r = dm_thin_insert_block(td, start, result, 0);
			if (r) {
				cell_defer_no_holder(tc, cell);
				goto err_out;
			}

			cell_defer_no_holder(tc, cell);
		}
		spin_lock_irqsave(&cw->lock, flags);
		cancel = cw->cancel;
		spin_unlock_irqrestore(&cw->lock, flags);
	} while (!cancel);

out:
	set_work_status(cw, cancel ? T2T_CANCEL : T2T_SUCCESS);
	DMDEBUG("%s: volume %llu thin_to_thick thread stop normally", __func__, tc->dev_id);
	return;

err_out:
	set_work_status(cw, T2T_FAIL);
	DMDEBUG("%s: volume %llu thin_to_thick thread stop %s",
		__func__, tc->dev_id, (r == -ENOSPC) ? "due to no free space" : "unexpectedly");
	return;
}

static int process_thin_to_thick_mesg(struct dm_target *ti, unsigned argc, char **argv)
{
	int r;
	struct thin_c *tc = ti->private;

	r = check_arg_count(argc, 2);
	if (r)
		return r;

	if (!strcasecmp(argv[1], "start"))
		add_job(tc, &tc->thick_work);

	else if (!strcasecmp(argv[1], "stop"))
		cancel_job(tc, &tc->thick_work);

	return r;
}

static void thin_clean_all(struct work_struct *ws)
{
	int r = 0;
	struct convert_work *cw = container_of(ws, struct convert_work, work);
	struct thin_c *tc = container_of(cw, struct thin_c, remove_work);
	struct pool *pool = tc->pool;
	struct dm_thin_device *td = tc->td;
	dm_block_t start = 0, len = 0;

	DMDEBUG("%s: volume %llu thin_clean_all thread start running", __func__, tc->dev_id);

	r = dm_thin_get_highest_mapped_block(tc->td, &len);
	if (r < 0) {
		DMERR("%s: dm_thin_get_highest_mapped_block returned %d", __func__, r);
		DMERR("%s: fallback to discard all blocks", __func__);
		len = (dm_block_t)tc->len;
		do_div(len, pool->sectors_per_block);
	}

	for (start = 0; start <= len; start++) {
		DMDEBUG("%s: remove block %llu", __func__, start);
		r = dm_thin_remove_block(td, start);
		if (r && r != -ENODATA) {
			DMERR("%s block %llu removed fail", __func__, start);
			set_work_status(cw, T2T_FAIL);
			return;
		}
	}

	set_work_status(cw, T2T_SUCCESS);
	DMDEBUG("%s: volume %llu thin_clean_all thread stop", __func__, tc->dev_id);
}

static int process_thin_pre_remove(struct dm_target *ti, unsigned argc, char **argv)
{
	int r = 0;
	struct thin_c *tc = ti->private;

	r = check_arg_count(argc, 1);
	if (r)
		return r;

	cancel_job(tc, &tc->thick_work);

	DMERR("%s: enqueue pre_remove work", __func__);
	add_job(tc, &tc->remove_work);

	return r;
}

static int process_thin_set_discard_passdown(struct dm_target *ti, unsigned argc, char **argv)
{
	int r;
	struct thin_c *tc = ti->private;

	r = check_arg_count(argc, 1);
	if (r)
		return r;

	if (!strcasecmp(argv[0], "discard_passdown"))
		tc->discard_passdown = true;
	else if (!strcasecmp(argv[0], "no_discard_passdown"))
		tc->discard_passdown = false;

	return 0;
}

static int process_thin_is_lun_mesg(struct dm_target *ti, unsigned argc, char **argv)
{
	int r;
	struct thin_c *tc = ti->private;

	r = check_arg_count(argc, 1);
	if (r)
		return r;

	spin_lock(&tc->lock);
	tc->is_lun = true;
	spin_unlock(&tc->lock);

	return 0;
}

static int thin_message(struct dm_target *ti, unsigned argc, char **argv)
{
	int r = -EINVAL;

	if (!strcasecmp(argv[0], "pre_remove"))
		r = process_thin_pre_remove(ti, argc, argv);

	else if (!strcasecmp(argv[0], "to_thick"))
		r = process_thin_to_thick_mesg(ti, argc, argv);

	else if (!strcasecmp(argv[0], "is_lun"))
		r = process_thin_is_lun_mesg(ti, argc, argv);

	else if (!strcasecmp(argv[0], "discard_passdown") || !strcasecmp(argv[0], "no_discard_passdown"))
		r = process_thin_set_discard_passdown(ti, argc, argv);

	else
		DMWARN("Unrecognised thin target message received: %s", argv[0]);

	return r;
}

/* Some help functions for iSCSI or other modules internal use */

static bool is_thin_target(struct dm_target *ti)
{
	if (!ti || !ti->private)
		return false;

	if (strcasecmp(ti->type->name, "thin"))
		return false;

	return true;
}

/*
 * ti: dm_targe of thin or thick get from thin_get_dmtarget()
 * dev: LUN struct link
 * dm_monitor_fn: dmmonitor call back function
 * return -1: fail, 0: successful
 */
int thin_set_dm_monitor(struct dm_target *ti, void *dev, void (*dm_monitor_fn)(void *, int))
{
	struct thin_c *tc;

	if (!is_thin_target(ti))
		return -1;

	mutex_lock(&dm_thin_pool_table.mutex);

	tc = ti->private;
	tc->dm_monitor_fn = dm_monitor_fn;
	tc->lundev = dev;

	mutex_unlock(&dm_thin_pool_table.mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(thin_set_dm_monitor);

int thin_get_dmtarget(char *name, struct dm_target **result);
/*
 * name: myvg-thin0
 * index: start index number of block data
 * len: query total number
 * result 0: deployed (mapped)
 * result 1: not deployed (deallocated)
 * return -1: fail, 0: successful
 */
int thin_get_lba_status(char *name, uint64_t index, uint64_t len, uint8_t *result)
{
	int r;
	uint64_t i;
	dm_block_t d;
	struct dm_target *ti;
	struct thin_c *tc;

	if (!len || thin_get_dmtarget(name, &ti))
		return -1;

	if (!is_thin_target(ti))
		return -1;

	tc = ti->private;

	mutex_lock(&dm_thin_pool_table.mutex);

	for ( i = 0; i < len; i++) {
		r = dm_thin_deploy(tc->td, index + i, &d);
		if (r && r != -ENODATA)
			return -1;

		result[i] = (r) ? 1 : 0;
	}

	mutex_unlock(&dm_thin_pool_table.mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(thin_get_lba_status);

/*
 * name: myvg-thin0
 * result: the number of sectors per block
 * return -1: fail, 0: success
 */
int thin_get_sectors_per_block(char *name, uint32_t *result)
{
	struct dm_target *ti;
	struct thin_c *tc;

	if (thin_get_dmtarget(name, &ti) || !is_thin_target(ti))
		return -1;

	tc = ti->private;
	*result = tc->pool->sectors_per_block;

	return 0;
}
EXPORT_SYMBOL_GPL(thin_get_sectors_per_block);

/*
 * name: myvg-thin0
 * total_size: thin volume total size (unit is sector)
 * used_size: thin volume used size (unit is sector)
 * return -1: fail, 0: successful
 */
int thin_get_data_status(struct dm_target *ti, uint64_t *total_size, uint64_t *used_size)
{
	struct thin_c *tc;
	dm_block_t mapped;

	if (!is_thin_target(ti))
		return -1;

	tc = ti->private;
	*total_size = (uint64_t)ti->len;

	if (dm_thin_get_mapped_count(tc->td, &mapped))
		return -1;

	*used_size = (uint64_t)(mapped * tc->pool->sectors_per_block);

	return 0;
}
EXPORT_SYMBOL_GPL(thin_get_data_status);

/*
 * Let other modules query pool status
 * return 1 : switch to sync I/O
 *        0 : normal I/O
 *        -ENOSPC: no space in pool
 */
int dm_thin_volume_is_full(void *data)
{
	struct thin_c *tc;
	struct pool *pool = (struct pool *)data;

	if (unlikely(!pool))
		return -EINVAL;

	if (pool->no_free_space)
		return -ENOSPC;

	if (pool->sync_io_triggered)
		return 1;

	return 0;
}
EXPORT_SYMBOL_GPL(dm_thin_volume_is_full);

static int thin_merge(struct dm_target *ti, struct bvec_merge_data *bvm,
                      struct bio_vec *biovec, int max_size)
{
	struct thin_c *tc = ti->private;
	struct pool *pool = tc->pool;
	struct request_queue *q = bdev_get_queue(tc->pool_dev->bdev);

	if (!q->merge_bvec_fn)
		return max_size;

	bvm->bi_bdev = tc->pool_dev->bdev;
	if (block_size_is_power_of_two(pool))
		bvm->bi_sector = bvm->bi_sector & (pool->sectors_per_block - 1);
	else
		bvm->bi_sector = sector_div(bvm->bi_sector, pool->sectors_per_block);

	return min(max_size, q->merge_bvec_fn(q, bvm, biovec));
}

static int thin_locate_thin(struct dm_target * ti, locate_thin_callout_fn fn, sector_t start, sector_t len, void *remap_desc, void **thin)
{
	struct thin_c *tc = ti->private;
	THIN_REMAP_DESC *rd = (THIN_REMAP_DESC *)remap_desc;

	if (thin)
		*thin = (void *)(tc->pool);

	if (ti->len < start + len)
		return -EINVAL;

	if (!support_fast_block_clone(tc->pool->pmd)) {
		DMDEBUG("Users disable fast block clone feature, return failed");
		return -EINVAL;
	}

	if (rd) {
		rd->ti = tc->pool->ti;
		rd->pool = (void *)tc->pool;
		rd->dev_id = tc->dev_id;
		rd->addr = start;
		rd->block_size = tc->pool->sectors_per_block << SECTOR_SHIFT;
	}

	return 0;
}

static int thin_invalidate(struct dm_target * ti, sector_t start, sector_t len, invalidate_callback_fn fn, void *data)
{
	DMDEBUG("%s: ready to invalidate");

	return (*fn)(data, NULL, 0);
}


static struct target_type thin_target = {
	.name = "thin",
	.version = {1, 8, 0},
	.module	= THIS_MODULE,
	.ctr = thin_ctr,
	.dtr = thin_dtr,
	.map = thin_map,
	.end_io = thin_endio,
	.postsuspend = thin_postsuspend,
	.status = thin_status,
	.iterate_devices = thin_iterate_devices,
	.locate_thin = thin_locate_thin,
	.invalidate = thin_invalidate,
	.message = thin_message,
	.merge = thin_merge,
};

static struct target_type thick_target = {
	.name = "thick",
	.version = {1, 9, 0},
	.module = THIS_MODULE,
	.ctr = thin_ctr,
	.dtr = thin_dtr,
	.map = thin_map,
	.end_io = thin_endio,
	.postsuspend = thin_postsuspend,
	.status = thin_status,
	.iterate_devices = thin_iterate_devices,
	.locate_thin = thin_locate_thin,
	.invalidate = thin_invalidate,
	.message = thin_message,
	.merge = thin_merge,
};

/*----------------------------------------------------------------*/

static int __init dm_thin_init(void)
{
	int r;

	pool_table_init();

	r = dm_register_target(&thin_target);
	if (r)
		return r;

	r = dm_register_target(&thick_target);
	if (r)
		goto bad_thick_target;

	r = dm_register_target(&pool_target);
	if (r)
		goto bad_pool_target;

	r = -ENOMEM;

	_new_mapping_cache = KMEM_CACHE(dm_thin_new_mapping, 0);
	if (!_new_mapping_cache)
		goto bad_new_mapping_cache;

	return 0;

bad_new_mapping_cache:
	dm_unregister_target(&pool_target);
bad_pool_target:
	dm_unregister_target(&thick_target);
bad_thick_target:
	dm_unregister_target(&thin_target);

	return r;
}

static void dm_thin_exit(void)
{
	dm_unregister_target(&thin_target);
	dm_unregister_target(&thick_target);
	dm_unregister_target(&pool_target);

	kmem_cache_destroy(_new_mapping_cache);
}

module_init(dm_thin_init);
module_exit(dm_thin_exit);

MODULE_DESCRIPTION(DM_NAME " thin provisioning target");
MODULE_AUTHOR("Joe Thornber <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
