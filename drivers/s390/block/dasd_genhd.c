/*
 * File...........: linux/drivers/s390/block/dasd_genhd.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999-2001
 *
 * Dealing with devices registered to multiple major numbers.
 *
 * $Revision: 1.38 $
 */

#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/blkpg.h>

#include <asm/uaccess.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_gendisk:"

#include "dasd_int.h"

static spinlock_t dasd_major_lock = SPIN_LOCK_UNLOCKED;
static struct list_head dasd_major_info = LIST_HEAD_INIT(dasd_major_info);

struct major_info {
	struct list_head list;
	int major;
};

/*
 * Register major number for the dasd driver. Call with DASD_MAJOR to
 * setup the static dasd device major 94 or with 0 to allocated a major
 * dynamically.
 */
static int
dasd_register_major(int major)
{
	struct major_info *mi;
	int new_major;

	/* Allocate major info structure. */
	mi = kmalloc(sizeof(struct major_info), GFP_KERNEL);

	/* Check if one of the allocations failed. */
	if (mi == NULL) {
		MESSAGE(KERN_WARNING, "%s",
			"Cannot get memory to allocate another "
			"major number");
		return -ENOMEM;
	}

	/* Register block device. */
	new_major = register_blkdev(major, "dasd");
	if (new_major < 0) {
		kfree(mi);
		return new_major;
	}
	if (major != 0)
		new_major = major;

	/* Initialize major info structure. */
	mi->major = new_major;

	/* Insert the new major info structure into dasd_major_info list. */
	spin_lock(&dasd_major_lock);
	list_add_tail(&mi->list, &dasd_major_info);
	spin_unlock(&dasd_major_lock);

	return 0;
}

static void
dasd_unregister_major(struct major_info * mi)
{
	int rc;

	if (mi == NULL)
		return;

	/* Delete the major info from dasd_major_info. */
	spin_lock(&dasd_major_lock);
	list_del(&mi->list);
	spin_unlock(&dasd_major_lock);

	rc = unregister_blkdev(mi->major, "dasd");
	if (rc < 0)
		MESSAGE(KERN_WARNING,
			"Cannot unregister from major no %d, rc = %d",
			mi->major, rc);

	/* Free memory. */
	kfree(mi);
}

/*
 * Allocate and register gendisk structure for device.
 */
int
dasd_gendisk_alloc(struct dasd_device *device)
{
	struct major_info *mi;
	struct gendisk *gdp;
	int index, len, rc;

	/* Make sure the major for this device exists. */
	mi = NULL;
	while (1) {
		spin_lock(&dasd_major_lock);
		index = device->devindex;
		list_for_each_entry(mi, &dasd_major_info, list) {
			if (index < DASD_PER_MAJOR)
				break;
			index -= DASD_PER_MAJOR;
		}
		spin_unlock(&dasd_major_lock);
		if (index < DASD_PER_MAJOR)
			break;
		rc = dasd_register_major(0);
		if (rc) {
			DBF_EXC(DBF_ALERT, "%s", "out of major numbers!");
			return rc;
		}
	}
	
	gdp = alloc_disk(1 << DASD_PARTN_BITS);
	if (!gdp)
		return -ENOMEM;

	/* Initialize gendisk structure. */
	gdp->major = mi->major;
	gdp->first_minor = index << DASD_PARTN_BITS;
	gdp->fops = &dasd_device_operations;
	gdp->driverfs_dev = &device->cdev->dev;

	/*
	 * Set device name.
	 *   dasda - dasdz : 26 devices
	 *   dasdaa - dasdzz : 676 devices, added up = 702
	 *   dasdaaa - dasdzzz : 17576 devices, added up = 18278
	 */
	len = sprintf(gdp->disk_name, "dasd");
	if (device->devindex > 25) {
		if (device->devindex > 701)
			len += sprintf(gdp->disk_name + len, "%c",
				       'a'+(((device->devindex-702)/676)%26));
		len += sprintf(gdp->disk_name + len, "%c",
			       'a'+(((device->devindex-26)/26)%26));
	}
	len += sprintf(gdp->disk_name + len, "%c", 'a'+(device->devindex%26));

 	sprintf(gdp->devfs_name, "dasd/%04x",
		_ccw_device_get_device_number(device->cdev));

	if (device->ro_flag)
		set_disk_ro(gdp, 1);
	gdp->private_data = device;
	gdp->queue = device->request_queue;
	device->gdp = gdp;
	set_capacity(device->gdp, 0);
	add_disk(device->gdp);
	return 0;
}

/*
 * Unregister and free gendisk structure for device.
 */
void
dasd_gendisk_free(struct dasd_device *device)
{
	del_gendisk(device->gdp);
	put_disk(device->gdp);
	device->gdp = 0;
}

/*
 * Trigger a partition detection.
 */
void
dasd_scan_partitions(struct dasd_device * device)
{
	struct block_device *bdev;

	/* Make the disk known. */
	set_capacity(device->gdp, device->blocks << device->s2b_shift);
	/* See fs/partition/check.c:register_disk,rescan_partitions */
	bdev = bdget_disk(device->gdp, 0);
	if (bdev) {
		if (blkdev_get(bdev, FMODE_READ, 1, BDEV_RAW) >= 0) {
			/* Can't call rescan_partitions directly. Use ioctl. */
			ioctl_by_bdev(bdev, BLKRRPART, 0);
			blkdev_put(bdev, BDEV_RAW);
		}
	}
}

/*
 * Remove all inodes in the system for a device, delete the
 * partitions and make device unusable by setting its size to zero.
 */
void
dasd_destroy_partitions(struct dasd_device * device)
{
	int p;

	for (p = device->gdp->minors - 1; p > 0; p--) {
		invalidate_partition(device->gdp, p);
		delete_partition(device->gdp, p);
	}
	invalidate_partition(device->gdp, 0);
	set_capacity(device->gdp, 0);
}

int
dasd_gendisk_init(void)
{
	int rc;

	/* Register to static dasd major 94 */
	rc = dasd_register_major(DASD_MAJOR);
	if (rc != 0) {
		MESSAGE(KERN_WARNING,
			"Couldn't register successfully to "
			"major no %d", DASD_MAJOR);
		return rc;
	}
	return 0;
}

void
dasd_gendisk_exit(void)
{
	struct list_head *l, *n;

	spin_lock(&dasd_major_lock);
	list_for_each_safe(l, n, &dasd_major_info)
		dasd_unregister_major(list_entry(l, struct major_info, list));
	spin_unlock(&dasd_major_lock);
}
