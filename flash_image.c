/*
 * Copyright 2014 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <bootimg.h>
#include "util.h"
#include "flash_image.h"
#include "gpt/partlink/partlink.h"
#include "update_osip.h"

int full_gpt(void)
{
	struct stat buf;

	return (stat(BASE_PLATFORM_INTEL_LABEL"/fastboot", &buf) == 0
		&& S_ISBLK(buf.st_mode));
}

char *get_gpt_path(const char *name)
{
	char *block_dev = malloc(BUFSIZ);
	char base[] = BASE_PLATFORM_INTEL_LABEL"/";
	struct stat buf;

	if (!name) {
		error("%s: Passed name is empty.", __func__);
		goto error;
	}

	if (strlen(name) > BUFSIZ - sizeof(base)) {
		error("%s: Buffer is not large enough to build block device path.", __func__);
		goto error;
	}

	if (!block_dev) {
		error("%s: Failed to allocate mem for block dev.", __func__);
		goto error;
	}

	strncpy(block_dev, base, sizeof(base));
	strncpy(block_dev + sizeof(base) - 1, name, strlen(name) + 1);

	if (stat(block_dev, &buf) != 0 || !S_ISBLK(buf.st_mode))
		goto error;

	return block_dev;
error:
	if (block_dev)
		free(block_dev);
	return NULL;
}

int flash_image(void *data, unsigned sz, const char *name)
{
	if (full_gpt()) {
		char *block_dev;
		int ret;

		block_dev = get_gpt_path(name);
		if (!block_dev)
			return -1;

		ret = file_write(block_dev, data, sz);
		free(block_dev);
		return ret;
	} else {
		int index = get_named_osii_index(name);

		if (index < 0) {
			error("Can't find OSII index!!");
			return -1;
		}

		return write_stitch_image(data, sz, index);
	}
}

static int pages(struct boot_img_hdr *hdr, int blob_size)
{
        return (blob_size + hdr->page_size - 1) / hdr->page_size;
}

static size_t bootimage_size(struct boot_img_hdr *hdr)
{
        size_t size;

        size = (1 + pages(hdr, hdr->kernel_size) +
                pages(hdr, hdr->ramdisk_size) +
                pages(hdr, hdr->second_size) +
		pages(hdr, hdr->sig_size)) *
                        hdr->page_size;
        return size;
}

static int read_image_full_gpt(const char *name, void **data)
{
	size_t size;
	struct boot_img_hdr hdr;
	char *block_dev;
	int ret = -1;
	int fd;

	block_dev = get_gpt_path(name);
	if (!block_dev)
		goto out;

	fd = open(block_dev, O_RDONLY);
	if (fd < 0) {
		error("Failed to open %s: %s", block_dev, strerror(errno));
		goto free_block_dev;
	}

	if (safe_read(fd, &hdr, sizeof(hdr))) {
		error("Failed to header of %s: %s", block_dev, strerror(errno));
		goto close;
	}

	if (memcmp(hdr.magic, BOOT_MAGIC, sizeof(hdr.magic))) {
		error("Corrupted image");
		goto close;
	}

	size = bootimage_size(&hdr);

	*data = malloc(size);
	if (!data) {
		error("Memory allocation failure");
		goto close;
	}
	memcpy(*data, &hdr, sizeof(hdr));
	ret = safe_read(fd, (char *)*data + sizeof(hdr), size - sizeof(hdr));
	if (ret)
		free(*data);
	else
		ret = size;
close:
	close(fd);
free_block_dev:
	free(block_dev);
out:
	return ret;
}

int read_image(const char *name, void **data)
{
	size_t size;
	if (full_gpt()) {
		return read_image_full_gpt(name, data);
	} else {
		int index;
		index = get_named_osii_index(name);
		if (index < 0) {
			error("Can't find image %s in the OSIP", name);
			return -1;
		}

		if (read_osimage_data(data, &size, index)) {
			error("Failed to read OSIP entry");
			return -1;
		}
	}
	return size;
}

int flash_android_kernel(void *data, unsigned sz)
{
	return flash_image(data, sz, ANDROID_OS_NAME);
}

int flash_recovery_kernel(void *data, unsigned sz)
{
	return flash_image(data, sz, RECOVERY_OS_NAME);
}

int flash_fastboot_kernel(void *data, unsigned sz)
{
	return flash_image(data, sz, FASTBOOT_OS_NAME);
}

int flash_splashscreen_image(void *data, unsigned sz)
{
	return flash_image(data, sz, SPLASHSCREEN_NAME);
}

int flash_esp(void *data, unsigned sz)
{
	return flash_image(data, sz, ESP_PART_NAME);
}
