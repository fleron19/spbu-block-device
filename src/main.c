// SPDX-License-Identifier: GPL-2.0-only
/*
 * ramblock - simple RAM block device
 * Adapted for Linux 6.18.13
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/genhd.h>
#include <linux/vmalloc.h>
#include <linux/blk-mq.h>
#include <linux/errno.h>
#include <linux/slab.h>

#define SECTOR_SHIFT 9
#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#define RAMBLOCK_SIZE_BYTES (4 * 1024 * 1024) /* 4 MiB */
#define RAMBLOCK_SECTORS (RAMBLOCK_SIZE_BYTES / SECTOR_SIZE)
#define RAMBLOCK_NAME "ramblock"
#define RAMBLOCK_MINORS 16

static int major_num;
static sector_t capacity;
static u8 *data;
static struct gendisk *gd;
static struct request_queue *queue;

static const struct block_device_operations ramblock_fops = {
	.owner = THIS_MODULE,
};

static void ramblock_make_request(struct request_queue *q, struct bio *bio)
{
	struct bio_vec bv;
	struct bvec_iter iter;
	sector_t sector = bio->bi_iter.bi_sector;
	unsigned int len;
	unsigned long offset_bytes;
	u8 *dev_addr;

	/* iterate all segments */
	bio_for_each_segment_all(bv, bio, iter) {
		void *kaddr;
		len = bv.bv_len;
		offset_bytes = (sector << SECTOR_SHIFT);

		/* bounds check */
		if (offset_bytes + len > (unsigned long)RAMBLOCK_SIZE_BYTES) {
			pr_err("%s: I/O out of bounds (sector %llu len %u)\n",
			       RAMBLOCK_NAME, (unsigned long long)sector, len);
			bio_endio(bio);
			return;
		}

		/* map page */
		kaddr = kmap_atomic(bv.bv_page);
		if (bio_data_dir(bio) == READ)
			memcpy(kaddr + bv.bv_offset, data + offset_bytes, len);
		else
			memcpy(data + offset_bytes, kaddr + bv.bv_offset, len);
		kunmap_atomic(kaddr);

		sector += len >> SECTOR_SHIFT;
	}

	bio_endio(bio);
}

static int __init ramblock_init(void)
{
	int ret = 0;

	data = vzalloc(RAMBLOCK_SIZE_BYTES);
	if (!data) {
		pr_err("%s: failed to allocate memory\n", RAMBLOCK_NAME);
		return -ENOMEM;
	}

	capacity = RAMBLOCK_SECTORS;

	major_num = register_blkdev(0, RAMBLOCK_NAME);
	if (major_num < 0) {
		pr_err("%s: failed to register block device\n", RAMBLOCK_NAME);
		ret = major_num;
		goto err_vfree;
	}

	queue = blk_alloc_queue(GFP_KERNEL);
	if (!queue) {
		pr_err("%s: failed to allocate request queue\n", RAMBLOCK_NAME);
		ret = -ENOMEM;
		goto err_unregister_blkdev;
	}

	blk_queue_make_request(queue, ramblock_make_request);
	blk_queue_logical_block_size(queue, SECTOR_SIZE);

	gd = alloc_disk(RAMBLOCK_MINORS);
	if (!gd) {
		pr_err("%s: failed to allocate gendisk\n", RAMBLOCK_NAME);
		ret = -ENOMEM;
		goto err_blk_cleanup_queue;
	}

	gd->major = major_num;
	gd->first_minor = 0;
	gd->fops = &ramblock_fops;
	gd->queue = queue;
	set_capacity(gd, capacity);
	snprintf(gd->disk_name, DISK_NAME_LEN, RAMBLOCK_NAME);

	add_disk(gd);

	pr_info("%s: registered with major %d\n", RAMBLOCK_NAME, major_num);
	return 0;

err_put_disk:
	put_disk(gd);
err_blk_cleanup_queue:
	blk_cleanup_queue(queue);
err_unregister_blkdev:
	unregister_blkdev(major_num, RAMBLOCK_NAME);
err_vfree:
	vfree(data);
	return ret;
}

static void __exit ramblock_exit(void)
{
	if (gd) {
		del_gendisk(gd);
		put_disk(gd);
	}
	if (queue)
		blk_cleanup_queue(queue);
	unregister_blkdev(major_num, RAMBLOCK_NAME);
	vfree(data);

	pr_info("%s: unregistered\n", RAMBLOCK_NAME);
}

module_init(ramblock_init);
module_exit(ramblock_exit);

MODULE_AUTHOR("Romanovskiy Mikhail <romanovskymv@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RAM Block device");
