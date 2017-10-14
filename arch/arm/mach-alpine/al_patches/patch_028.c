#include <linux/device.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include <asm/page.h>

#include "al_patches_main.h"

static ssize_t patch_028_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *page)
{
	return snprintf(page, PAGE_SIZE, "AL ETH: WOL: add support for PHY WOL.\n");
}

static struct kobj_attribute patch_028_attr =
	__ATTR(patch_028, S_IRUGO, patch_028_show, NULL);

static int __init al_patch_028(void)
{
	al_patches_add(&patch_028_attr.attr);

	return 0;

}

__initcall(al_patch_028);

