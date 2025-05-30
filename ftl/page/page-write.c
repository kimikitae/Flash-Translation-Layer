/**
 * @file page-write.c
 * @brief write logic for page ftl
 * @author Gijun Oh
 * @version 0.2
 * @date 2021-09-22
 */
#include "module.h"
#include "page.h"
#include "device.h"
#include "log.h"
#include "lru.h"
#include "bits.h"

#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <glib.h>

int uesr_flag;

/**
 * @brief invalidate a segment that including to the given LPN
 *
 * @param pgftl pointer of the page FTL structure
 * @param lpn logical page address to invalidate
 */
static void page_ftl_invalidate(struct page_ftl *pgftl, size_t lpn)
{
	struct page_ftl_segment *segment;
	struct device_address paddr;

	uint32_t segnum;
	size_t nr_valid_pages, nr_free_pages;

	/**< segment information update */
	paddr.lpn = pgftl->trans_map[lpn];
	segnum = paddr.format.block;
	segment = &pgftl->segments[segnum];

	segment->lpn_list =
		g_list_remove(segment->lpn_list, GSIZE_TO_POINTER(lpn));

	nr_valid_pages = g_atomic_int_get(&segment->nr_valid_pages);
	nr_free_pages = g_atomic_int_get(&segment->nr_free_pages);
	g_atomic_int_set(&segment->nr_valid_pages,
			 (unsigned int)(nr_valid_pages - 1));

	/**< global information update */
	pgftl->trans_map[lpn] = PADDR_EMPTY;
	if (nr_free_pages == 0 && get_bit(pgftl->gc_seg_bits, segnum) != 1) {
		pgftl->gc_list = g_list_append(pgftl->gc_list, segment);
		set_bit(pgftl->gc_seg_bits, segnum);
	}
}

/**
 * @brief write's end request function
 *
 * @param request the request which is submitted before
 */
static void page_ftl_write_end_rq(struct device_request *request)
{
	free(request->data);
	device_free_request(request);
}

/**
 * @brief read sequence for overwrite
 *
 * @param pgftl pointer of the page FTL
 * @param lpn logical page address which wants to overwrite
 * @param buffer a pointer to a buffer containing the result of the read
 *
 * @return reading data size. a negative number means fail to read
 *
 * @note
 * NAND-based storage doesn't allow to do overwrite.
 * Therefore, you must use the out-of-place update. So this logic is necessary.
 */
static ssize_t page_ftl_read_for_overwrite(struct page_ftl *pgftl, size_t lpn,
					   void *buffer)
{
	struct device *dev;
	struct device_request *read_rq;

	size_t page_size;
	ssize_t ret;

	dev = pgftl->dev;
	page_size = device_get_page_size(dev);

	read_rq = device_alloc_request(DEVICE_DEFAULT_REQUEST);
	if (read_rq == NULL) {
		pr_err("crete read request failed\n");
		return -ENOMEM;
	}
	read_rq->flag = DEVICE_READ;
	read_rq->sector = lpn * page_size;
	read_rq->data_len = page_size;
	read_rq->data = buffer;
	ret = page_ftl_read(pgftl, read_rq);
	if (ret < 0) {
		pr_err("previous buffer read failed\n");
		return -EFAULT;
	}
	return ret;
}

/**
 * @brief update the translation mapping table
 *
 * @param pgftl pointer of the page FTL
 * @param paddr written device address
 * @param sector logical sector number
 */
static void page_ftl_write_update_metadata(struct page_ftl *pgftl,
					   struct device_address paddr,
					   size_t sector)
{
	struct page_ftl_segment *segment;

	size_t lpn;
	lpn = page_ftl_get_lpn(pgftl, sector);
	if (pgftl->trans_map[lpn] != PADDR_EMPTY) {
		page_ftl_invalidate(pgftl, lpn);
		pr_debug("invalidate address: %zu => %u\n", lpn,
			 pgftl->trans_map[lpn]);
	}
	/**< segment information update */
	segment = &pgftl->segments[paddr.format.block];
	segment->lpn_list =
		g_list_append(segment->lpn_list, GSIZE_TO_POINTER(lpn));

	/**< global information update */
	page_ftl_update_map(pgftl, sector, paddr.lpn);

	pr_debug("new address: %zu => %u (seg: %u)\n", lpn,
		 pgftl->trans_map[lpn], pgftl->trans_map[lpn] >> 13);
	pr_debug("%u/%u(free/valid)\n",
		 g_atomic_int_get(&segment->nr_free_pages),
		 g_atomic_int_get(&segment->nr_valid_pages));
}

