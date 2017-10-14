#include <linux/device.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include <asm/page.h>

#include "al_patches_main.h"

static ssize_t patch_029_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *page)
{
	return snprintf(page, PAGE_SIZE, "AL ETH: [bug fix] store ethtool parameters and restore them upon SFP disconnect/connect and if down/up.\n");
}

static struct kobj_attribute patch_029_attr =
	__ATTR(patch_029, S_IRUGO, patch_029_show, NULL);

static int __init al_patch_029(void)
{
	al_patches_add(&patch_029_attr.attr);

	return 0;

}

__initcall(al_patch_029);

