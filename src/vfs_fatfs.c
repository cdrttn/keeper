/* 
 * This file is part of keeper.
 * 
 * Copyright 2013, cdavis
 *  
 * keeper is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * keeper is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with keeper.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "keeper.h"

#define FP(fp) ((FIL *)((fp)->ctx))

static int8_t
fatfs_open(struct file *fp, const char *path, uint8_t access)
{
	FIL *f;
	BYTE acc = FA_READ | FA_OPEN_EXISTING;
	FRESULT rv;

	if (access == VFS_RW) {
		acc = FA_READ | FA_WRITE | FA_OPEN_ALWAYS;
	}

	f = fast_mallocz(sizeof(*f));
	if (f == NULL)
		return -1;
	
	rv = f_open(f, path, acc);
	if (rv != FR_OK) {
		fast_free(f);
		return -1;
	}

	fp->ctx = f;

	return 0;
}

static void
fatfs_close(struct file *fp)
{
	f_close(FP(fp));
	fast_free(FP(fp));
	fp->ctx = NULL;
}

static int8_t
fatfs_read(struct file *fp, void *buf, uint16_t index)
{
	FRESULT rv;
	UINT len;

	rv = f_lseek(FP(fp), VFS_SECT_TO_OFFSET(index));
	if (rv != FR_OK)
		return -1;
	rv = f_read(FP(fp), buf, VFS_SECT_SIZE, &len);
	if (rv != FR_OK)
		return -1;
	if (len != VFS_SECT_SIZE)
		memset(buf, 0, VFS_SECT_SIZE);

	return 0;
}

static int8_t
fatfs_write(struct file *fp, const void *buf, uint16_t index)
{
	FRESULT rv;
	UINT len;

	rv = f_lseek(FP(fp), VFS_SECT_TO_OFFSET(index));
	if (rv != FR_OK)
		return -1;
	rv = f_write(FP(fp), buf, VFS_SECT_SIZE, &len);
	if (rv != FR_OK || len != VFS_SECT_SIZE)
		return -1;

	return 0;
}

struct vfs fatfs_vfs = {
	.name = "fatfs",
	.vopen = fatfs_open,
	.vclose = fatfs_close,
	.vread_sector = fatfs_read,
	.vwrite_sector = fatfs_write,
};
