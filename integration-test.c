/**
 * @file main.c
 * @brief main program for test the interface
 * @author Gijun Oh
 * @version 0.2
 * @date 2021-09-22
 */
#include <assert.h>
#include <climits>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>

#include "module.h"
#include "flash.h"
#include "page.h"
#include "log.h"
#include "device.h"

#define USE_FORCE_ERASE
#define USE_RANDOM_WAIT
// #define USE_DEBUG_PRINT

// #define BLOCKSIZE_1MB
#define BLOCKSIZE_4KB

#define DEVICE_PATH "/dev/nvme0n2"
#define WRITE_SIZE ((size_t)128 * (size_t)(1 << 20)) // 128 MiB
#define NR_ERASE (10)
#if defined(BLOCKSIZE_4KB)
#define BLOCK_SIZE ((size_t)4 * (size_t)(1 << 10)) // 4 KiB
#elif defined(BLOCKSIZE_1MB)
#define BLOCK_SIZE ((size_t)(1 << 20)) // 1 MiB
#endif

int is_check[WRITE_SIZE / BLOCK_SIZE];

void *read_thread(void *data)
{
	ssize_t buffer[BLOCK_SIZE / sizeof(size_t)];
	struct flash_device *flash;
	off_t offset;

	offset = 0;
	flash = (struct flash_device *)data;

	while (offset < (off_t)WRITE_SIZE) {
		ssize_t ret;
		srand(((unsigned int)time(NULL) * (unsigned int)offset) %
		      UINT_MAX);
		memset(buffer, 0, sizeof(buffer));
		ret = flash->f_op->read(flash, (void *)buffer, BLOCK_SIZE,
					offset);
		if (ret < 0 || (offset > 0 && buffer[0] == 0)) {
			continue;
		}
		if (buffer[0] == -1) {
			continue;
		}
		assert(ret == BLOCK_SIZE);
#ifdef USE_DEBUG_PRINT
		printf("%-12s: %-16zd(offset: %zu)\n", "read", buffer[0],
		       offset);
#endif
		is_check[(size_t)(*(ssize_t *)buffer) / BLOCK_SIZE] = 1;
		offset += BLOCK_SIZE;
#ifdef USE_RANDOM_WAIT
		usleep((rand() % 500) + 1000);
#endif
	}
	return NULL;
}

void *write_thread(void *data)
{
	ssize_t buffer[BLOCK_SIZE / sizeof(ssize_t)];
	struct flash_device *flash;
	off_t offset;

	offset = 0;
	flash = (struct flash_device *)data;

	while (offset < (off_t)WRITE_SIZE) {
		ssize_t ret;
		srand((((unsigned int)time(NULL) * (unsigned int)offset) + 1) %
		      UINT_MAX);
		memset(buffer, 0, sizeof(buffer));
		buffer[0] = (ssize_t)offset;
		ret = flash->f_op->write(flash, (void *)buffer, BLOCK_SIZE,
					 offset);
		if (ret < 0) {
			pr_err("write failed (offset: %zu)\n", (size_t)offset);
		}
#ifdef USE_DEBUG_PRINT
		printf("%-12s: %-16zd(offset: %zu)\n", "write", buffer[0],
		       (size_t)offset);
#endif
		assert(ret == BLOCK_SIZE);
		offset += BLOCK_SIZE;
#ifdef USE_RANDOM_WAIT
		usleep((rand() % 500) + 100);
#endif
	}
	return NULL;
}

gint is_overwrite = 0;

