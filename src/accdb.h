#ifndef _ACCDB_H_
#define _ACCDB_H_
#include "pool.h"

struct vfs;

struct sector {
	uint8_t dirty:1;
	uint8_t empty:1;
	uint16_t refcount;
	uint16_t sector;
	uint8_t *buf;
	struct sector *prev, *next;
};

enum cache_strategy {
	CACHE_LRU,
	CACHE_MRU
};

struct accdb {
	struct file *fp;
	struct pool *pool;
	struct cleanup cleanup;
	uint8_t strategy:1;
	uint8_t run_init:1;
	uint16_t sect_start;
	uint16_t sect_end;
#define ACCDB_CACHE_SIZE 8
	struct sector cache[ACCDB_CACHE_SIZE];
	struct sector *mru;
	struct sector *lru;
#ifdef CACHE_MEASURE
	uint32_t cache_hits;
	uint32_t cache_misses;
#endif
};

struct accdb_index {
	struct accdb *db;
	uint8_t *buf;
	uint8_t *blob_user;
	uint8_t *blob_pass;
	uint8_t *blob_note;
	uint8_t *rec;
	uint8_t before_beginning:1;
};

typedef uint32_t accdb_id_t;

void test_accdb_plaintext(const struct vfs *meth, struct pool *pool);
void test_accdb_crypt(const struct vfs *meth, struct pool *pool);

int8_t accdb_open(struct accdb *db, struct file *fp, struct pool *pool);
int8_t accdb_close(struct accdb *db);

int8_t accdb_index_init(struct accdb *db, struct accdb_index *idx);
void accdb_index_clear(struct accdb_index *idx);
int8_t accdb_index_to_id(struct accdb_index *idx, accdb_id_t *id);
int8_t accdb_index_from_id(struct accdb *db, struct accdb_index *idx, accdb_id_t id);
uint8_t accdb_index_has_entry(struct accdb_index *idx);
int8_t accdb_index_next(struct accdb_index *idx);
int8_t accdb_index_prev(struct accdb_index *idx);
int8_t accdb_index_next_note(struct accdb_index *idx, const void **note, size_t *size);
int8_t accdb_index_get_entry(struct accdb_index *idx, const char **brief,
		      const char **user, const char **pass);
int8_t accdb_add(struct accdb *db, struct accdb_index *idx,
	  const char *brief, const char *user, const char *pass);
int8_t accdb_add_note(struct accdb_index *idx, void *data, size_t size);
int8_t accdb_del(struct accdb_index *idx);

#endif // _ACCDB_H_
