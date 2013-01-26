#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "vfs_pc.h"

#define FD(fp) ((int)(uintptr_t)(fp->ctx))

static int8_t
pc_open(struct file *fp, const char *path, uint8_t access)
{
	int fd;
	int acc = O_RDONLY;
	mode_t mode = 0644;

	if (access == VFS_RW) {
		acc = O_CREAT | O_RDWR;
	}
	
	fd = open(path, acc, mode);
	if (fd < 0)
		return -1;

	fp->ctx = (void*)(uintptr_t)fd;

	return 0;
}

static void
pc_close(struct file *fp)
{
	close(FD(fp));
}

static int8_t
pc_read(struct file *fp, void *buf, uint16_t index)
{
	ssize_t rv;
	lseek(FD(fp), VFS_SECT_TO_OFFSET(index), SEEK_SET);
	rv = read(FD(fp), buf, VFS_SECT_SIZE);
	if (rv == 0) {
		memset(buf, 0, VFS_SECT_SIZE);
		return 0;
	} else if (rv > 0)
		return 0;
	return -1;
}

static int8_t
pc_write(struct file *fp, const void *buf, uint16_t index)
{
	lseek(FD(fp), VFS_SECT_TO_OFFSET(index), SEEK_SET);
	return write(FD(fp), buf, VFS_SECT_SIZE) == VFS_SECT_SIZE? 0 : -1;
}

struct vfs pc_vfs = {
	.name = "big pc",
	.vopen = pc_open,
	.vclose = pc_close,
	.vread_sector = pc_read,
	.vwrite_sector = pc_write,
};