void *overwrite_thread(void *data)
{
	ssize_t buffer[BLOCK_SIZE / sizeof(ssize_t)];
	struct flash_device *flash;
	off_t offset;

	offset = 0;
	flash = (struct flash_device *)data;

	g_atomic_int_set(&is_overwrite, 1);

	sleep(2);
	while (offset < (off_t)WRITE_SIZE) {
		ssize_t ret;
		srand((((unsigned int)time(NULL) * (unsigned int)offset) + 2) %
		      UINT_MAX);
		memset(buffer, 0, sizeof(buffer));
		buffer[0] = (ssize_t)offset;
		ret = flash->f_op->write(flash, (void *)buffer, BLOCK_SIZE,
					 offset);
		if (ret < 0) {
			pr_err("overwrite failed (offset: %zu)\n",
			       (size_t)offset);
		}
#ifdef USE_DEBUG_PRINT
		printf("%-12s: %-16zd(offset: %zu)\n", "overwrite", buffer[0],
		       (size_t)offset);
#endif
		offset += BLOCK_SIZE;
#ifdef USE_RANDOM_WAIT
		usleep((rand() % 500) + 100);
#endif
	}
	return NULL;
}

void *erase_thread(void *data)
{
#ifdef USE_FORCE_ERASE
	struct flash_device *flash;
	int i;
	flash = (struct flash_device *)data;
	while (!g_atomic_int_get(&is_overwrite)) {
		usleep(100);
	}
	for (i = 0; i < NR_ERASE; i++) {
		usleep(1000 * 1000);
		flash->f_op->ioctl(flash, PAGE_FTL_IOCTL_TRIM);
#ifdef USE_DEBUG_PRINT
		printf("\tforced garbage collection!\n");
#endif
	}
#endif
	(void)data;

	return NULL;
}

int main(void)
{
	pthread_t threads[6]; // write, write, read, read, overwrite, erase;
	int thread_id;
	int is_all_valid;
	size_t status;
	size_t i;
	struct flash_device *flash = NULL;

	memset(is_check, 0, sizeof(is_check));
#if defined(DEVICE_USE_ZONED)
	assert(0 == module_init(PAGE_FTL_MODULE, &flash, ZONE_MODULE));
#elif defined(DEVICE_USE_BLUEDBM)
	assert(0 == module_init(PAGE_FTL_MODULE, &flash, BLUEDBM_MODULE));
#else
	assert(0 == module_init(PAGE_FTL_MODULE, &flash, RAMDISK_MODULE));
#endif
	assert(0 == flash->f_op->open(flash, DEVICE_PATH, O_CREAT | O_RDWR));
	thread_id =
		pthread_create(&threads[0], NULL, write_thread, (void *)flash);
	if (thread_id < 0) {
		perror("thread create failed");
		exit(errno);
	}
	thread_id =
		pthread_create(&threads[1], NULL, write_thread, (void *)flash);
	if (thread_id < 0) {
		perror("thread create failed");
		exit(errno);
	}
	thread_id =
		pthread_create(&threads[2], NULL, read_thread, (void *)flash);
	if (thread_id < 0) {
		perror("thread create failed");
		exit(errno);
	}
	thread_id =
		pthread_create(&threads[3], NULL, read_thread, (void *)flash);
	if (thread_id < 0) {
		perror("thread create failed");
		exit(errno);
	}
	thread_id = pthread_create(&threads[4], NULL, overwrite_thread,
				   (void *)flash);
	if (thread_id < 0) {
		perror("thread create failed");
		exit(errno);
	}
	thread_id =
		pthread_create(&threads[5], NULL, erase_thread, (void *)flash);
	if (thread_id < 0) {
		perror("thread create failed");
		exit(errno);
	}

	pthread_join(threads[0], (void **)&status);
	pthread_join(threads[1], (void **)&status);
	pthread_join(threads[2], (void **)&status);
	pthread_join(threads[3], (void **)&status);
	pthread_join(threads[4], (void **)&status);
	pthread_join(threads[5], (void **)&status);

	flash->f_op->close(flash);
	assert(0 == module_exit(flash));

	is_all_valid = 1;
	for (i = 0; i < sizeof(is_check) / sizeof(int); i++) {
		if (!is_check[i]) {
			printf("read failed offset: %-20zu\n",
			       (i * BLOCK_SIZE));
			is_all_valid = 0;
		}
	}
	if (is_all_valid) {
		printf("all data exist => FINISH\n");
	}

	return 0;
}
