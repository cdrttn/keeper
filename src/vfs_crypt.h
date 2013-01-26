#ifndef _VFS_CRYPT_H_
#define _VFS_CRYPT_H_

struct file;
struct pool;

int8_t vfs_crypt_init(struct file *base, struct pool *pool);
int8_t vfs_crypt_format(struct file *fp, const void *password, size_t len);
int8_t vfs_crypt_unlock(struct file *fp, const void *password, size_t len);
int8_t vfs_crypt_chpass(struct file *fp, const void *newpw, size_t len);

#endif // _VFS_CRYPT_H_
