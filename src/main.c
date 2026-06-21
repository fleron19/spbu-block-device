// SPDX-License-Identifier: GPL-2.0-only
/*
 * ramblock - simple RAM block device for Linux 6.18
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/ktime.h>

#define RAMBLOCK_SIZE_BYTES (4 * 1024 * 1024) /* 4 MiB */
#define RAMBLOCK_SECTORS (RAMBLOCK_SIZE_BYTES / SECTOR_SIZE)
#define RAMBLOCK_NAME "block-device"
sector_t capacity;
u8 *data;
struct gendisk *gd;

static void ramblock_submit_bio(struct bio *bio);

static const struct block_device_operations ramblock_fops = {
	.submit_bio = ramblock_submit_bio,
};

static inline sector_t bytes_to_sectors(loff_t bytes)
{
	return bytes >> SECTOR_SHIFT;
}

static inline loff_t sector_to_bytes(sector_t sector)
{
	return sector << SECTOR_SHIFT;
}

static blk_status_t ramblock_transfer(struct bio *bio)
{
	struct bio_vec bv;
	struct bvec_iter iter;
	sector_t sector = bio->bi_iter.bi_sector;

	for_each_bvec(bv, bio->bi_io_vec, iter, bio->bi_iter) {
		void *kaddr;
		loff_t pos = sector_to_bytes(sector);
		unsigned int len = bv.bv_len;

		if (pos + bv.bv_offset + len > RAMBLOCK_SIZE_BYTES) {
			pr_err("%s: I/O out of bounds\n", RAMBLOCK_NAME);
			return BLK_STS_IOERR;
		}

		kaddr = kmap_local_page(bv.bv_page);
		if (bio_data_dir(bio) == READ)
			memcpy(kaddr + bv.bv_offset, data + pos, len);
		else
			memcpy(data + pos, kaddr + bv.bv_offset, len);
		kunmap_local(kaddr);

		sector += bytes_to_sectors(len);
	}

	return BLK_STS_OK;
}

static void ramblock_submit_bio(struct bio *bio)
{
	blk_status_t err = ramblock_transfer(bio);

	bio->bi_status = err;
	bio_endio(bio);
}

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

	gd = blk_alloc_disk(&lim, NUMA_NO_NODE);
	if (IS_ERR(gd)) {
		ret = PTR_ERR(gd);
		pr_err("%s: blk_alloc_disk failed: %d\n", RAMBLOCK_NAME, ret);
		goto err_vfree;
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
	vfree(data);

	pr_info("%s: unregistered\n", RAMBLOCK_NAME);
}

module_init(ramblock_init);
module_exit(ramblock_exit);

MODULE_AUTHOR("Romanovskiy Mikhail <romanovskymv@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RAM Block device");
