#include <stddef.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "intop.h"
#include "accdb.h"
#include "vfs.h"
#include "pool.h"
#include "test.h"

struct accdb;

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

#define SECT_TYPE_INDEX		0xf
#define SECT_TYPE_BLOB		0xe
#define SECT_HEADER_SIZE_TYPE 	0
#define SECT_HEADER_PREV 	2
#define SECT_HEADER_NEXT 	4
#define SECT_HEADER_MAX		6
#define SECT_PAYLOAD_MAX	(VFS_SECT_SIZE - SECT_HEADER_MAX)

#define REC_INDEX_SIZE_TYPE	0
#define REC_INDEX_ENTRY		1
#define REC_INDEX_ENTRY_MAX	128

#define REC_BLOB_USERNAME	0
#define REC_BLOB_PASSWORD	1
#define REC_BLOB_NOTE		2
#define REC_BLOB_SIZE_TYPE	0
#define REC_BLOB_ENTRY		2
#define REC_BLOB_ENTRY_MAX	(SECT_PAYLOAD_MAX - 2)

#define BITMAP_START(db)	(0 + (db)->sect_start)
#define BITMAP_MAX(db)		(16 + (db)->sect_start)
#define INDEX_START(db)		BITMAP_MAX(db)
#define SECT_NULL 		0
#define ACCDB_MAX		0xffff
#define SECT_IS_NULL(i)		((i) == SECT_NULL)
#define SECT_PER_BITMAP		(VFS_SECT_SIZE * 8)
#define BITMAP_OFFSET_TO_SECT(db, bsect, bbyte, bit)	\
	((bsect - (db)->sect_start) * SECT_PER_BITMAP + bbyte * 8 + bit)
#define GET_SECTOR(b) ((struct sector *)USER_PTR(b))

static inline void accdb_cache_dirty(uint8_t *buf);
static inline uint16_t buf_get_size(const uint8_t *sector);
static inline uint8_t buf_get_type(const uint8_t *sector);
static inline uint16_t buf_get_next(const uint8_t *sector);
static inline uint16_t buf_get_prev(const uint8_t *sector);

#ifdef DBGPRINT
#define LOG(x) logf(x)
static const char *
_s(const uint8_t *buf)
{
	static char str[64];
	struct sector *sector = GET_SECTOR(buf);

	sprintf(str, "sector %u: ref %u, dirty %u, size %u, type 0x%x, "
		     "prev %u, next %u",
		sector->sector,
		sector->refcount,
		sector->dirty,
		buf_get_size(sector->buf),
		buf_get_type(sector->buf),
		buf_get_prev(sector->buf),
		buf_get_next(sector->buf));

	return str;
}
#else
#define LOG(x)
#endif

static inline void 
buf_get_type_size(const uint8_t *sector, uint8_t *type, uint16_t *size)
{
	// first four bits = type, remaining 12 = size
	uint16_t size_type = get_uint16(sector, SECT_HEADER_SIZE_TYPE);
	*type = size_type >> 12;
	*size = size_type & 0xfff;
}

static inline void
buf_set_type_size(uint8_t *sector, uint8_t type, uint8_t size)
{
	set_uint16(sector, SECT_HEADER_SIZE_TYPE, (type << 12) | size);
}

static inline uint16_t
buf_get_size(const uint8_t *sector)
{
	return get_uint16(sector, SECT_HEADER_SIZE_TYPE) & 0xfff;
}

static inline void
buf_set_size(uint8_t *sector, uint16_t size)
{
	size &= 0xfff;
	set_uint16(sector, SECT_HEADER_SIZE_TYPE,
		   (get_uint16(sector, SECT_HEADER_SIZE_TYPE) & 0xf000) | size);
}

static inline uint8_t
buf_get_type(const uint8_t *sector)
{
	return get_uint16(sector, SECT_HEADER_SIZE_TYPE) >> 12;
}

static inline uint16_t
buf_get_next(const uint8_t *sector)
{
	return get_uint16(sector, SECT_HEADER_NEXT);
}

static inline void
buf_set_next(uint8_t *sector, uint16_t next)
{
	set_uint16(sector, SECT_HEADER_NEXT, next);
}

static inline uint16_t
buf_get_prev(const uint8_t *sector)
{
	return get_uint16(sector, SECT_HEADER_PREV);
}

static inline void
buf_set_prev(uint8_t *sector, uint16_t prev)
{
	set_uint16(sector, SECT_HEADER_PREV, prev);
}

static inline uint8_t *
buf_get_payload(uint8_t *sector)
{
	return sector + SECT_HEADER_MAX;
}

static inline uint8_t *
buf_get_end(uint8_t *sector)
{
	return sector + SECT_HEADER_MAX + buf_get_size(sector);
}

static inline uint16_t
buf_get_available(uint8_t *sector)
{
	return SECT_PAYLOAD_MAX - buf_get_size(sector);
}

static uint8_t *
buf_allocate_record(uint8_t *sector, uint16_t size)
{
	uint16_t used = buf_get_size(sector);

	if (used + size > SECT_PAYLOAD_MAX)
		return NULL;

	buf_set_size(sector, used + size);

	accdb_cache_dirty(sector);
	return buf_get_payload(sector) + used;
}

static void
buf_deallocate_record(uint8_t *sector, uint8_t *top, uint16_t size)
{
	uint16_t used = buf_get_size(sector);
	uint8_t *payload = buf_get_payload(sector);
	uint8_t *bottom = top + size;
	uint8_t *end = payload + used;

	assert(top >= payload && bottom <= end);

	if (bottom < end)
		memmove(top, bottom, end - bottom);

	buf_set_size(sector, used - size);
	accdb_cache_dirty(sector);
}

static void
buf_deallocate_section(uint8_t *sector, uint16_t offset, uint16_t size)
{
	buf_deallocate_record(sector, buf_get_payload(sector) + offset, size);
}

static inline uint8_t
index_rec_get_extended(const uint8_t *payload)
{
	return payload[REC_INDEX_SIZE_TYPE] >> 7;
}

static inline uint8_t
index_rec_get_size(const uint8_t *payload)
{
	return payload[REC_INDEX_SIZE_TYPE] & 0x7f;
}

// same as above, but with type/length byte added
static inline uint8_t
index_rec_get_total_size(const uint8_t *payload)
{
	return index_rec_get_size(payload) + 1;
}

static inline void
index_rec_next(uint8_t **payload)
{
	*payload = *payload + index_rec_get_total_size(*payload);
}

static inline void
index_rec_set_extended(uint8_t *payload, uint8_t ext)
{
	payload[REC_INDEX_SIZE_TYPE] = ((ext & 1) << 7) |
			(payload[REC_INDEX_SIZE_TYPE] & 0x7f);
}

static inline void
index_rec_set_size(uint8_t *payload, uint8_t size)
{
	payload[REC_INDEX_SIZE_TYPE] = (payload[REC_INDEX_SIZE_TYPE] & 0x80) |
			(size & 0x7f);
}

#define CHECK(ex) if (!(ex)) return -1
static int8_t
index_rec_parse(const uint8_t *payload, const char **brief,
	        const char **user, const char **pass)
{
	size_t size = index_rec_get_size(payload);
	const uint8_t *p = payload + REC_INDEX_ENTRY;
	const uint8_t *end = p + size;

	CHECK(!index_rec_get_extended(payload));
	*brief = (const char *)p;

	p = memchr(p, '\0', size);
	CHECK(p != NULL);
	++p;

	*user = (const char *)p;
	
	p = memchr(p, '\0', end - p);
	CHECK(p != NULL);
	++p;

	*pass = (const char *)p;

	p = memchr(p, '\0', end - p);
	CHECK(p != NULL);
	CHECK(++p == end);

	return 0;
}

static int8_t
index_rec_parse_extended(const uint8_t *payload, const char **brief,
			 uint16_t *ptr)
{
	size_t size = index_rec_get_size(payload);
	const uint8_t *p = payload + REC_INDEX_ENTRY;
	const uint8_t *end = p + size;

	CHECK(index_rec_get_extended(payload));
	*brief = (const char *)p;

	p = memchr(p, '\0', size);
	CHECK(p != NULL);
	++p;

	CHECK(p + 2 == end);
	*ptr = get_uint16(p, 0);

	return 0;
}
#undef CHECK

