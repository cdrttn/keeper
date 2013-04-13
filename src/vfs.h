/* 
 * This file is part of keeper.
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

#ifndef _VFS_H_
#define _VFS_H_

#include <stdint.h>
#include <stdio.h>

#define VFS_RO 0
#define VFS_RW 1
#define VFS_SECT_SIZE 512
#define VFS_SECT_TO_OFFSET(i) (i * VFS_SECT_SIZE)

struct vfs;

struct file {
	const struct vfs *vfs;
	void *ctx;
};

struct vfs {
	const char *name;
	int8_t (*vopen)(struct file *fp, const char *path, uint8_t access);
	void (*vclose)(struct file *fp);
	int8_t (*vread_sector)(struct file *fp, void *buf, uint16_t index);
	int8_t (*vwrite_sector)(struct file *fp, const void *buf, uint16_t index);
	uint16_t (*vstart_sector)(struct file *fp);
	uint16_t (*vend_sector)(struct file *fp);
	//TODO: support enumerating dirs
};

int8_t vfs_open(const struct vfs *methods, struct file *fp, const char *path,
		uint8_t access);
void vfs_close(struct file *fp);
int8_t vfs_read_sector(struct file *fp, void *buf, size_t amt);
int8_t vfs_write_sector(struct file *fp, const void *buf, size_t amt);
uint16_t vfs_start_sector(struct file *fp);
uint16_t vfs_end_sector(struct file *fp);

struct pool;
void test_vfs(const struct vfs *meth, struct pool *p);
void test_vfs_run_all(const struct vfs *meth, struct pool *p);

#endif // _VFS_H_

