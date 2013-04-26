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

#ifndef _VFS_CRYPT_H_
#define _VFS_CRYPT_H_

struct file;
struct pool;

int8_t vfs_crypt_init(struct file *base, struct pool *pool);
int8_t vfs_crypt_format(struct file *fp, const void *password, size_t len);
int8_t vfs_crypt_unlock(struct file *fp, const void *password, size_t len);
int8_t vfs_crypt_chpass(struct file *fp, const void *newpw, size_t len);

void test_vfs_crypt_run_all(const struct vfs *meth, struct pool *p);

#endif // _VFS_CRYPT_H_