enum index_type {
	INDEX_NORMAL,
	INDEX_EXT,
	INDEX_INVALID,
};
static enum index_type
index_rec_find_type(const char *brief, const char *user, const char *pass,
		    uint8_t *size)
{
	size_t len_brief = strlen(brief) + 1;
	size_t len_user = strlen(user) + 1;
	size_t len_pass = strlen(pass) + 1;
	size_t len = len_brief + len_user + len_pass;

	if (len <= REC_INDEX_ENTRY_MAX) {
		*size = len + 1;
		return INDEX_NORMAL;
	}	
	if (len > REC_INDEX_ENTRY_MAX && len_brief + 2 <= REC_INDEX_ENTRY_MAX) {
		*size = len_brief + 2 + 1;
		return INDEX_EXT;
	}
	
	return INDEX_INVALID;
}

static int8_t
index_rec_create(uint8_t *payload, const char *brief,
	         const char *user, const char *pass)
{
	uint8_t *p = payload + REC_INDEX_ENTRY;
	size_t len_brief = strlen(brief) + 1;
	size_t len_user = strlen(user) + 1;
	size_t len_pass = strlen(pass) + 1;
	size_t len;

	len = len_brief + len_user + len_pass;
	if (len > REC_INDEX_ENTRY_MAX)
		return -1;

	index_rec_set_extended(payload, 0);
	index_rec_set_size(payload, len);
	memcpy(p, brief, len_brief);
	p += len_brief;
	memcpy(p, user, len_user);
	p += len_user;
	memcpy(p, pass, len_pass);
	
	return 0;
}

static int8_t
index_rec_create_extended(uint8_t *payload, const char *brief, uint16_t ptr)
{
	uint8_t *p = payload + REC_INDEX_ENTRY;
	size_t len_brief = strlen(brief) + 1;
	size_t len;

	len = len_brief + 2;
	if (len > REC_INDEX_ENTRY_MAX)
		return -1;

	index_rec_set_extended(payload, 1);
	index_rec_set_size(payload, len);
	memcpy(p, brief, len_brief);
	p += len_brief;

	set_uint16(p, 0, ptr);

	return 0;
}

static inline uint8_t
blob_rec_get_type(const uint8_t *payload)
{
	return get_uint16(payload, REC_BLOB_SIZE_TYPE) >> 12;
}

static inline uint16_t
blob_rec_get_size(const uint8_t *payload)
{
	return get_uint16(payload, REC_BLOB_SIZE_TYPE) & 0xfff;
}

static inline uint16_t
blob_rec_get_total_size(const uint8_t *payload)
{
	return blob_rec_get_size(payload) + 2;
}

static inline void
blob_rec_next(uint8_t **payload)
{
	*payload = *payload + blob_rec_get_total_size(*payload);
}

static inline void
blob_rec_set_type(uint8_t *payload, uint8_t type)
{
	set_uint16(payload, REC_BLOB_SIZE_TYPE,
		   (get_uint16(payload, REC_BLOB_SIZE_TYPE) & 0x0fff) |
		   (type << 12));
}

static inline void
blob_rec_set_size(uint8_t *payload, uint16_t size)
{
	set_uint16(payload, REC_BLOB_SIZE_TYPE,
		   (get_uint16(payload, REC_BLOB_SIZE_TYPE) & 0xf000) |
		   (size & 0xfff));
}

static inline void *
blob_rec_get_data(uint8_t *payload)
{
	return payload + 2;
}

static int8_t
blob_rec_create(uint8_t *payload, uint8_t type, const void *data, size_t size)
{
	if (size > REC_BLOB_ENTRY_MAX)
		return -1;

	blob_rec_set_type(payload, type);
	blob_rec_set_size(payload, size);
	memcpy(blob_rec_get_data(payload), data, size);

	return 0;
}

/*
Cache

ACCDB cache is a crude LRU and MRU system. Cache items are organized in a list.
The most recently requested item is at the front, and the oldest item is
at the end. The list may contain up to ACCDB_CACHE_MAX buffers. Searching
is linear, since there're only a handful of slots in practice.

LRU replacement works by scaning the cache list back to front, looking for
the first item that is not referenced. The item is either reused for
a new sector of data and moved to the front of the list, or released
back to the pool allocator if it wants us to free up some memory. Slots
with unallocated buffers are sent to the back of the list.

MRU is just the reverse process.

MRU ->
newest -\
	[0] [1] [2] [3] [4]
			  \- oldest
			  <- LRU

The the replacement strategy is determined by db->strategy.

*/

static inline void
sector_list_pop(struct sector *s)
{
	if (s->prev) {
		s->prev->next = s->next;
	}
	if (s->next) {
		s->next->prev = s->prev;
	}
	s->prev = NULL;
	s->next = NULL;
}

static void
accdb_cache_init(struct accdb *db)
{
	size_t i;
	struct sector *s, *prev = NULL;

	db->run_init = 1;	
	for (i = 0; i < ACCDB_CACHE_SIZE; ++i) {
		s = &db->cache[i];
		memset(s, 0, sizeof(*s));
		if (prev)
			prev->next = s;
		s->prev = prev;
		prev = s;

		//attempt to preallocate a block from the pool
		//it's not an error if we can't fill every cache
		//slot; allocation can be attempted later.
		s->buf = pool_allocate_block(db->pool, s);
		s->empty = 1;
	}
	db->mru = &db->cache[0];
	db->lru = &db->cache[ACCDB_CACHE_SIZE - 1];
	db->run_init = 0;	

#ifdef CACHE_MEASURE
	db->cache_hits = db->cache_misses = 0;
#endif
}

// find a spot in the cache going by the chosen strategy
// if not all cache slots are allocated, attempt to allocate
// a block to keep the cache as full as possible.
static struct sector *
accdb_cache_provision(struct accdb *db, uint8_t cleanup)
{
	struct sector *s;

	// empty slots, if present, are at the end of the list
	// attempt to fill one if we're not doing a cleanup
	if (!cleanup) {
		s = db->lru;
		if (!s->buf && (s->buf = pool_allocate_block(db->pool, s))) {
			s->empty = 1;
			return s;
		}
	}

	// can't allocate memory? attempt to reclaim a an item that
	// has a buffer already
	if (db->strategy == CACHE_LRU) {
		for (s = db->lru; s != NULL; s = s->prev) {
			if (s->refcount == 0 && s->buf) {
				return s;
			}
		}
	} else {
		for (s = db->mru; s != NULL; s = s->next) {
			if (s->refcount == 0 && s->buf) {
				return s;
			}
		}
	}

	return NULL;
}

// move s to the head of the list at mru
static inline void
accdb_cache_mru(struct accdb *db, struct sector *s)
{
	if (s == db->mru)
		return;
	if (s == db->lru)
		db->lru = s->prev;
	sector_list_pop(s);
	s->next = db->mru;
	db->mru->prev = s;
	db->mru = s;
}

// move s to the back of the list
static inline void
accdb_cache_lru(struct accdb *db, struct sector *s)
{
	if (s == db->lru)
		return;
	if (s == db->mru)
		db->mru = s->next;
	sector_list_pop(s);
	s->prev = db->lru;
	db->lru->next = s;
	db->lru = s;
}

static void
accdb_cache_print(struct accdb *db, int quiet)
{
	struct sector *s;
	int avail = 0;

	logf((_P("Order: MRU -> LRU\n")));
	logf((_P("-------------------\n")));
	for (s = db->mru; s; s = s->next) {
		if (!quiet) {
			logf((_P("cache:\n")));
			logf((_P(" dirty = %d\n"), (int)s->dirty));
			logf((_P(" refcount = %d\n"), (int)s->refcount));
			logf((_P(" sector = %d\n"), (int)s->sector));
			logf((_P(" empty = %d\n"), (int)s->empty));
			logf((_P(" buf = %p\n\n"), s->buf));
		}
		if (!s->refcount && s->buf)
			avail++;
	}
	logf((_P("available, %d/%d\n"), avail, ACCDB_CACHE_SIZE));
	logf((_P("-------------------\n")));
#ifdef CACHE_MEASURE
	logf((_P("Total: %lu, Hits: %lu, Misses: %lu\n"),
	       (unsigned long)(db->cache_hits + db->cache_misses),
	       (unsigned long)db->cache_hits,
	       (unsigned long)db->cache_misses));
#endif
}

