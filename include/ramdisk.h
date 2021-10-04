/**
 * @file ramdisk.h
 * @brief ramdisk's header file
 * @author Gijun Oh
 * @version 1.0
 * @date 2021-10-03
 */
#ifndef RAMDISK_H
#define RAMDISK_H

#include <stdint.h>
#include <stdlib.h>

#include "device.h"

struct ramdisk {
	size_t size;
	char *buffer;
	uint64_t *is_used;
};

int ramdisk_open(struct device *);
ssize_t ramdisk_write(struct device *, struct device_request *);
ssize_t ramdisk_read(struct device *, struct device_request *);
ssize_t ramdisk_erase(struct device *, struct device_request *);
int ramdisk_close(struct device *);

int ramdisk_device_init(struct device *, uint64_t flags);
int ramdisk_device_exit(struct device *);

#endif