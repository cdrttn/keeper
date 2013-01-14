#include "vfs_test.h"
#include "vfs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>


int main()
{
	uint16_t i;
	uint8_t sector[VFS_SECT_SIZE] = { '\0' };
	struct file fp;

	if (vfs_open(&test_vfs, &fp, "belch.txt", VFS_RW) < 0) {
		goto out;
	}

	const char *out = "Life of love";
	memcpy(sector, out, strlen(out));

	for (i = 0; i < 4; ++i) {
		if (vfs_write_sector(&fp, sector, i) < 0)
			goto out;
	}
	
	for (i = 0; i < 4; ++i) {
		if (vfs_read_sector(&fp, sector, i) < 0)
			goto out;
		assert(memcmp(sector, out, strlen(out)) == 0);
	}

	return 0;
out:
	perror("keeper");
	return 0;
}