static int8_t
accdb_cache_flush_one(struct accdb *db, struct sector *sector)
{
	int8_t rv = 0;

	if (sector->dirty) {
		assert(sector->buf != NULL);
		rv = vfs_write_sector(db->fp, sector->buf,
				      sector->sector);
		LOG(("WRITE %s\n", _s(sector->buf)));
		if (rv == 0)
			sector->dirty = 0;
	}

	return rv;
}

static void
accdb_cache_do_cleanup(struct pool *pool, struct accdb *db)
{
	struct sector *s;

	if (db->run_init)
		return;
	
	s = accdb_cache_provision(db, 1);
	if (s) {
		assert(s->buf);
		LOG(("CLEANUP: %s\n", _s(s->buf)));
		// XXX graceful way to handle error here?
		if (accdb_cache_flush_one(db, GET_SECTOR(s->buf)) < 0)
			return;
		pool_deallocate_block(pool, s->buf);
		s->buf = NULL;
		s->empty = 1;
		// keep unallocated cache entries at the back of the list
		accdb_cache_lru(db, s);
	}
}

static uint8_t *
accdb_cache_get(struct accdb *db, uint16_t sector)
{
	struct sector *s;

	// is sector in cache?	
	for (s = db->mru; s != NULL; s = s->next) {
		if (s->sector == sector && s->buf && !s->empty) {
			s->refcount++;
			LOG(("REF %s\n", _s(s->buf)));
			accdb_cache_mru(db, s);
#ifdef CACHE_MEASURE
			db->cache_hits++;
#endif
			return s->buf;
		}	
	}

	// no. find a spot in the cache, and read sector.
#ifdef CACHE_MEASURE
	db->cache_misses++;
#endif
	s = accdb_cache_provision(db, 0);
	if (s) {	
		if (accdb_cache_flush_one(db, s) < 0)
			return NULL;
		if (vfs_read_sector(db->fp, s->buf, sector) < 0)
			return NULL;
		s->sector = sector;
		s->refcount = 1;
		s->empty = 0;
		accdb_cache_mru(db, s);
		LOG(("READ %s\n", _s(s->buf)));
		return s->buf;
	}

	// no space in cache! out of memory!
	return NULL;
}

static int8_t
accdb_cache_flush(struct accdb *db)
{
	size_t i;
	struct sector *s;

	for (i = 0; i < ACCDB_CACHE_SIZE; ++i) {
		s = &db->cache[i];
		if (s->buf && accdb_cache_flush_one(db, s) < 0)
			return -1;
	}

	return 0;
}

static int8_t
accdb_cache_clear(struct accdb *db)
{
	size_t i;

	if (accdb_cache_flush(db) < 0)
		return -1;

	for (i = 0; i < ACCDB_CACHE_SIZE; ++i) {
		struct sector *s = &db->cache[i];
		if (s->refcount == 0) {
			s->empty = 1;
			if (s->buf)
				memset(s->buf, 0, VFS_SECT_SIZE);
			//pool_deallocate_block(db->pool, s->buf);
		}
	}

	return 0;
}

static inline uint8_t *
accdb_cache_ref(uint8_t *buf)
{
	struct sector *sector = GET_SECTOR(buf);
	sector->refcount++;
	return buf;
}

static inline uint16_t
accdb_cache_sector(uint8_t *buf)
{
	struct sector *sector = GET_SECTOR(buf);
	return sector->sector;
}

static inline void
accdb_cache_dirty(uint8_t *buf)
{
	struct sector *sector = GET_SECTOR(buf);
	sector->dirty = 1;
}

static inline void
accdb_cache_clean(uint8_t *buf)
{
	struct sector *sector = GET_SECTOR(buf);
	sector->dirty = 0;
}

static inline void
accdb_cache_put(uint8_t *buf)
{
	struct sector *sector = GET_SECTOR(buf);
	assert(sector->refcount > 0);
	sector->refcount--;
}

static inline void
accdb_cache_put_dirty(uint8_t *buf)
{
	accdb_cache_dirty(buf);
	accdb_cache_put(buf);
}

static int8_t
accdb_allocate_sector(struct accdb *db, uint16_t *free_sect)
{
	uint16_t sector, byte, bit;
	uint8_t *buf;

	// search for a free sector
	for (sector = BITMAP_START(db); sector < BITMAP_MAX(db); ++sector) {
		buf = accdb_cache_get(db, sector);
		if (!buf)
			return -1;
		for (byte = 0; byte < VFS_SECT_SIZE; ++byte) {
			for (bit = 0; bit < 8; ++bit) {
				if (!(buf[byte] & (1 << bit))) {
					*free_sect = BITMAP_OFFSET_TO_SECT(db,
									   sector,
								           byte,
									   bit);
					buf[byte] |= (1 << bit);
					accdb_cache_put_dirty(buf);
					return 0;
				}
			}
		}
	}

	// file is full!

	return -1;
}

static uint8_t *
accdb_allocate_buf(struct accdb *db, uint8_t type)
{
	uint16_t sector;
	uint8_t *buf;

	if (accdb_allocate_sector(db, &sector) < 0)
		return NULL;

	buf = accdb_cache_get(db, sector);
	if (buf == NULL)
		return NULL;
	buf_set_type_size(buf, type, 0);
	buf_set_prev(buf, 0);
	buf_set_next(buf, 0);
	accdb_cache_dirty(buf);

	return buf;
}

static int8_t
accdb_deallocate_sector(struct accdb *db, uint16_t sect)
{
	uint16_t bitmap = sect / SECT_PER_BITMAP + BITMAP_START(db);
	uint16_t offset = sect % SECT_PER_BITMAP;
	uint16_t byte = offset / 8;
	uint8_t bit = offset % 8;
	uint8_t *buf;

	if (sect < BITMAP_MAX(db))
		return -1;

	buf = accdb_cache_get(db, bitmap);
	if (buf == NULL)
		return -1;
	buf[byte] &= ~(1 << bit);
	accdb_cache_put_dirty(buf);

	return 0;
}

static int8_t
accdb_deallocate_buf(struct accdb *db, uint8_t *buf)
{
	struct sector *s = GET_SECTOR(buf);
	assert(s->refcount == 1);
	s->dirty = 0;
	accdb_cache_put(buf);
	return accdb_deallocate_sector(db, s->sector);
}

static int8_t
accdb_format(struct accdb *db)
{
	uint16_t i, count, byte;
	uint8_t *buf;

	count = db->sect_start;

	for (i = BITMAP_START(db); i < BITMAP_MAX(db); ++i) {
		buf = accdb_cache_get(db, i);
		if (buf == NULL)
			return -1;
		memset(buf, 0, VFS_SECT_SIZE);
		byte = 0;
		if (i == BITMAP_START(db)) {
			// mark the first 16 bitmap bits, so the sectors
			// of the bitmap aren't written over.
			buf[byte++] = 0xff;
			buf[byte++] = 0xff;
		}
		// mark reserved sectors
		for (; count && byte < VFS_SECT_SIZE; ++byte) {
			uint8_t bit;
			for (bit = 0; count && bit < 8; ++bit, --count)
				buf[byte] |= (1<<bit);
		}
		accdb_cache_put_dirty(buf);
	}

	buf = accdb_allocate_buf(db, SECT_TYPE_INDEX);
	accdb_cache_put_dirty(buf);

	return accdb_cache_flush(db);
}

// append b to a
static int8_t
accdb_list_append(struct accdb *db, uint8_t *a, uint8_t *b)
{
	uint16_t new_sector = accdb_cache_sector(b);
	uint16_t next = buf_get_next(a);
	uint8_t *next_buf;

	if (!SECT_IS_NULL(next)) {
		next_buf = accdb_cache_get(db, next);
		if (next_buf == NULL)
			return -1;
		buf_set_prev(next_buf, new_sector);
		accdb_cache_put_dirty(next_buf);
	}

	buf_set_next(a, new_sector);
	buf_set_next(b, next);
	buf_set_prev(b, accdb_cache_sector(a));

	accdb_cache_dirty(a);
	accdb_cache_dirty(b);
	
	return 0;
}

