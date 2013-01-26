#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "pool.h"

struct pool *
pool_init(uint16_t amt)
{
	struct pool *pool;
	size_t tot = sizeof(struct pool) + sizeof(struct block) * amt;

	pool = calloc(1, tot);
	if (pool == NULL)
		return NULL;
	pool->total = amt;

	return pool;
}

void
pool_free(struct pool *pool)
{
	size_t tot = sizeof(struct pool) + sizeof(struct block) * pool->total;
	memset(pool, 0, tot);
	free(pool);
}

void
pool_deallocate_block(struct pool *pool, uint8_t *block)
{
	assert(BLOCK(block)->allocated == 1);
	assert(pool->nallocated > 0);
	memset(BLOCK(block), 0, sizeof(struct block));
	pool->nallocated--;
}

uint8_t *
pool_allocate_block(struct pool *pool, void *p)
{
	size_t i;
	struct block *b;
	struct cleanup *clean;

	if (pool->nallocated == pool->total) {
		// attempt to free up some memory!
		for (clean = pool->cleanup; clean; clean = clean->next) {
			clean->cb(pool, clean->arg);
		}
	}

	for (i = 0; i < pool->total; ++i) {
		b = &pool->data[i];
		if (!b->allocated) {
			b->allocated = 1;
			b->user_ptr = p;
			pool->nallocated++;
			assert(pool->nallocated <= pool->total);
			return b->block;
		}
	}

	return NULL;
}

// note: clean must be valid while pool is alive!
void
pool_add_cleanup(struct pool *pool, struct cleanup *clean)
{
	clean->next = NULL;
	if (pool->cleanup) {
		clean->next = pool->cleanup;
	}
	pool->cleanup = clean;
}

void
pool_del_cleanup(struct pool *pool, struct cleanup *clean)
{
	struct cleanup *tmp, *prev;

	if (clean == pool->cleanup) {
		pool->cleanup = clean->next;
		return;
	}

	prev = pool->cleanup;
	tmp = prev->next;
	while (tmp) {
		if (tmp == clean) {
			prev->next = tmp->next;
		}
		prev = tmp;
		tmp = tmp->next;
	}
}

// tests

#define TEST_POOL_SZ 10

static void
test_cleanup(struct pool *pool, void *arg)
{
	static uint8_t done = 0;
	uint8_t **buf = arg;
	size_t i;
	
	if (done)
		return;

	for (i = 0; i < TEST_POOL_SZ/2; ++i) {
		pool_deallocate_block(pool, buf[i]);
	}
	done = 1;
}

static uint8_t
test_block_is_zero(uint8_t *block)
{
	size_t i;
	for (i = 0; i < VFS_SECT_SIZE; ++i) {
		if (block[i] != 0x00)
			return 0;
	}
	return 1;
}

void
test_pool(void)
{
	uint8_t i;
	uint8_t *buf[TEST_POOL_SZ];
	struct pool *pool;
	struct cleanup c1,c2,c3;
	struct cleanup clean = {
		.cb = test_cleanup,
		.arg = buf
	};

	pool = pool_init(TEST_POOL_SZ);
	assert(pool != NULL);
	assert(pool->total == TEST_POOL_SZ);
	assert(pool->nallocated == 0);

	// test cleanup add/del
	pool_add_cleanup(pool, &c1);
	assert(pool->cleanup == &c1);
	pool_add_cleanup(pool, &c2);
	assert(pool->cleanup == &c2);
	assert(pool->cleanup->next == &c1);
	pool_add_cleanup(pool, &c3);
	assert(pool->cleanup == &c3);
	assert(pool->cleanup->next == &c2);

	pool_del_cleanup(pool, &c1);
	pool_del_cleanup(pool, &c2);
	pool_del_cleanup(pool, &c3);
	assert(pool->cleanup == NULL);

	// add the real one
	pool_add_cleanup(pool, &clean);

	for (i = 0; i < TEST_POOL_SZ; ++i) {
		void *n = (void*)(uintptr_t)i;
		buf[i] = pool_allocate_block(pool, n);
		assert(buf[i] != NULL);
		assert(BLOCK(buf[i])->allocated == 1);
		assert(USER_PTR(buf[i]) == n);
		assert(test_block_is_zero(buf[i]) == 1);
		memset(buf[i], i, VFS_SECT_SIZE);
	}
	assert(pool->nallocated == pool->total);

	// cleanup should free back the first half
	for (i = 0; i < TEST_POOL_SZ/2; ++i) {
		uint8_t *b;
		void *n = (void*)(uintptr_t)i;
		b = pool_allocate_block(pool, n);
		assert(b != NULL);
		assert(BLOCK(b)->allocated == 1);
		assert(USER_PTR(b) == n);
		assert(b == buf[i]);
		assert(test_block_is_zero(b) == 1);
	}
	assert(pool->nallocated == pool->total);

	assert(pool_allocate_block(pool, NULL) == NULL);

	for (i = 0; i < TEST_POOL_SZ; ++i) {
		pool_deallocate_block(pool, buf[i]);
		assert(BLOCK(buf[i])->allocated == 0);
	}
	assert(pool->nallocated == 0);

	pool_free(pool);
}

#if 0
int main(void)
{
	printf("Pool:\n");
	test_pool();
	printf("OK\n");
	
	return 0;
}
#endif
