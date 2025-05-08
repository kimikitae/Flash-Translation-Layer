#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include "device.h"
#include "bluedbm.h"
#include "unity.h"

#define BDBM_FILE_NAME NULL

// #define WRITE_PAGE_SIZE(x) (device_get_total_pages(x))
#define WRITE_PAGE_SIZE(x) (device_get_pages_per_segment(x) * 16)

struct device *dev;

static void end_rq(struct device_request *request);

void setUp(void)
{
	printf("set up!\n");
	int ret;
	ret = device_module_init(BLUEDBM_MODULE, &dev, 0);
	TEST_ASSERT_EQUAL_INT(0, ret);
}

void tearDown(void)
{
	TEST_ASSERT_NOT_NULL(dev);
	device_module_exit(dev);
}

void test_open(void)
{
	printf("test open!\n");
	
	int ret;
	ret = dev->d_op->open(dev, BDBM_FILE_NAME, O_CREAT | O_RDWR);
	TEST_ASSERT_EQUAL_INT(0, ret);
	ret = dev->d_op->close(dev);
	TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_full_write(void)
{
	struct timespec begin, end;
	double elapsed_time = 0;

	printf("test full write!\n");
	
	struct device_request request;
	struct device_address addr;
	uint32_t *buffer;
	size_t page_size;
	size_t total_pages;

	TEST_ASSERT_EQUAL_INT(0, dev->d_op->open(dev, BDBM_FILE_NAME,
						 O_CREAT | O_RDWR));
	page_size = device_get_page_size(dev);
	total_pages = WRITE_PAGE_SIZE(dev);

	buffer = (uint32_t *)malloc(page_size);
	TEST_ASSERT_NOT_NULL(buffer);
	memset(buffer, 0, page_size);

	/**< note that all I/O functions run synchronously */
	for (addr.lpn = 0; addr.lpn < total_pages; addr.lpn++) {
		memcpy(buffer, &addr.lpn, sizeof(uint32_t));
		request.paddr = addr;
		request.data_len = page_size;
		request.end_rq = NULL;
		request.flag = DEVICE_WRITE;
		request.sector = 0;
		request.data = buffer;
		
		clock_gettime(CLOCK_MONOTONIC, &begin);
		TEST_ASSERT_EQUAL_INT(request.data_len,
					dev->d_op->write(dev, &request));
		clock_gettime(CLOCK_MONOTONIC, &end);
		elapsed_time += (end.tv_sec - begin.tv_sec) + (end.tv_nsec - begin.tv_nsec) / 1e9;

		if(addr.lpn == 0){
			printf("start address : %d(pages) ~ ", addr.lpn);
		}else if(addr.lpn == total_pages - 1){
			printf("end address : %d(pages)\n", addr.lpn);
		}

		//if(addr.lpn == 100)break;
	}

	printf("1GB write Elapsed Time: %.4f seconds\n", elapsed_time);
	printf("1GB write Bandwidth: %-10.4lf MiB/s\n", (double)(total_pages * page_size) / (elapsed_time * (0x1 << 20)));

	memset(buffer, 0, page_size);

	elapsed_time = 0;

	uint32_t testarr[101] = {0, };

	for (addr.lpn = 0; addr.lpn < total_pages; addr.lpn++) {
		request.paddr = addr;
		request.data_len = page_size;
		request.end_rq = NULL;
		request.flag = DEVICE_READ;
		request.sector = 0;
		request.data = buffer;

		clock_gettime(CLOCK_MONOTONIC, &begin);
		TEST_ASSERT_EQUAL_INT(request.data_len,
				    dev->d_op->read(dev, &request));
		clock_gettime(CLOCK_MONOTONIC, &end);
		elapsed_time += (end.tv_sec - begin.tv_sec) + (end.tv_nsec - begin.tv_nsec) / 1e9;

		//usleep(1000);

		printf("lpn:%d\tdata:%d\n", addr.lpn, *(uint32_t *)request.data);
		//TEST_ASSERT_EQUAL_UINT32(addr.lpn, *(uint32_t *)request.data);
		/*if(addr.lpn == 0){
			printf("start address : %d(pages) ~ ", addr.lpn);
		}else if(addr.lpn == total_pages - 1){
			printf("end address : %d(pages)\n", addr.lpn);
		}*/

		if(addr.lpn == 100)break;
	}

	printf("1GB read Elapsed Time: %.4f seconds\n", elapsed_time);
	printf("1GB read Bandwidth: %-10.4lf MiB/s\n", (double)((addr.lpn + 1) * page_size) / (elapsed_time * (0x1 << 20)));

	TEST_ASSERT_EQUAL_INT(0, dev->d_op->close(dev));
	free(buffer);
}

void test_overwrite(void)
{
	printf("test overwrite!\n");
	
	struct device_request request;
	struct device_address addr;
	char *buffer;
	size_t page_size;
	size_t total_pages;

	TEST_ASSERT_EQUAL_INT(0, dev->d_op->open(dev, BDBM_FILE_NAME,
						 O_CREAT | O_RDWR));
	page_size = device_get_page_size(dev);
	total_pages = WRITE_PAGE_SIZE(dev);
	buffer = (char *)malloc(page_size);
	TEST_ASSERT_NOT_NULL(buffer);
	memset(buffer, 0, page_size);

	/**< note that all I/O functions run synchronously */
	for (addr.lpn = 0; addr.lpn < total_pages; addr.lpn++) {
		memcpy(buffer, &addr.lpn, sizeof(uint32_t));
		request.paddr = addr;
		request.data_len = page_size;
		request.end_rq = NULL;
		request.flag = DEVICE_WRITE;
		request.sector = 0;
		request.data = buffer;
		TEST_ASSERT_EQUAL_INT(request.data_len,
				      dev->d_op->write(dev, &request));
	}

	for (addr.lpn = 0; addr.lpn < total_pages; addr.lpn++) {
		request.paddr = addr;
		request.data_len = page_size;
		request.end_rq = NULL;
		request.flag = DEVICE_WRITE;
		request.sector = 0;
		request.data = buffer;
		TEST_ASSERT_EQUAL_INT(-EINVAL, dev->d_op->write(dev, &request));
	}

	TEST_ASSERT_EQUAL_INT(0, dev->d_op->close(dev));
	free(buffer);
}

void test_erase(void)
{
	printf("test erase!\n");

	struct device_request request;
	struct device_address addr;
	char *buffer;
	size_t page_size;
	size_t total_pages;
	size_t nr_segments;
	size_t nr_pages_per_segment;
	size_t segnum;

	TEST_ASSERT_EQUAL_INT(0, dev->d_op->open(dev, BDBM_FILE_NAME,
						 O_CREAT | O_RDWR));
	page_size = device_get_page_size(dev);
	total_pages = WRITE_PAGE_SIZE(dev);
	buffer = (char *)malloc(page_size);
	TEST_ASSERT_NOT_NULL(buffer);
	memset(buffer, 0, page_size);

	/**< note that all I/O functions run synchronously */
	for (addr.lpn = 0; addr.lpn < total_pages; addr.lpn++) {
		memcpy(buffer, &addr.lpn, sizeof(uint32_t));
		request.paddr = addr;
		request.data_len = page_size;
		request.end_rq = NULL;
		request.flag = DEVICE_WRITE;
		request.sector = 0;
		request.data = buffer;
		TEST_ASSERT_EQUAL_INT(request.data_len,
				      dev->d_op->write(dev, &request));
	}

	nr_segments = WRITE_PAGE_SIZE(dev) / device_get_pages_per_segment(dev);
	for (segnum = 0; segnum < nr_segments - 1; segnum++) {
		addr.lpn = 0;
		addr.format.block = segnum;
		request.paddr = addr;
		request.flag = DEVICE_ERASE;
		request.end_rq = NULL;
		TEST_ASSERT_EQUAL_INT(0, dev->d_op->erase(dev, &request));
	};

	nr_pages_per_segment = device_get_pages_per_segment(dev);
	for (addr.lpn = 0; addr.lpn < total_pages - nr_pages_per_segment;
	     addr.lpn++) {
		memcpy(buffer, &addr.lpn, sizeof(uint32_t));
		request.paddr = addr;
		request.data_len = page_size;
		request.end_rq = NULL;
		request.flag = DEVICE_WRITE;
		request.sector = 0;
		request.data = buffer;
		TEST_ASSERT_EQUAL_INT(request.data_len,
				      dev->d_op->write(dev, &request));
	}

	for (; addr.lpn < total_pages; addr.lpn++) {
		memcpy(buffer, &addr.lpn, sizeof(uint32_t));
		request.paddr = addr;
		request.data_len = page_size;
		request.end_rq = NULL;
		request.flag = DEVICE_WRITE;
		request.sector = 0;
		request.data = buffer;
		TEST_ASSERT_EQUAL_INT(-EINVAL, dev->d_op->write(dev, &request));
	}

	TEST_ASSERT_EQUAL_INT(0, dev->d_op->close(dev));
	free(buffer);
}

static void end_rq(struct device_request *request)
{
	struct device_address paddr = request->paddr;
	uint8_t *is_check = (uint8_t *)request->rq_private;
	is_check[paddr.lpn] = 1;
}

void test_end_rq_works(void)
{
	struct device_request request;
	struct device_address addr;
	char *buffer;
	uint8_t *is_check;
	size_t page_size;
	size_t total_pages;
	size_t segnum;
	size_t nr_segments;

	TEST_ASSERT_EQUAL_INT(0, dev->d_op->open(dev, BDBM_FILE_NAME,
						 O_CREAT | O_RDWR));
	page_size = device_get_page_size(dev);
	total_pages = WRITE_PAGE_SIZE(dev);
	nr_segments = WRITE_PAGE_SIZE(dev) / device_get_pages_per_segment(dev);

	buffer = (char *)malloc(page_size);
	TEST_ASSERT_NOT_NULL(buffer);
	memset(buffer, 0, page_size);

	is_check = (uint8_t *)malloc(total_pages);
	TEST_ASSERT_NOT_NULL(is_check);
	memset(is_check, 0, total_pages);

	/**< note that all I/O functions run synchronously */
	for (addr.lpn = 0; addr.lpn < total_pages; addr.lpn++) {
		memcpy(buffer, &addr.lpn, sizeof(uint32_t));
		request.paddr = addr;
		request.data_len = page_size;
		request.end_rq = end_rq;
		request.flag = DEVICE_WRITE;
		request.sector = 0;
		request.data = buffer;
		request.rq_private = (void *)is_check;
		TEST_ASSERT_EQUAL_INT(request.data_len,
				      dev->d_op->write(dev, &request));
	}

	for (addr.lpn = 0; addr.lpn < total_pages; addr.lpn++) {
		TEST_ASSERT_EQUAL_INT(1, is_check[addr.lpn]);
	}

	memset(is_check, 0, total_pages);
	for (addr.lpn = 0; addr.lpn < total_pages; addr.lpn++) {
		request.paddr = addr;
		request.data_len = page_size;
		request.end_rq = end_rq;
		request.flag = DEVICE_READ;
		request.sector = 0;
		request.data = buffer;
		TEST_ASSERT_EQUAL_INT(request.data_len,
				      dev->d_op->read(dev, &request));
		TEST_ASSERT_EQUAL_UINT32(addr.lpn, *(uint32_t *)request.data);
	}

	for (addr.lpn = 0; addr.lpn < total_pages; addr.lpn++) {
		TEST_ASSERT_EQUAL_INT(1, is_check[addr.lpn]);
	}

	memset(is_check, 0, total_pages);
	for (segnum = 0; segnum < nr_segments; segnum++) {
		addr.lpn = 0;
		addr.format.block = segnum;
		request.paddr = addr;
		request.flag = DEVICE_ERASE;
		request.end_rq = end_rq;
		TEST_ASSERT_EQUAL_INT(0, dev->d_op->erase(dev, &request));
	};

	for (segnum = 0; segnum < nr_segments; segnum++) {
		addr.lpn = 0;
		addr.format.block = segnum;
		TEST_ASSERT_EQUAL_INT(1, is_check[addr.lpn]);
	}

	TEST_ASSERT_EQUAL_INT(0, dev->d_op->close(dev));
	free(buffer);
	free(is_check);
}

int main(void)
{
	UNITY_BEGIN();
	//setUp();
	//RUN_TEST(test_open);
	RUN_TEST(test_full_write);
	//RUN_TEST(test_overwrite);
	//RUN_TEST(test_erase);
	//RUN_TEST(test_end_rq_works);
	return UNITY_END();
}