static int8_t
accdb_list_remove(struct accdb *db, uint8_t *a)
{
	uint16_t next = buf_get_next(a);
	uint16_t prev = buf_get_prev(a);
	uint8_t *next_buf = NULL;
	uint8_t *prev_buf = NULL;

	if (!SECT_IS_NULL(next) &&
	    !(next_buf = accdb_cache_get(db, next)))
		return -1;

	if (!SECT_IS_NULL(prev) &&
	    !(prev_buf = accdb_cache_get(db, prev)))
		return -1;

	if (prev_buf) {
		buf_set_next(prev_buf, next);
		accdb_cache_put_dirty(prev_buf);
	}
	if (next_buf) {
		buf_set_prev(next_buf, prev);
		accdb_cache_put_dirty(next_buf);
	}

	buf_set_next(a, SECT_NULL);
	buf_set_prev(a, SECT_NULL);
	accdb_cache_dirty(a);

	return 0;
}

static int8_t
accdb_list_clear(struct accdb *db, uint16_t ptr)
{
	uint8_t *buf;

	while (ptr) {
		buf = accdb_cache_get(db, ptr);
		if (buf == NULL)
			return -1;
		ptr = buf_get_next(buf);
		if (accdb_deallocate_buf(db, buf) < 0)
			return -1;
	}

	return 0;
}

void
accdb_index_clear(struct accdb_index *idx)
{
	if (idx->buf)
		accdb_cache_put(idx->buf);
	if (idx->blob_user)
		accdb_cache_put(idx->blob_user);
	if (idx->blob_pass)
		accdb_cache_put(idx->blob_pass);
	if (idx->blob_note)
		accdb_cache_put(idx->blob_note);
	memset(idx, 0, sizeof(*idx));
}

static int8_t
accdb_index_init_internal(struct accdb *db, struct accdb_index *idx)
{
	accdb_index_clear(idx);

	idx->db = db;
	idx->buf = accdb_cache_get(db, INDEX_START(db));
	if (idx->buf == NULL)
		return -1;
	if (buf_get_type(idx->buf) != SECT_TYPE_INDEX) {
		accdb_cache_put(idx->buf);
		return -1;
	}
	idx->rec = buf_get_payload(idx->buf);

	return 0;
}

int8_t
accdb_index_init(struct accdb *db, struct accdb_index *idx)
{
	accdb_index_init_internal(db, idx);

	// INDEX_START may have been emptied
	if (buf_get_size(idx->buf) == 0 && buf_get_next(idx->buf)) {
		uint16_t next = buf_get_next(idx->buf);
		accdb_cache_put(idx->buf);
		idx->buf = accdb_cache_get(db, next);
		if (idx->buf == NULL)
			return -1;
		idx->rec = buf_get_payload(idx->buf);
	}

	return 0;
}

uint8_t
accdb_index_has_entry(struct accdb_index *idx)
{
	if (!idx->rec || !idx->buf || idx->before_beginning)
		return 0;
	return idx->rec < buf_get_end(idx->buf);
}

int8_t
accdb_index_next(struct accdb_index *idx)
{
	if (idx->blob_user)
		accdb_cache_put(idx->blob_user);
	if (idx->blob_pass)
		accdb_cache_put(idx->blob_pass);
	idx->blob_user = idx->blob_pass = NULL;

	if (idx->before_beginning) {
		idx->before_beginning = 0;
		return 0;
	}

	index_rec_next(&idx->rec);

	if (idx->rec >= buf_get_end(idx->buf)) {
		uint16_t ptr = buf_get_next(idx->buf);
		if (ptr == 0)
			return 0;
		accdb_cache_put(idx->buf);
		idx->buf = accdb_cache_get(idx->db, ptr);
		if (idx->buf == NULL)
			return -1;
		idx->rec = buf_get_payload(idx->buf);
		if (idx->rec >= buf_get_end(idx->buf))
			return -1;
	}

	return 0;
}

int8_t
accdb_index_prev(struct accdb_index *idx)
{
	uint8_t *pl, *end;

	if (idx->blob_user)
		accdb_cache_put(idx->blob_user);
	if (idx->blob_pass)
		accdb_cache_put(idx->blob_pass);
	idx->blob_user = idx->blob_pass = NULL;

	// if this is the first record of the page, we must find the bottom
	// record of the previous page
	if (idx->rec == buf_get_payload(idx->buf)) {
		uint8_t *buf;
		uint16_t prev;
		prev = buf_get_prev(idx->buf);
		if (!prev) {
			idx->before_beginning = 1;
			return 0;
		}
		buf = accdb_cache_get(idx->db, prev);
		if (buf == NULL)
			return -1;
		idx->rec = buf_get_end(buf);
		accdb_cache_put(idx->buf);
		idx->buf = buf;
	}

	pl = buf_get_payload(idx->buf);
	end = buf_get_end(idx->buf);
	
	while (pl + index_rec_get_total_size(pl) < idx->rec) {
		assert(pl < end);
		index_rec_next(&pl);
	}

	idx->rec = pl;

	return 0;
}

static uint8_t
blob_find(uint8_t *sector, const void **data, size_t *size, uint8_t type)
{
	uint8_t *pl = buf_get_payload(sector);
	uint8_t *end = buf_get_end(sector);

	while (pl < end) {
		if (blob_rec_get_type(pl) == type) {
			if (size)
				*size = blob_rec_get_size(pl);
			*data = blob_rec_get_data(pl);
			return 1;
		}

		blob_rec_next(&pl);
	}

	return 0;
}

static uint8_t *
blob_list_find(struct accdb *db, const void **data, size_t *size,
	       uint16_t ptr, uint8_t type)
{
	uint8_t *buf;

	while (ptr) {
		buf = accdb_cache_get(db, ptr);
		if (buf == NULL)
			return NULL;
		if (blob_find(buf, data, size, type))
			return buf;
		ptr = buf_get_next(buf);
		accdb_cache_put(buf);
	}

	return NULL;
}

int8_t
accdb_index_get_entry(struct accdb_index *idx, const char **brief,
		      const char **user, const char **pass)
{
	if (idx->rec == NULL || !accdb_index_has_entry(idx))
		return -1;

	if (index_rec_get_extended(idx->rec)) {
		uint16_t ptr;
		if (index_rec_parse_extended(idx->rec, brief, &ptr) < 0)
			return -1;
		*user = NULL;
		*pass = NULL;
		if (!idx->blob_user) {
			idx->blob_user = blob_list_find(idx->db,
							(const void **)user,
							NULL, ptr,
							REC_BLOB_USERNAME);
			if (idx->blob_user == NULL)
				return -1;
		}
		if (!idx->blob_pass) {
			idx->blob_pass = blob_list_find(idx->db,
							(const void **)pass,
							NULL, ptr,
							REC_BLOB_PASSWORD);
			if (idx->blob_pass == NULL)
				return -1;
		}
		if (!*user && !*pass) {
			assert(idx->blob_user != NULL);
			assert(idx->blob_pass != NULL);
			blob_find(idx->blob_user, (const void **)user, NULL,
				  REC_BLOB_USERNAME);
			blob_find(idx->blob_pass, (const void **)pass, NULL,
				  REC_BLOB_PASSWORD);
		}
		if (!*user || !*pass)
			return -1;
	} else {
		if (index_rec_parse(idx->rec, brief, user, pass) < 0)
			return -1;
	}

	return 0;
}

static int8_t
blob_create(struct accdb *db, const char *user, const char *pass, uint16_t *ptr)
{
	size_t blen;
	uint8_t *rec, *blob1, *blob2;

	blob1 = blob2 = NULL;

	blob1 = accdb_allocate_buf(db, SECT_TYPE_BLOB);
	if (blob1 == NULL)
		goto out;

	blen = strlen(user) + 1;		
	rec = buf_allocate_record(blob1, blen + 2);
	if (rec == NULL)
		goto out;
	if (blob_rec_create(rec, REC_BLOB_USERNAME, user, blen) < 0)
		goto out;

	blen = strlen(pass) + 1;
	rec = buf_allocate_record(blob1, blen + 2);
	if (rec == NULL) {
		blob2 = accdb_allocate_buf(db, SECT_TYPE_BLOB);
		if (blob2 == NULL)
			goto out;
		rec = buf_allocate_record(blob2, blen + 2);
		if (rec == NULL)
			goto out;
		if (accdb_list_append(db, blob1, blob2) < 0)
			goto out;
	}
	if (blob_rec_create(rec, REC_BLOB_PASSWORD, pass, blen) < 0)
		goto out;

	assert(blob1);
	*ptr = accdb_cache_sector(blob1);

	if (blob1)
		accdb_cache_put_dirty(blob1);
	if (blob2)
		accdb_cache_put_dirty(blob2);

	return 0;

out:
	if (blob1)
		accdb_deallocate_buf(db, blob1);
	if (blob2)
		accdb_deallocate_buf(db, blob2);

	return -1;
}

