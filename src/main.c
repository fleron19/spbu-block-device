// SPDX-License-Identifier: GPL-2.0-only
/*
 * ramblock - simple RAM block device for Linux 6.18
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/bio.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/ktime.h>

#define SECTOR_SHIFT 9
#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#define RAMBLOCK_SIZE_BYTES (4 * 1024 * 1024) /* 4 MiB */
#define RAMBLOCK_SECTORS (RAMBLOCK_SIZE_BYTES / SECTOR_SIZE)
#define RAMBLOCK_NAME "block-device"
#define QUEUE_DEPTH 128

static sector_t capacity;
static u8 *data;
static struct gendisk *gd;
static struct blk_mq_tag_set tag_set;

static const struct block_device_operations ramblock_fops = {
};

static int ramblock_transfer(struct request *req)
{
	struct bio *bio;
	struct bio_vec bv;
	struct bvec_iter iter;

	if (blk_rq_is_passthrough(req))
		return BLK_STS_IOERR;

	__rq_for_each_bio(bio, req) {
		sector_t sector = bio->bi_iter.bi_sector;

		bio_for_each_segment(bv, bio, iter) {
			void *kaddr;
			unsigned long offset_bytes;
			unsigned int len = bv.bv_len;

			offset_bytes = (sector << SECTOR_SHIFT);

			if (offset_bytes + bv.bv_offset + len > (unsigned long)RAMBLOCK_SIZE_BYTES) {
				pr_err("%s: I/O out of bounds\n", RAMBLOCK_NAME);
				return BLK_STS_IOERR;
			}

			kaddr = kmap_local_page(bv.bv_page);
			if (bio_data_dir(bio) == READ)
				memcpy(kaddr + bv.bv_offset, data + offset_bytes, len);
			else
				memcpy(data + offset_bytes, kaddr + bv.bv_offset, len);
			kunmap_local(kaddr);

			sector += len >> SECTOR_SHIFT;
		}
	}

	return BLK_STS_OK;
}

static blk_status_t ramblock_queue_rq(struct blk_mq_hw_ctx *hctx,
				      const struct blk_mq_queue_data *bd)
{
	struct request *req = bd->rq;
	blk_status_t st;

	blk_mq_start_request(req);
	st = ramblock_transfer(req);
	blk_mq_end_request(req, st);
	return BLK_STS_OK;
}

static struct blk_mq_ops ramblock_mq_ops = {
	.queue_rq = ramblock_queue_rq,
	.complete = NULL,
};

static int __init ramblock_init(void)
{
	int ret;
	struct queue_limits lim = {
		.logical_block_size = SECTOR_SIZE,
		.physical_block_size = SECTOR_SIZE,
	};

	data = vzalloc(RAMBLOCK_SIZE_BYTES);
	if (!data) {
		pr_err("%s: failed to allocate memory\n", RAMBLOCK_NAME);
		return -ENOMEM;
	}

	capacity = RAMBLOCK_SECTORS;

	memset(&tag_set, 0, sizeof(tag_set));
	tag_set.ops = &ramblock_mq_ops;
	tag_set.nr_hw_queues = 1;
	tag_set.queue_depth = QUEUE_DEPTH;
	tag_set.numa_node = NUMA_NO_NODE;

	ret = blk_mq_alloc_tag_set(&tag_set);
	if (ret) {
		pr_err("%s: blk_mq_alloc_tag_set failed: %d\n", RAMBLOCK_NAME, ret);
		goto err_vfree;
	}

	gd = blk_mq_alloc_disk(&tag_set, &lim, NULL);
	if (IS_ERR(gd)) {
		ret = PTR_ERR(gd);
		pr_err("%s: blk_mq_alloc_disk failed: %d\n", RAMBLOCK_NAME, ret);
		goto err_free_tag_set;
	}

	gd->fops = &ramblock_fops;
	set_capacity(gd, capacity);
	strscpy(gd->disk_name, RAMBLOCK_NAME, sizeof(gd->disk_name));
	ret = add_disk(gd);
	if (ret) {
		pr_err("%s: add_disk failed: %d\n", RAMBLOCK_NAME, ret);
		goto err_put_disk;
	}

	pr_info("%s: registered\n", RAMBLOCK_NAME);
	return 0;

err_put_disk:
	put_disk(gd);
err_free_tag_set:
	blk_mq_free_tag_set(&tag_set);
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
	blk_mq_free_tag_set(&tag_set);
	vfree(data);

	pr_info("%s: unregistered\n", RAMBLOCK_NAME);
}

module_init(ramblock_init);
module_exit(ramblock_exit);

MODULE_AUTHOR("Romanovskiy Mikhail <romanovskymv@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RAM Block device");
