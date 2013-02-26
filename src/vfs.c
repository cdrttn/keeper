#include <string.h>
#include "keeper.h"

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

#define TEST_FN "vfsfile.dat"

static uint8_t
test_sector_contains(const uint8_t *buf, char c)
{
	uint16_t i;

	for (i = 0; i < 512; ++i) {
		if (buf[i] != (uint8_t)c)
			return 0;
	}

	return 1;
}

void
test_vfs(const struct vfs *meth, struct pool *p)
{
	uint8_t *buf;
	struct file f;

	buf = pool_allocate_block(p, NULL);
	v_assert(buf != NULL);

	v_assert(vfs_open(meth, &f, TEST_FN, VFS_RW) == 0);
	memset(buf, 'B', 512);
	v_assert(vfs_write_sector(&f, buf, 1) == 0);
	memset(buf, 'A', 512);
	v_assert(vfs_write_sector(&f, buf, 0) == 0);
	vfs_close(&f);
	
	v_assert(vfs_open(meth, &f, TEST_FN, VFS_RO) == 0);
	v_assert(vfs_read_sector(&f, buf, 0) == 0);
	v_assert(test_sector_contains(buf, 'A') == 1);
	v_assert(vfs_read_sector(&f, buf, 1) == 0);
	v_assert(test_sector_contains(buf, 'B') == 1);
	vfs_close(&f);

	pool_deallocate_block(p, buf);
}

void
test_vfs_run_all(const struct vfs *meth, struct pool *p)
{
	outf("VFS:\r\n");
	test_vfs(meth, p);
	outf("OK\r\n");
}

#if 0
#include "vfs_pc.h"
int main(void)
{
	struct pool *p = pool_init(4);
	v_assert(p);
	test_vfs_run_all(&pc_vfs, p);
	
	return 0;
}
#endif