int8_t
accdb_add(struct accdb *db, struct accdb_index *idx,
	  const char *brief, const char *user, const char *pass)
{
	uint8_t len;
	enum index_type type;
	uint16_t ptr = 0, blob = 0;

	type = index_rec_find_type(brief, user, pass, &len);
	if (type == INDEX_INVALID)
		return -1;

	if (accdb_index_init_internal(db, idx) < 0)
		return -1;

	if (type == INDEX_EXT) {
		if (blob_create(db, user, pass, &blob) < 0)
			goto out;
	}

	// traverse the index, looking for space for the new entry
	idx->rec = NULL;
	while (1) {
		idx->rec = buf_allocate_record(idx->buf, len);
		ptr = buf_get_next(idx->buf);
		if (idx->rec || !ptr)
			break;
		accdb_cache_put(idx->buf);
		idx->buf = accdb_cache_get(db, ptr);
		if (idx->buf == NULL)
			goto out;
	} 

	// no space, make a new index sector
	if (!idx->rec) {
		uint8_t *tmp = accdb_allocate_buf(db, SECT_TYPE_INDEX);
		if (tmp == NULL)
			goto out;
		if (accdb_list_append(db, idx->buf, tmp) < 0) {
			accdb_deallocate_buf(db, tmp);
			goto out;
		}
		accdb_cache_put(idx->buf);
		//accdb_cache_flush(db);
		idx->buf = tmp;
		idx->rec = buf_allocate_record(idx->buf, len);
		if (idx->rec == NULL)
			goto out;
	}

	if (type == INDEX_EXT) {
		assert(blob != 0);
		if (index_rec_create_extended(idx->rec, brief, blob) < 0)
			goto out;
	} else {
		if (index_rec_create(idx->rec, brief, user, pass) < 0)
			goto out;
	}

	// idx will now point to the new record

	return 0;

out:
	accdb_index_clear(idx);

	return -1;
}

static int8_t
convert_to_extended(struct accdb_index *idx)
{
	const char *brief, *user, *pass;
	uint16_t blob;
	uint8_t *modp, *endp;
	
	if (index_rec_parse(idx->rec, &brief, &user, &pass) < 0)
		return -1;
	
	if (blob_create(idx->db, user, pass, &blob) < 0)
		return -1;

	endp = idx->rec + index_rec_get_total_size(idx->rec);
	modp = idx->rec + 1 + strlen(brief) + 1;

	// we assert here that it's possible to convert any short index
	// entry to an extended entry in place, assuming the brief stays
	// the same. the 16-bit pointer would take up the same amount of
	// space as a blank username and password.

	assert(endp - modp >= 2);
	set_uint16(modp, 0, blob);
	modp += 2;

	// shave off the unused part of the new record
	if (modp < endp) {
		buf_deallocate_record(idx->buf, modp,
				      endp - modp);
	}

	index_rec_set_extended(idx->rec, 1);
	index_rec_set_size(idx->rec, modp - idx->rec - 1);
	accdb_cache_dirty(idx->buf);

	return 0;
}

int8_t
accdb_add_note(struct accdb_index *idx, void *data, size_t size)
{
	uint16_t blob = 0;
	const char *brief;
	uint8_t *buf, *note;

	if (!accdb_index_has_entry(idx))
		return -1;
	if (size > REC_BLOB_ENTRY_MAX)
		return -1;
	if (index_rec_get_extended(idx->rec) == 0 &&
	    convert_to_extended(idx) < 0)
		return -1;
	if (index_rec_parse_extended(idx->rec, &brief, &blob) < 0)
		return -1;
	
	assert(blob != 0);	
	// search for free spot in blob list for new note
	note = NULL;
	buf = NULL;
	while (!note && blob) {
		if (buf)
			accdb_cache_put(buf);
		buf = accdb_cache_get(idx->db, blob);
		if (buf == NULL)
			return -1;
		note = buf_allocate_record(buf, size + 2);
		blob = buf_get_next(buf);
	}

	// no space, create new blob
	if (!note) {
		uint8_t *tmp;

		assert(buf != NULL);
		tmp = accdb_allocate_buf(idx->db, SECT_TYPE_BLOB);
		if (!tmp)
			return -1;
		if (accdb_list_append(idx->db, buf, tmp) < 0) {
			accdb_deallocate_buf(idx->db, tmp);
			return -1;
		}
		note = buf_allocate_record(tmp, size + 2);
		if (note == NULL) {
			accdb_deallocate_buf(idx->db, tmp);
			return -1;
		}
		accdb_cache_put_dirty(buf);
		buf = tmp;
	}
	
	assert(buf != NULL);
	if (blob_rec_create(note, REC_BLOB_NOTE, data, size) < 0)
		return -1;
	accdb_cache_put_dirty(buf);

	return 0;
}

int8_t
accdb_index_next_note(struct accdb_index *idx, const void **note, size_t *size)
{
	uint8_t *rec, *end;

	if (!index_rec_get_extended(idx->rec))
		return -1;

	if (idx->blob_note == NULL || *note == NULL) {
		const char *brief;
		uint16_t blob = 0;
		*note = NULL;
		if (index_rec_parse_extended(idx->rec, &brief, &blob) < 0)
			return -1;
		if (idx->blob_note)
			accdb_cache_put(idx->blob_note);
		idx->blob_note = blob_list_find(idx->db, note, size, blob,
						REC_BLOB_NOTE);
		return 0;
	}

	assert(idx->blob_note);
	rec = ((uint8_t *)*note) - 2;
	end = buf_get_end(idx->blob_note);
	if (rec < buf_get_payload(idx->blob_note) || rec >= end)
		return -1;

	blob_rec_next(&rec);
	if (rec >= end) {
		*note = NULL;
		accdb_cache_put(idx->blob_note);
		idx->blob_note = blob_list_find(idx->db, note, size,
						buf_get_next(idx->blob_note),
						REC_BLOB_NOTE);
	} else {
		// username and password blobs are always the first
		// to appear in the blob list, so if this is not the
		// case, something's wrong.
		if (blob_rec_get_type(rec) != REC_BLOB_NOTE)
			return -1;
		*note = blob_rec_get_data(rec);
		*size = blob_rec_get_size(rec);
	}

	return 0;
}

static int8_t
chuser_in_place(struct accdb_index *idx, const char *user, const char *pass)
{
}

int8_t
accdb_chuser(struct accdb_index *idx, const char *new_user)
{
	return 0;
}

int8_t
accdb_chpass(struct accdb_index *idx, const char *new_pass)
{
	return 0;
}


// note: the index is moved to the next available record.
// the rules for adjustment are:
//	1. if record(s) exist after the removed record in the same page,
//	   nothing is done, except to move the record pointer to the next
//	   record.
//	2. if the removed record is the last on the page, and there is a
//	   next page, the index is advanced to the first record on the next
//	   page.
//	3. if the removed record is the last on the page, and there are
//	   no more pages in the index, the index points to the end of the
//	   of that final page.
//	
int8_t
accdb_del(struct accdb_index *idx)
{
	struct accdb *db;
	uint8_t *buf, *rec;
	uint16_t next;
	uint16_t prev;

	if (!accdb_index_has_entry(idx))
		return -1;

	db = idx->db;
	buf = accdb_cache_ref(idx->buf);
	rec = idx->rec;
	accdb_index_clear(idx);
	next = buf_get_next(buf);
	prev = buf_get_prev(buf);

	if (index_rec_get_extended(rec)) {
		const char *brief;
		uint16_t ptr;

		if (index_rec_parse_extended(rec, &brief, &ptr) < 0)
			return -1;
		if (accdb_list_clear(db, ptr) < 0)
			return -1;
	}
	
	buf_deallocate_record(buf, rec, index_rec_get_total_size(rec));

	if (buf_get_size(buf) == 0 && prev != 0) {
		// remove this index page if it's empty (and not INDEX_START)
		assert(accdb_cache_sector(buf) != INDEX_START(db));
		if (accdb_list_remove(db, buf) < 0)
			return -1;
		if (accdb_deallocate_buf(db, buf) < 0)
			return -1;

		if (next) {
			buf = accdb_cache_get(db, next);
			if (buf == NULL)
				return -1;
			rec = buf_get_payload(buf);
		} else {
			assert(prev);
			buf = accdb_cache_get(db, prev);
			if (buf == NULL)
				return -1;
			rec = buf_get_end(buf);
		}
	} else if (rec >= buf_get_end(buf) && next) {
		accdb_cache_put(buf);
		buf = accdb_cache_get(db, next);
		if (!buf)
			return -1;
		rec = buf_get_payload(buf);
	}

	// reconstruct the index
	idx->db = db;
	idx->buf = buf;
	idx->rec = rec;

	return 0;
}

