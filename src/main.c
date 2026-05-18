// SPDX-License-Identifier: GPL-2.0-only
/*
 * ramblock - simple RAM block device for Linux 6.18.13
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/vmalloc.h>
#include <linux/blk-mq.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/blk_types.h>
#include <linux/string.h>

#define SECTOR_SHIFT 9
#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#define RAMBLOCK_SIZE_BYTES (4 * 1024 * 1024) /* 4 MiB */
#define RAMBLOCK_SECTORS (RAMBLOCK_SIZE_BYTES / SECTOR_SIZE)
#define RAMBLOCK_NAME "ramblock"
#define RAMBLOCK_MINORS 16
#define QUEUE_DEPTH 128

static int major_num;
static sector_t capacity;
static u8 *data;
static struct gendisk *gd;
static struct blk_mq_tag_set tag_set;
static struct request_queue *queue;

static const struct block_device_operations ramblock_fops = {
	.owner = THIS_MODULE,
};

static int ramblock_transfer(struct request *req)
{
	struct bio_vec bv;
	struct bvec_iter iter;
	sector_t sector = blk_rq_pos(req);
	unsigned long offset_bytes;
	unsigned int len;

	if (rq_data_dir(req) != READ && rq_data_dir(req) != WRITE)
		return BLK_STS_IOERR;

	rq_for_each_segment(bv, req, iter) {
		void *kaddr;

		len = bv.bv_len;
		offset_bytes = (sector << SECTOR_SHIFT);

		if (offset_bytes + bv.bv_offset + len > (unsigned long)RAMBLOCK_SIZE_BYTES) {
			pr_err("%s: I/O out of bounds (sector %llu len %u off %u)\n",
			       RAMBLOCK_NAME, (unsigned long long)sector, len, bv.bv_offset);
			return BLK_STS_IOERR;
		}

		kaddr = kmap_local_page(bv.bv_page);
		if (rq_data_dir(req) == READ)
			memcpy(kaddr + bv.bv_offset, data + offset_bytes, len);
		else
			memcpy(data + offset_bytes, kaddr + bv.bv_offset, len);
		kunmap_local(kaddr);

		sector += len >> SECTOR_SHIFT;
	}

	return BLK_STS_OK;
}

static blk_status_t ramblock_queue_rq(struct blk_mq_hw_ctx *hctx,
				      const struct blk_mq_queue_data *bd)
{
	struct request *req = bd->rq;
	blk_status_t st;

	st = ramblock_transfer(req);
	__blk_mq_end_request(req, st);
	return BLK_STS_OK;
}

static struct blk_mq_ops ramblock_mq_ops = {
	.queue_rq = ramblock_queue_rq,
};

static int __init ramblock_init(void)
{
	int ret;

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

	memset(&tag_set, 0, sizeof(tag_set));
	tag_set.ops = &ramblock_mq_ops;
	tag_set.nr_hw_queues = 1;
	tag_set.queue_depth = QUEUE_DEPTH;
	tag_set.numa_node = NUMA_NO_NODE;
	tag_set.cmd_size = 0;
	tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	tag_set.driver_data = NULL;

	ret = blk_mq_alloc_tag_set(&tag_set);
	if (ret) {
		pr_err("%s: blk_mq_alloc_tag_set failed: %d\n", RAMBLOCK_NAME, ret);
		goto err_unregister_blkdev;
	}

	queue = blk_mq_init_queue(&tag_set);
	if (IS_ERR(queue)) {
		ret = PTR_ERR(queue);
		pr_err("%s: blk_mq_init_queue failed: %d\n", RAMBLOCK_NAME, ret);
		goto err_free_tag_set;
	}

	blk_queue_logical_block_size(queue, SECTOR_SIZE);

	gd = alloc_disk(RAMBLOCK_MINORS);
	if (!gd) {
		pr_err("%s: alloc_disk failed\n", RAMBLOCK_NAME);
		ret = -ENOMEM;
		goto err_cleanup_queue;
	}

	gd->major = major_num;
	gd->first_minor = 0;
	gd->fops = &ramblock_fops;
	gd->queue = queue;
	set_capacity(gd, capacity);
	strlcpy(gd->disk_name, RAMBLOCK_NAME, DISK_NAME_LEN);

	add_disk(gd);

	pr_info("%s: registered with major %d\n", RAMBLOCK_NAME, major_num);
	return 0;

err_cleanup_queue:
	blk_cleanup_queue(queue);
err_free_tag_set:
	blk_mq_free_tag_set(&tag_set);
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
	blk_mq_free_tag_set(&tag_set);
	unregister_blkdev(major_num, RAMBLOCK_NAME);
	vfree(data);

	pr_info("%s: unregistered\n", RAMBLOCK_NAME);
}

module_init(ramblock_init);
module_exit(ramblock_exit);

MODULE_AUTHOR("Romanovskiy Mikhail <romanovskymv@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RAM Block device");
