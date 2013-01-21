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

#endif // _VFS_H_

