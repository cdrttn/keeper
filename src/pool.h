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

#ifndef _POOL_H_
#define _POOL_H_

#include <stdint.h>
#include <stddef.h>
#include "vfs.h"

struct block {
	uint8_t block[VFS_SECT_SIZE];
	void *user_ptr;
	uint8_t allocated:1;
};

// this cb gets called when the user attempts to allocate
// a block when none are currently available. it should
// attempt to release memory back to the pool. it should
// not call pool_allocate_block!
struct pool;
typedef void (*try_free_cb)(struct pool *, void *);

struct cleanup {
	try_free_cb cb;
	void *arg;
	struct cleanup *next;
};

struct pool {
	uint16_t total;
	uint16_t nallocated;
	struct cleanup *cleanup;
	struct block data[0];
};

#define BLOCK(b) ((struct block *)((b) - offsetof(struct block, block)))
#define USER_PTR(b) (BLOCK(b)->user_ptr)

struct pool *pool_init(uint16_t amt);
void pool_free(struct pool *pool);
uint8_t *pool_allocate_block(struct pool *pool, void *p);
void pool_deallocate_block(struct pool *pool, uint8_t *block);
void pool_add_cleanup(struct pool *pool, struct cleanup *clean);
void pool_del_cleanup(struct pool *pool, struct cleanup *clean);
void pool_print(struct pool *p);


void test_pool_run_all(void);
void test_pool(void);


#endif // _POOL_H_