/**
 * @brief the core logic for writing the request to the device.
 *
 * @param pgftl pointer of the page FTL structure
 * @param request user's request pointer
 *
 * @return writing data size. a negative number means fail to write.
 */
ssize_t page_ftl_write(struct page_ftl *pgftl, struct device_request *request)
{
	struct device *dev;
	struct device_address paddr;
	char *buffer;
	ssize_t ret;
	size_t page_size;

	size_t lpn, offset;
	size_t nr_entries;

	size_t write_size;
	size_t sector;

	int is_exist;

	dev = pgftl->dev;
	page_size = device_get_page_size(dev);
	write_size = request->data_len;

	sector = request->sector;
	lpn = page_ftl_get_lpn(pgftl, sector);
	offset = page_ftl_get_page_offset(pgftl, sector);

	nr_entries = page_ftl_get_map_size(pgftl) / sizeof(uint32_t);
	if (lpn > nr_entries) {
		pr_err("invalid lpn detected (lpn: %zu, max: %zu)\n", lpn,
		       nr_entries);
		return -EINVAL;
	}

	if (offset + request->data_len > page_size) {
		pr_err("overflow the write data (offset: %zu, length: %zu)\n",
		       offset, request->data_len);
		return -EINVAL;
	}

	pthread_mutex_lock(&pgftl->mutex);
	paddr = page_ftl_get_free_page(pgftl); /**< global data retrieve */
	pthread_mutex_unlock(&pgftl->mutex);
	if (paddr.lpn == PADDR_EMPTY) {
		pr_err("cannot allocate the valid page from device\n");
		return -EFAULT;
	}

	buffer = (char *)malloc(page_size);
	if (buffer == NULL) {
		pr_err("memory allocation failed\n");
		return -ENOMEM;
	}
	memset(buffer, 0, page_size);
	pthread_mutex_lock(&pgftl->mutex);
	is_exist = pgftl->trans_map[lpn] != PADDR_EMPTY;
	pthread_mutex_unlock(&pgftl->mutex);
	if (is_exist && !(offset == 0 || page_size == write_size)) {
	//if (is_exist && write_size < page_size) {
	//if (is_exist) {
		ret = page_ftl_read_for_overwrite(pgftl, lpn, buffer);
		if (ret < 0) {
			pr_err("read failed (lpn:%zu)\n", lpn);
			return ret;
		}
	}
	memcpy(&buffer[offset], request->data, write_size);

	request->flag = DEVICE_WRITE;
	request->data = buffer;
	request->paddr = paddr;
	request->rq_private = (void *)pgftl;
	request->data_len = page_size;
	request->end_rq = page_ftl_write_end_rq;

	/*
	// check whether user write(benchmark.c) or gc write
	if(user_flag){
		user_flag = 0;
		printf("PPN: %016x\t(segnum: %zu)\n", paddr.lpn, paddr.format.block);
	}
	*/
	//printf("[FTL-log] write\tpaddr : %llX\tdata_len : %zubytes \n", request->paddr, request->data_len);
	ret = dev->d_op->write(dev, request);
	if (ret != (ssize_t)device_get_page_size(dev)) {
		pr_err("device write failed (ppn: %u)\n", request->paddr.lpn);
		return ret;
	}

	pthread_mutex_lock(&pgftl->mutex);
	page_ftl_write_update_metadata(pgftl, paddr, sector);
	pthread_mutex_unlock(&pgftl->mutex);

	return write_size;
}
