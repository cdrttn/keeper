#ifndef _ACCDB_H_
#define _ACCDB_H_

struct accdb;
struct accdb_index;
struct vfs;
struct pool;

void test_accdb_plaintext(const struct vfs *meth, struct pool *pool);
void test_accdb_crypt(const struct vfs *meth, struct pool *pool);

#endif // _ACCDB_H_