int8_t
accdb_open(struct accdb *db, struct file *fp, struct pool *pool)
{
	db->fp = fp;
	db->strategy = CACHE_MRU;
	db->pool = pool;
	db->cleanup.cb = (try_free_cb)accdb_cache_do_cleanup;
	db->cleanup.arg = db;
	pool_add_cleanup(pool, &db->cleanup);
	accdb_cache_init(db);
	db->sect_start = vfs_start_sector(fp);
	
	return 0;
}

int8_t
accdb_close(struct accdb *db)
{
	size_t i;

	if (accdb_cache_clear(db) < 0)
		return -1;

	pool_del_cleanup(db->pool, &db->cleanup);

	for (i = 0; i < ACCDB_CACHE_SIZE; ++i) {
		struct sector *s = &db->cache[i];
		if (s->buf)
			pool_deallocate_block(db->pool, s->buf);
	}

	return 0;
}

//// Self-Tests ////

void
test_accdb(struct accdb *db)
{
	size_t i;
	char brief[32], user[32], pass[32];
	char *bigbuf;
	const char *briefp, *userp, *passp;
	struct accdb_index idx;
	const void *note;
	size_t size;

	bigbuf = (char*)pool_allocate_block(db->pool, NULL);
	v_assert(bigbuf != NULL);

	memset(&idx, 0, sizeof(struct accdb_index));
	
	for (i = 0; i < 128; ++i) {
		sprintf(brief, "BRIEF%u", (unsigned)i);
		sprintf(user, "USER%u", (unsigned)i);
		sprintf(pass, "PASS%u", (unsigned)i);
		v_assert(accdb_add(db, &idx, brief, user, pass) == 0);
		v_assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		v_assert(strcmp(brief, briefp) == 0);
		v_assert(strcmp(user, userp) == 0);
		v_assert(strcmp(pass, passp) == 0);
		//accdb_cache_print(db, 1);
	}

	v_assert(accdb_index_init(db, &idx) == 0);

	i = 0;
	while (accdb_index_has_entry(&idx)) {
		sprintf(brief, "BRIEF%u", (unsigned)i);
		sprintf(user, "USER%u", (unsigned)i);
		sprintf(pass, "PASS%u", (unsigned)i);
		v_assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		//printf("%s, %s, %s\n", briefp, userp, passp);
		v_assert(strcmp(brief, briefp) == 0);
		v_assert(strcmp(user, userp) == 0);
		v_assert(strcmp(pass, passp) == 0);
		v_assert(accdb_index_next(&idx) == 0);
		++i;
	}
	v_assert(i == 128);

	i = 128;
	v_assert(accdb_index_prev(&idx) == 0);
	while (accdb_index_has_entry(&idx)) {
		v_assert(i > 0);
		--i;
		sprintf(brief, "BRIEF%u", (unsigned)i);
		sprintf(user, "USER%u", (unsigned)i);
		sprintf(pass, "PASS%u", (unsigned)i);
		v_assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		//printf("%s, %s, %s\n", briefp, userp, passp);
		v_assert(strcmp(brief, briefp) == 0);
		v_assert(strcmp(user, userp) == 0);
		v_assert(strcmp(pass, passp) == 0);
		v_assert(accdb_index_prev(&idx) == 0);
	}
	v_assert(i == 0);

	v_assert(accdb_index_init(db, &idx) == 0);

	i = 0;
	while (accdb_index_has_entry(&idx)) {
		v_assert(accdb_del(&idx) == 0);
		if (++i == 64)
			break;
	}
	v_assert(accdb_index_init(db, &idx) == 0);
	while (accdb_index_has_entry(&idx)) {
		sprintf(brief, "BRIEF%u", (unsigned)i);
		sprintf(user, "USER%u", (unsigned)i);
		sprintf(pass, "PASS%u", (unsigned)i);
		v_assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		//printf("%s, %s, %s\n", briefp, userp, passp);
		v_assert(strcmp(brief, briefp) == 0);
		v_assert(strcmp(user, userp) == 0);
		v_assert(strcmp(pass, passp) == 0);
		v_assert(accdb_index_next(&idx) == 0);
		++i;
	}
	v_assert(i == 128);

	v_assert(accdb_index_prev(&idx) == 0);
	v_assert(accdb_del(&idx) == 0);
	v_assert(accdb_index_prev(&idx) == 0);
	v_assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
	//printf("%s, %s, %s\n", briefp, userp, passp);

	memset(bigbuf, 'B', REC_BLOB_ENTRY_MAX);
	v_assert(accdb_index_init(db, &idx) == 0);
	v_assert(accdb_add_note(&idx, bigbuf, REC_BLOB_ENTRY_MAX) == 0);
	v_assert(accdb_add_note(&idx, bigbuf, 130) == 0);
	v_assert(accdb_add_note(&idx, bigbuf, 20) == 0);
	v_assert(accdb_add_note(&idx, bigbuf, REC_BLOB_ENTRY_MAX) == 0);
	
	v_assert(accdb_index_init(db, &idx) == 0);
	i = 64;
	while (accdb_index_has_entry(&idx)) {
		sprintf(brief, "BRIEF%u", (unsigned)i);
		sprintf(user, "USER%u", (unsigned)i);
		sprintf(pass, "PASS%u", (unsigned)i);
		v_assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		//printf("%s, %s, %s\n", briefp, userp, passp);
		v_assert(strcmp(brief, briefp) == 0);
		v_assert(strcmp(user, userp) == 0);
		v_assert(strcmp(pass, passp) == 0);
		v_assert(accdb_index_next(&idx) == 0);
		++i;
	}
	v_assert(i == 127);

	v_assert(accdb_index_init(db, &idx) == 0);
	note = NULL;
	i = 0;
	while (1) {
		v_assert(accdb_index_next_note(&idx, &note, &size) == 0);
		if (note == NULL)
			break;
		v_assert(size <= REC_BLOB_ENTRY_MAX);
		v_assert(memcmp(note, bigbuf, size) == 0);
		++i;
	}
	v_assert(i == 4);

	memset(bigbuf, 'B', 130);
	bigbuf[129] = '\0';

	v_assert(accdb_add(db, &idx, "blob brief", "blob usr", bigbuf) == 0);
	v_assert(accdb_add(db, &idx, "blob brief", "blob usr", bigbuf) == 0);
	v_assert(accdb_add(db, &idx, "blob brief", "blob usr", bigbuf) == 0);

	v_assert(accdb_index_init(db, &idx) == 0);

	i = 0;
	while (accdb_index_has_entry(&idx)) {
		v_assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		if (!strcmp(briefp, "blob brief")) {
			//printf("%s, %s, '%s'\n", briefp, userp, passp);
			v_assert(strcmp("blob usr", userp) == 0);
			v_assert(strcmp(bigbuf, passp) == 0);
			++i;
		}
		v_assert(accdb_index_next(&idx) == 0);
	}
	v_assert(i == 3);

	v_assert(accdb_add(db, &idx, "blob all", bigbuf, bigbuf) == 0);
	v_assert(accdb_index_init(db, &idx) == 0);

	i = 0;
	while (accdb_index_has_entry(&idx)) {
		v_assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		if (!strcmp(briefp, "blob all")) {
			if (strcmp(bigbuf, userp) != 0) {
				logf((_P("?????userp == '%s'\n"), userp));
				logf((_P("?????passp == '%s'\n"), passp));
				v_assert(0);
			}
			if (strcmp(bigbuf, passp) != 0) {
				logf((_P("?????userp == '%s'\n"), userp));
				logf((_P("?????passp == '%s'\n"), passp));
				v_assert(0);
			}
			/*
			v_assert(strcmp(bigbuf, userp) == 0);
			v_assert(strcmp(bigbuf, passp) == 0);
			*/
			++i;
		}
		v_assert(accdb_index_next(&idx) == 0);
	}
	v_assert(i == 1);

	memset(bigbuf, 'B', REC_BLOB_ENTRY_MAX);
	bigbuf[REC_BLOB_ENTRY_MAX - 1] = '\0';

	v_assert(accdb_add(db, &idx, "blob giant", bigbuf, bigbuf) == 0);
	v_assert(accdb_cache_clear(db) == 0);
	v_assert(accdb_index_init(db, &idx) == 0);

	i = 0;
	while (accdb_index_has_entry(&idx)) {
		v_assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		if (!strcmp(briefp, "blob giant")) {
			v_assert(strcmp(bigbuf, userp) == 0);
			v_assert(strcmp(bigbuf, passp) == 0);
			++i;
		}
		v_assert(accdb_index_next(&idx) == 0);
	}
	v_assert(i == 1);

	v_assert(accdb_index_init(db, &idx) == 0);
	while (accdb_index_has_entry(&idx)) {
		v_assert(accdb_del(&idx) == 0);
	}

	accdb_index_clear(&idx);
	pool_deallocate_block(db->pool, (uint8_t *)bigbuf);
}

