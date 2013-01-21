#include "vfs.h"

int8_t
vfs_open(const struct vfs *methods, struct file *fp,
	 const char *path, uint8_t access)
{
	fp->vfs = methods;
	return methods->vopen(fp, path, access);
}

void
vfs_close(struct file *fp)
{
	fp->vfs->vclose(fp);
}

int8_t
vfs_read_sector(struct file *fp, void *buf, size_t amt)
{
	return fp->vfs->vread_sector(fp, buf, amt);
}

int8_t
vfs_write_sector(struct file *fp, const void *buf, size_t amt)
{
	return fp->vfs->vwrite_sector(fp, buf, amt);
}

uint16_t
vfs_start_sector(struct file *fp)
{
	if (fp->vfs->vstart_sector)
		return fp->vfs->vstart_sector(fp);
	return 0;
}

uint16_t
vfs_end_sector(struct file *fp)
{
	if (fp->vfs->vend_sector)
		return fp->vfs->vend_sector(fp);
	return 0xffff;
}