void
test_accdb_rec(struct accdb *db)
{
	uint8_t rec[132];
	char bigbuf[130];
	const char *brief = "BRIEF";
	const char *user = "USER";
	const char *pass = "PASS";
	const char *briefp, *userp, *passp;
	uint16_t ptr = 0xbeef, ptrp;
	uint8_t len;

	memset(bigbuf, 'B', sizeof(bigbuf) - 1);
	bigbuf[sizeof(bigbuf) - 1] = '\0';

	v_assert(index_rec_find_type(brief, user, pass, &len) == INDEX_NORMAL);
	v_assert(len == strlen(brief) + strlen(user) + strlen(pass) + 4);

	v_assert(index_rec_find_type(brief, bigbuf, pass, &len) == INDEX_EXT);
	v_assert(len == strlen(brief) + 4);

	v_assert(index_rec_find_type(bigbuf, user, pass, &len) == INDEX_INVALID);

	memset(rec, 0, sizeof(rec));
	
	v_assert(index_rec_create(rec, brief, user, pass) == 0);
	v_assert(index_rec_parse(rec, &briefp, &userp, &passp) == 0);
	v_assert(index_rec_get_extended(rec) == 0);
	v_assert(strcmp(briefp, brief) == 0);
	v_assert(strcmp(userp, user) == 0);
	v_assert(strcmp(passp, pass) == 0);

	memset(rec, 0, sizeof(rec));

	v_assert(index_rec_create_extended(rec, brief, ptr) == 0);
	v_assert(index_rec_parse_extended(rec, &briefp, &ptrp) == 0);
	v_assert(index_rec_get_extended(rec) == 1);
	v_assert(strcmp(briefp, brief) == 0);
	v_assert(ptrp == ptr);

	memset(rec, 0, sizeof(rec));
	v_assert(blob_rec_create(rec, REC_BLOB_NOTE, bigbuf, sizeof(bigbuf)) == 0);
	v_assert(blob_rec_get_type(rec) == REC_BLOB_NOTE);
	v_assert(blob_rec_get_size(rec) == sizeof(bigbuf));
	v_assert(memcmp(blob_rec_get_data(rec), bigbuf, sizeof(bigbuf)) == 0);
}

void
test_accdb_allocation(struct accdb *db)
{
	uint8_t *b[3];
	uint16_t s[3];
	struct sector *p;
	unsigned i;

#if 0
	scratch = accdb_cache_get_scratch(db);
	v_assert(scratch != NULL);
	p = GET_SECTOR(scratch);
	v_assert(p->buf == scratch);
	v_assert(p->empty == 0);
	v_assert(p->refcount == 1);
	v_assert(p->scratch == 1);
	v_assert(accdb_deallocate_buf(db, scratch) == 0);
#endif

	v_assert(accdb_cache_flush(db) == 0);

	for (i = 0; i < 3; ++i) { 
		b[i] = accdb_allocate_buf(db, SECT_TYPE_BLOB);
		v_assert(b[i] != NULL);
		v_assert(buf_get_type(b[i]) == SECT_TYPE_BLOB);
		p = GET_SECTOR(b[i]);
		s[i] = p->sector;
		v_assert(p->buf == b[i]);
		v_assert(p->refcount == 1);
	}

	for (i = 0; i < 3; ++i)
		v_assert(accdb_deallocate_buf(db, b[i]) == 0);

	// with the allocator as it is, we should get the same sectors
	// after freeing before.
	for (i = 0; i < 3; ++i) { 
		b[i] = accdb_allocate_buf(db, SECT_TYPE_BLOB);
		v_assert(b[i] != NULL);
		v_assert(buf_get_type(b[i]) == SECT_TYPE_BLOB);
		p = GET_SECTOR(b[i]);
		v_assert(s[i] == p->sector);
		v_assert(p->buf == b[i]);
		v_assert(p->refcount == 1);
	}

	for (i = 0; i < 3; ++i)
		v_assert(accdb_deallocate_buf(db, b[i]) == 0);
}

void
test_accdb_buffer(struct accdb *db)
{
	uint8_t *buf;
	char tmp[128];
	const char *a1 = "this is the first one:";
	const char *a2 = "this is the second one:";
	const char *a3 = "Groundhog Day babe:";
	uint8_t *b1, *b2, *b3;
	size_t s1, s2, s3;

	buf = accdb_allocate_buf(db, SECT_TYPE_BLOB);
	v_assert(buf != NULL);
	v_assert(buf_get_type(buf) == SECT_TYPE_BLOB);
	v_assert(buf_get_size(buf) == 0);
#if 0
	buf_set_type_size(buf, SECT_TYPE_BLOB, 0);
#endif
	s1 = strlen(a1);
	s2 = strlen(a2);
	s3 = strlen(a3);
	memset(tmp, 0, sizeof(tmp));

	b1 = buf_allocate_record(buf, s1);
	v_assert(b1 != NULL);
	memcpy(b1, a1, s1);
	strcat(tmp, a1);
	v_assert(buf_get_size(buf) == strlen(tmp));
	v_assert(memcmp(buf_get_payload(buf), tmp, strlen(tmp)) == 0);

	b2 = buf_allocate_record(buf, s2);
	v_assert(b2 != NULL);
	memcpy(b2, a2, s2);
	strcat(tmp, a2);
	v_assert(buf_get_size(buf) == strlen(tmp));
	v_assert(memcmp(buf_get_payload(buf), tmp, strlen(tmp)) == 0);

	b3 = buf_allocate_record(buf, s3);
	v_assert(b3 != NULL);
	memcpy(b3, a3, s3);
	strcat(tmp, a3);
	v_assert(buf_get_size(buf) == strlen(tmp));
	v_assert(memcmp(buf_get_payload(buf), tmp, strlen(tmp)) == 0);

	tmp[0] = '\0';
	strcat(tmp, a1);
	strcat(tmp, a3);
	buf_deallocate_record(buf, b2, s2);
	v_assert(buf_get_size(buf) == strlen(tmp));
	v_assert(memcmp(buf_get_payload(buf), tmp, strlen(tmp)) == 0);

	tmp[0] = '\0';
	strcat(tmp, a3);
	buf_deallocate_record(buf, b1, s1);
	v_assert(buf_get_size(buf) == strlen(tmp));
	v_assert(memcmp(buf_get_payload(buf), tmp, strlen(tmp)) == 0);

	tmp[0] = '\0';
	// prior memmoves invalidates b3
	buf_deallocate_section(buf, 0, s3); 
	v_assert(buf_get_size(buf) == 0);
	
	accdb_deallocate_buf(db, buf);
}

void
test_accdb_list(struct accdb *db)
{
	uint8_t *head, *buf[5], *record;
	unsigned i;
	uint16_t next, head_save;
	char tmp[32];

	head = accdb_allocate_buf(db, SECT_TYPE_BLOB);
	v_assert(head != NULL);

	buf_set_prev(head, 0xdead);
	v_assert(buf_get_prev(head) == 0xdead);
	buf_set_prev(head, 0);

	buf_set_next(head, 0xbeef);
	v_assert(buf_get_next(head) == 0xbeef);
	buf_set_next(head, 0);

	record = buf_allocate_record(head, 4);
	v_assert(record != NULL);
	memcpy(record, "HEAD", 4);
	v_assert(buf_get_size(head) == 4);
	v_assert(memcmp(buf_get_payload(head), "HEAD", 4) == 0);

	for (i = 0; i < 5; ++i) {
		buf[i] = accdb_allocate_buf(db, SECT_TYPE_BLOB);
		v_assert(buf[i] != NULL);
		sprintf(tmp, "SECT %u", i);
		record = buf_allocate_record(buf[i], strlen(tmp));
		v_assert(record != NULL);
		memcpy(record, tmp, strlen(tmp));
		v_assert(accdb_list_append(db, head, buf[i]) == 0);
	}
#if 0
	printf("1. HEAD: 0x%x (next 0x%x, prev 0x%x)\n",
		accdb_cache_sector(head), buf_get_next(head),
		buf_get_prev(head));
	
	for (i = 0; i < 5; ++i) {
		printf(" SECT %u: 0x%x (next 0x%x, prev 0x%x)\n",
			i, accdb_cache_sector(buf[i]),
			buf_get_next(buf[i]),
			buf_get_prev(buf[i]));
	}
#endif
	v_assert(accdb_cache_flush(db) == 0);

	v_assert(buf_get_prev(head) == SECT_NULL);
	v_assert(buf_get_next(head) == accdb_cache_sector(buf[4]));
	v_assert(buf_get_prev(buf[4]) == accdb_cache_sector(head));
	v_assert(buf_get_next(buf[4]) == accdb_cache_sector(buf[3]));
	v_assert(buf_get_prev(buf[3]) == accdb_cache_sector(buf[4]));
	v_assert(buf_get_next(buf[3]) == accdb_cache_sector(buf[2]));
	v_assert(buf_get_prev(buf[2]) == accdb_cache_sector(buf[3]));
	v_assert(buf_get_next(buf[2]) == accdb_cache_sector(buf[1]));
	v_assert(buf_get_prev(buf[1]) == accdb_cache_sector(buf[2]));
	v_assert(buf_get_next(buf[1]) == accdb_cache_sector(buf[0]));
	v_assert(buf_get_prev(buf[0]) == accdb_cache_sector(buf[1]));
	v_assert(buf_get_next(buf[0]) == SECT_NULL);

	v_assert(accdb_cache_clear(db) == 0);

	head_save = accdb_cache_sector(head);
	next = buf_get_next(head);
	accdb_cache_put(head);
	i = 0;
	while (!SECT_IS_NULL(next)) {
		head = accdb_cache_get(db, next);
		v_assert(head != NULL);
		next = buf_get_next(head);
		accdb_cache_put(head);
		i++;
	}
	v_assert(i == 5);

	for (i = 0; i < 5; ++i)
		accdb_cache_put(buf[i]);

	v_assert(accdb_list_clear(db, head_save) == 0);
}

// run this on a freshly opened DB
void
test_accdb_cache_list(struct accdb *db)
{
	struct sector *s;
	size_t i;

	for (i = 0, s = db->mru; s; ++i, s = s->next) {
		v_assert(i < ACCDB_CACHE_SIZE);
		v_assert(&db->cache[i] == s);
		v_assert(s->empty == 1);
	}
	v_assert(i == ACCDB_CACHE_SIZE);
	for (i = 0, s = db->lru; s; ++i, s = s->prev) {
		v_assert(i < ACCDB_CACHE_SIZE);
		v_assert(s->empty == 1);
	}
	v_assert(i == ACCDB_CACHE_SIZE);
	v_assert(&db->cache[i - 1] == db->lru);

	accdb_cache_mru(db, &db->cache[2]);
	for (i = 0, s = db->mru; s; ++i, s = s->next) {
		v_assert(i < ACCDB_CACHE_SIZE);
		v_assert(s->empty == 1);
	}
	v_assert(i == ACCDB_CACHE_SIZE);
	for (i = 0, s = db->lru; s; ++i, s = s->prev) {
		v_assert(i < ACCDB_CACHE_SIZE);
		v_assert(s->empty == 1);
	}
	v_assert(i == ACCDB_CACHE_SIZE);
	s = &db->cache[2];
	v_assert(s->prev == NULL);
	v_assert(db->mru == s);

	accdb_cache_lru(db, &db->cache[3]);
	for (i = 0, s = db->mru; s; ++i, s = s->next) {
		v_assert(i < ACCDB_CACHE_SIZE);
		v_assert(s->empty == 1);
	}
	v_assert(i == ACCDB_CACHE_SIZE);
	for (i = 0, s = db->lru; s; ++i, s = s->prev) {
		v_assert(i < ACCDB_CACHE_SIZE);
		v_assert(s->empty == 1);
	}
	v_assert(i == ACCDB_CACHE_SIZE);
	s = &db->cache[3];
	v_assert(s->next == NULL);
	v_assert(db->lru == s);
}

static void
test_accdb_run_all(struct accdb *db)
{
	logf((_P("\n\nCache list:\n")));
	test_accdb_cache_list(db);
	logf((_P("OK\n")));

	v_assert(accdb_format(db) == 0);

	logf((_P("\nIndex records:\n")));
	test_accdb_rec(db);
	logf((_P("OK\n")));

	logf((_P("\n\nAllocation:\n")));
	test_accdb_allocation(db);
	logf((_P("OK\n")));

	logf((_P("\n\nBuffers:\n")));
	test_accdb_buffer(db);
	logf((_P("OK\n")));

	logf((_P("\n\nLists:\n")));
	test_accdb_list(db);
	logf((_P("OK\n")));

	logf((_P("\n\nDB:\n")));
	test_accdb(db);
	logf((_P("OK\n")));
}

void
test_accdb_plaintext(const struct vfs *meth, struct pool *pool)
{
	struct file fp;
	struct accdb db;

	logf((_P("\n---Plain---\n")));
	v_assert(vfs_open(meth, &fp, "test.db", VFS_RW) == 0);
	accdb_open(&db, &fp, pool);

	test_accdb_run_all(&db);

	v_assert(accdb_cache_flush(&db) == 0);
	accdb_cache_print(&db, 0);
	accdb_close(&db);
	vfs_close(&fp);
}

#include "crypto.h"
#include "vfs_crypt.h"
#define TEST_PW "oogabooganooga"
#define TEST_PW_LEN 14
#define TEST_POOL_SZ (ACCDB_CACHE_SIZE + 2)

void
test_accdb_crypt(const struct vfs *meth, struct pool *pool)
{
	struct file fp;
	struct accdb db;

	printf("\n---Encrypted---\n");
	v_assert(vfs_open(meth, &fp, "crypt.db", VFS_RW) == 0);
	v_assert(vfs_crypt_init(&fp, pool) == 0);
	v_assert(vfs_crypt_format(&fp, TEST_PW, TEST_PW_LEN) == 0);
	vfs_close(&fp);

	v_assert(vfs_open(meth, &fp, "crypt.db", VFS_RW) == 0);
	v_assert(vfs_crypt_init(&fp, pool) == 0);
	v_assert(vfs_crypt_unlock(&fp, TEST_PW, TEST_PW_LEN) == 0);

	accdb_open(&db, &fp, pool);

	test_accdb_run_all(&db);

	v_assert(accdb_cache_flush(&db) == 0);
	accdb_cache_print(&db, 0);
	accdb_close(&db);
	vfs_close(&fp);
}

#if 0
int
main(void)
{
	struct pool *p;

	p = pool_init(TEST_POOL_SZ);
	v_assert(p);

	crypto_init();
	test_accdb_plaintext(p);
	test_accdb_crypt(p);

	return 0;
}
#endif
