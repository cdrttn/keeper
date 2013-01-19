#include <stddef.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "accdb.h"
#include "vfs.h"

struct sector {
	uint8_t dirty:1;
	uint8_t empty:1;
	uint8_t scratch:1;
	uint16_t refcount;
	uint16_t sector;
	uint8_t buf[VFS_SECT_SIZE];
};

struct accdb {
	struct file *fp;
#define ACCDB_CACHE_SIZE 8
	struct sector cache[ACCDB_CACHE_SIZE];
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

#define BITMAP_START		0
#define BITMAP_MAX		16
#define INDEX_START		BITMAP_MAX
#define SECT_NULL 		0
#define ACCDB_MAX		0xffff
#define SECT_IS_NULL(i)		((i) == SECT_NULL)
#define SECT_PER_BITMAP		(VFS_SECT_SIZE * 8)
#define BITMAP_OFFSET_TO_SECT(bsect, bbyte, bit)	\
	(bsect * SECT_PER_BITMAP + bbyte * 8 + bit)
#define GET_SECTOR(b) \
	((struct sector *)((b) - offsetof(struct sector, buf)))

static inline void accdb_cache_dirty(uint8_t *buf);
static inline uint16_t buf_get_size(const uint8_t *sector);
static inline uint8_t buf_get_type(const uint8_t *sector);
static inline uint16_t buf_get_next(const uint8_t *sector);
static inline uint16_t buf_get_prev(const uint8_t *sector);

#ifdef DBGPRINT
#define LOG(x) printf x
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


static inline uint16_t 
get_uint16(const uint8_t *buf, uint16_t pos)
{
	return (buf[pos] << 8) | buf[pos + 1];
}

static inline uint32_t 
get_uint32(const uint8_t *buf, uint16_t pos)
{
	return ((buf[pos] << 24) | (buf[pos + 1] << 16) |
		(buf[pos + 2] << 8) | buf[pos + 3]);
} 

static inline void 
set_uint16(uint8_t *buf, uint16_t pos, uint16_t b)
{
	buf[pos] = (b >> 8) & 0xff;
	buf[pos + 1] = b & 0xff;
}

static inline void 
set_uint32(uint8_t *buf, uint16_t pos, uint32_t b)
{
	buf[pos] = (b >> 24) & 0xff;
	buf[pos + 1] = (b >> 16) & 0xff;
	buf[pos + 2] = (b >> 8) & 0xff;
	buf[pos + 3] = b & 0xff;
}

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

static void
accdb_cache_print(struct accdb *db, int quiet)
{
	unsigned i;
	int avail = 0;

	printf("-------------------\n");
	for (i = 0; i < ACCDB_CACHE_SIZE; ++i) {
		if (!quiet) {
			printf("cache %u:\n", i);
			printf(" dirty = %d\n", (int)db->cache[i].dirty);
			printf(" empty = %d\n", (int)db->cache[i].empty);
			printf(" scratch = %d\n", (int)db->cache[i].scratch);
			printf(" refcount = %d\n", (int)db->cache[i].refcount);
			printf(" sector = %d\n\n", (int)db->cache[i].sector);
		}
		if (!db->cache[i].refcount)
			avail++;
	}
	printf("available, %d/%d\n", avail, ACCDB_CACHE_SIZE);
	printf("-------------------\n");
}

static int8_t
accdb_cache_flush_one(struct accdb *db, struct sector *sector)
{
	int8_t rv = 0;

	if (sector->dirty) {
		if (!sector->scratch) {
			rv = vfs_write_sector(db->fp, sector->buf,
					      sector->sector);
			LOG(("WRITE %s\n", _s(sector->buf)));
		}
		if (rv == 0)
			sector->dirty = 0;
	}

	return rv;
}

static uint8_t *
accdb_cache_get(struct accdb *db, uint16_t sector)
{
	size_t i;

	// is sector in cache?	
	for (i = 0; i < ACCDB_CACHE_SIZE; ++i) {
		if (db->cache[i].sector == sector &&
		    db->cache[i].empty == 0 &&
		    db->cache[i].scratch == 0) {
			db->cache[i].refcount++;
			LOG(("REF %s\n", _s(db->cache[i].buf)));
			return db->cache[i].buf;
		}	
	}

	// no. find a free spot and load from file.
	// XXX change this to prefer unused slots ?
	for (i = 0; i < ACCDB_CACHE_SIZE; ++i) {
		if (db->cache[i].refcount == 0) {
			if (accdb_cache_flush_one(db, &db->cache[i]) < 0)
				return NULL;
			if (vfs_read_sector(db->fp, db->cache[i].buf,
				            sector) < 0)
				return NULL;
			db->cache[i].empty = 0;
			db->cache[i].sector = sector;
			db->cache[i].scratch = 0;
			db->cache[i].refcount = 1;
			LOG(("READ %s\n", _s(db->cache[i].buf)));
			return db->cache[i].buf;
		}
	}

	// no space in cache! out of memory!
	return NULL;
}

static uint8_t *
accdb_cache_get_scratch(struct accdb *db)
{
	size_t i;

	// find an unclaimed buffer for scratch space

	for (i = 0; i < ACCDB_CACHE_SIZE; ++i) {
		if (db->cache[i].refcount == 0) {
			if (accdb_cache_flush_one(db, &db->cache[i]) < 0)
				return NULL;
			db->cache[i].empty = 0;
			db->cache[i].sector = 0;
			db->cache[i].scratch = 1;
			db->cache[i].refcount = 1;
			return db->cache[i].buf;
		}
	}

	return NULL;
}

static int8_t
accdb_cache_flush(struct accdb *db)
{
	size_t i;
	
	for (i = 0; i < ACCDB_CACHE_SIZE; ++i) {
		if (accdb_cache_flush_one(db, &db->cache[i]) < 0)
			return -1;
	}

	return 0;
}

static uint8_t
accdb_cache_clear(struct accdb *db)
{
	size_t i;

	if (accdb_cache_flush(db) < 0)
		return -1;

	for (i = 0; i < ACCDB_CACHE_SIZE; ++i) {
		if (db->cache[i].refcount == 0) {
			memset(&db->cache[i], 0, sizeof(struct sector));
			db->cache[i].empty = 1;
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
	for (sector = BITMAP_START; sector < BITMAP_MAX; ++sector) {
		buf = accdb_cache_get(db, sector);
		if (!buf)
			return -1;
		for (byte = 0; byte < VFS_SECT_SIZE; ++byte) {
			for (bit = 0; bit < 8; ++bit) {
				if (!(buf[byte] & (1 << bit))) {
					*free_sect = BITMAP_OFFSET_TO_SECT(sector,
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
	uint16_t bitmap = sect / SECT_PER_BITMAP;
	uint16_t offset = sect % SECT_PER_BITMAP;
	uint16_t byte = offset / 8;
	uint8_t bit = offset % 8;
	uint8_t *buf;

	if (sect < BITMAP_MAX)
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
	if (!s->scratch)
		return accdb_deallocate_sector(db, s->sector);
	return 0;
}

static int8_t
accdb_format(struct accdb *db)
{
	uint16_t i;
	uint8_t *buf;

	for (i = BITMAP_START; i < BITMAP_MAX; ++i) {
		buf = accdb_cache_get(db, i);
		if (buf == NULL)
			return -1;
		memset(buf, 0, VFS_SECT_SIZE);
		if (i == 0) {
			// mark the first 16 bitmap bits, so the sectors
			// of the bitmap aren't written over.
			buf[0] = 0xff;
			buf[1] = 0xff;
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
	idx->buf = accdb_cache_get(db, INDEX_START);
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
		assert(accdb_cache_sector(buf) != INDEX_START);
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
accdb_open(struct accdb *db, struct file *fp)
{
	size_t i;

	db->fp = fp;
	for (i = 0; i < ACCDB_CACHE_SIZE; ++i) {
		memset(&db->cache[i], 0, sizeof(struct sector));
		db->cache[i].empty = 1;
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

	bigbuf = (char*)accdb_cache_get_scratch(db);
	assert(bigbuf != NULL);

	memset(&idx, 0, sizeof(struct accdb_index));
	
	for (i = 0; i < 128; ++i) {
		sprintf(brief, "BRIEF%u", (unsigned)i);
		sprintf(user, "USER%u", (unsigned)i);
		sprintf(pass, "PASS%u", (unsigned)i);
		assert(accdb_add(db, &idx, brief, user, pass) == 0);
		assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		assert(strcmp(brief, briefp) == 0);
		assert(strcmp(user, userp) == 0);
		assert(strcmp(pass, passp) == 0);
		//accdb_cache_print(db, 1);
	}

	assert(accdb_index_init(db, &idx) == 0);

	i = 0;
	while (accdb_index_has_entry(&idx)) {
		sprintf(brief, "BRIEF%u", (unsigned)i);
		sprintf(user, "USER%u", (unsigned)i);
		sprintf(pass, "PASS%u", (unsigned)i);
		assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		//printf("%s, %s, %s\n", briefp, userp, passp);
		assert(strcmp(brief, briefp) == 0);
		assert(strcmp(user, userp) == 0);
		assert(strcmp(pass, passp) == 0);
		assert(accdb_index_next(&idx) == 0);
		++i;
	}
	assert(i == 128);

	i = 128;
	assert(accdb_index_prev(&idx) == 0);
	while (accdb_index_has_entry(&idx)) {
		assert(i > 0);
		--i;
		sprintf(brief, "BRIEF%u", (unsigned)i);
		sprintf(user, "USER%u", (unsigned)i);
		sprintf(pass, "PASS%u", (unsigned)i);
		assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		//printf("%s, %s, %s\n", briefp, userp, passp);
		assert(strcmp(brief, briefp) == 0);
		assert(strcmp(user, userp) == 0);
		assert(strcmp(pass, passp) == 0);
		assert(accdb_index_prev(&idx) == 0);
	}
	assert(i == 0);

	assert(accdb_index_init(db, &idx) == 0);

	i = 0;
	while (accdb_index_has_entry(&idx)) {
		assert(accdb_del(&idx) == 0);
		if (++i == 64)
			break;
	}
	assert(accdb_index_init(db, &idx) == 0);
	while (accdb_index_has_entry(&idx)) {
		sprintf(brief, "BRIEF%u", (unsigned)i);
		sprintf(user, "USER%u", (unsigned)i);
		sprintf(pass, "PASS%u", (unsigned)i);
		assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		//printf("%s, %s, %s\n", briefp, userp, passp);
		assert(strcmp(brief, briefp) == 0);
		assert(strcmp(user, userp) == 0);
		assert(strcmp(pass, passp) == 0);
		assert(accdb_index_next(&idx) == 0);
		++i;
	}
	assert(i == 128);

	assert(accdb_index_prev(&idx) == 0);
	assert(accdb_del(&idx) == 0);
	assert(accdb_index_prev(&idx) == 0);
	assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
	//printf("%s, %s, %s\n", briefp, userp, passp);

	memset(bigbuf, 'B', REC_BLOB_ENTRY_MAX);
	assert(accdb_index_init(db, &idx) == 0);
	assert(accdb_add_note(&idx, bigbuf, REC_BLOB_ENTRY_MAX) == 0);
	assert(accdb_add_note(&idx, bigbuf, 130) == 0);
	assert(accdb_add_note(&idx, bigbuf, 20) == 0);
	assert(accdb_add_note(&idx, bigbuf, REC_BLOB_ENTRY_MAX) == 0);
	
	assert(accdb_index_init(db, &idx) == 0);
	i = 64;
	while (accdb_index_has_entry(&idx)) {
		sprintf(brief, "BRIEF%u", (unsigned)i);
		sprintf(user, "USER%u", (unsigned)i);
		sprintf(pass, "PASS%u", (unsigned)i);
		assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		//printf("%s, %s, %s\n", briefp, userp, passp);
		assert(strcmp(brief, briefp) == 0);
		assert(strcmp(user, userp) == 0);
		assert(strcmp(pass, passp) == 0);
		assert(accdb_index_next(&idx) == 0);
		++i;
	}
	assert(i == 127);

	assert(accdb_index_init(db, &idx) == 0);
	note = NULL;
	i = 0;
	while (1) {
		assert(accdb_index_next_note(&idx, &note, &size) == 0);
		if (note == NULL)
			break;
		assert(size <= REC_BLOB_ENTRY_MAX);
		assert(memcmp(note, bigbuf, size) == 0);
		++i;
	}
	assert(i == 4);

	memset(bigbuf, 'B', 130);
	bigbuf[129] = '\0';

	assert(accdb_add(db, &idx, "blob brief", "blob usr", bigbuf) == 0);
	assert(accdb_add(db, &idx, "blob brief", "blob usr", bigbuf) == 0);
	assert(accdb_add(db, &idx, "blob brief", "blob usr", bigbuf) == 0);

	assert(accdb_index_init(db, &idx) == 0);

	i = 0;
	while (accdb_index_has_entry(&idx)) {
		assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		if (!strcmp(briefp, "blob brief")) {
			//printf("%s, %s, '%s'\n", briefp, userp, passp);
			assert(strcmp("blob usr", userp) == 0);
			assert(strcmp(bigbuf, passp) == 0);
			++i;
		}
		assert(accdb_index_next(&idx) == 0);
	}
	assert(i == 3);

	assert(accdb_add(db, &idx, "blob all", bigbuf, bigbuf) == 0);
	assert(accdb_index_init(db, &idx) == 0);

	i = 0;
	while (accdb_index_has_entry(&idx)) {
		assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		if (!strcmp(briefp, "blob all")) {
			assert(strcmp(bigbuf, userp) == 0);
			assert(strcmp(bigbuf, passp) == 0);
			++i;
		}
		assert(accdb_index_next(&idx) == 0);
	}
	assert(i == 1);

	memset(bigbuf, 'B', REC_BLOB_ENTRY_MAX);
	bigbuf[REC_BLOB_ENTRY_MAX - 1] = '\0';

	assert(accdb_add(db, &idx, "blob giant", bigbuf, bigbuf) == 0);
	assert(accdb_cache_clear(db) == 0);
	assert(accdb_index_init(db, &idx) == 0);

	i = 0;
	while (accdb_index_has_entry(&idx)) {
		assert(accdb_index_get_entry(&idx, &briefp, &userp, &passp) == 0);
		if (!strcmp(briefp, "blob giant")) {
			assert(strcmp(bigbuf, userp) == 0);
			assert(strcmp(bigbuf, passp) == 0);
			++i;
		}
		assert(accdb_index_next(&idx) == 0);
	}
	assert(i == 1);

	assert(accdb_index_init(db, &idx) == 0);
	while (accdb_index_has_entry(&idx)) {
		assert(accdb_del(&idx) == 0);
	}

	accdb_index_clear(&idx);
	accdb_cache_put((uint8_t *)bigbuf);
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

	assert(index_rec_find_type(brief, user, pass, &len) == INDEX_NORMAL);
	assert(len == strlen(brief) + strlen(user) + strlen(pass) + 4);

	assert(index_rec_find_type(brief, bigbuf, pass, &len) == INDEX_EXT);
	assert(len == strlen(brief) + 4);

	assert(index_rec_find_type(bigbuf, user, pass, &len) == INDEX_INVALID);

	memset(rec, 0, sizeof(rec));
	
	assert(index_rec_create(rec, brief, user, pass) == 0);
	assert(index_rec_parse(rec, &briefp, &userp, &passp) == 0);
	assert(index_rec_get_extended(rec) == 0);
	assert(strcmp(briefp, brief) == 0);
	assert(strcmp(userp, user) == 0);
	assert(strcmp(passp, pass) == 0);

	memset(rec, 0, sizeof(rec));

	assert(index_rec_create_extended(rec, brief, ptr) == 0);
	assert(index_rec_parse_extended(rec, &briefp, &ptrp) == 0);
	assert(index_rec_get_extended(rec) == 1);
	assert(strcmp(briefp, brief) == 0);
	assert(ptrp == ptr);

	memset(rec, 0, sizeof(rec));
	assert(blob_rec_create(rec, REC_BLOB_NOTE, bigbuf, sizeof(bigbuf)) == 0);
	assert(blob_rec_get_type(rec) == REC_BLOB_NOTE);
	assert(blob_rec_get_size(rec) == sizeof(bigbuf));
	assert(memcmp(blob_rec_get_data(rec), bigbuf, sizeof(bigbuf)) == 0);
}

void
test_accdb_allocation(struct accdb *db)
{
	uint8_t *b[3], *scratch;
	uint16_t s[3];
	struct sector *p;
	unsigned i;

	scratch = accdb_cache_get_scratch(db);
	assert(scratch != NULL);
	p = GET_SECTOR(scratch);
	assert(p->buf == scratch);
	assert(p->empty == 0);
	assert(p->refcount == 1);
	assert(p->scratch == 1);
	assert(accdb_deallocate_buf(db, scratch) == 0);

	assert(accdb_cache_flush(db) == 0);

	for (i = 0; i < 3; ++i) { 
		b[i] = accdb_allocate_buf(db, SECT_TYPE_BLOB);
		assert(b[i] != NULL);
		assert(buf_get_type(b[i]) == SECT_TYPE_BLOB);
		p = GET_SECTOR(b[i]);
		s[i] = p->sector;
		assert(p->buf == b[i]);
		assert(p->empty == 0);
		assert(p->refcount == 1);
		assert(p->scratch == 0);
		printf("allocate 0x%x\n", s[i]);
	}

	for (i = 0; i < 3; ++i)
		assert(accdb_deallocate_buf(db, b[i]) == 0);

	// with the allocator as it is, we should get the same sectors
	// after freeing before.
	for (i = 0; i < 3; ++i) { 
		b[i] = accdb_allocate_buf(db, SECT_TYPE_BLOB);
		assert(b[i] != NULL);
		assert(buf_get_type(b[i]) == SECT_TYPE_BLOB);
		p = GET_SECTOR(b[i]);
		assert(s[i] == p->sector);
		assert(p->buf == b[i]);
		assert(p->empty == 0);
		assert(p->refcount == 1);
		assert(p->scratch == 0);
		printf("(2nd) allocate 0x%x\n", s[i]);
	}

	for (i = 0; i < 3; ++i)
		assert(accdb_deallocate_buf(db, b[i]) == 0);
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

	buf = accdb_cache_get_scratch(db);
	assert(buf != NULL);

	buf_set_type_size(buf, SECT_TYPE_BLOB, 0);
	assert(buf_get_type(buf) == SECT_TYPE_BLOB);
	assert(buf_get_size(buf) == 0);

	s1 = strlen(a1);
	s2 = strlen(a2);
	s3 = strlen(a3);
	memset(tmp, 0, sizeof(tmp));

	b1 = buf_allocate_record(buf, s1);
	assert(b1 != NULL);
	memcpy(b1, a1, s1);
	strcat(tmp, a1);
	printf("a1 (%u) '%s', payload %u\n", (unsigned)s1, a1, (unsigned)buf_get_size(buf));
	assert(buf_get_size(buf) == strlen(tmp));
	assert(memcmp(buf_get_payload(buf), tmp, strlen(tmp)) == 0);

	b2 = buf_allocate_record(buf, s2);
	assert(b2 != NULL);
	memcpy(b2, a2, s2);
	strcat(tmp, a2);
	printf("a2 (%u) '%s', payload %u\n", (unsigned)s2, a2, (unsigned)buf_get_size(buf));
	assert(buf_get_size(buf) == strlen(tmp));
	assert(memcmp(buf_get_payload(buf), tmp, strlen(tmp)) == 0);

	b3 = buf_allocate_record(buf, s3);
	assert(b3 != NULL);
	memcpy(b3, a3, s3);
	strcat(tmp, a3);
	printf("a3 (%u) '%s', payload %u\n", (unsigned)s3, a3, (unsigned)buf_get_size(buf));
	assert(buf_get_size(buf) == strlen(tmp));
	assert(memcmp(buf_get_payload(buf), tmp, strlen(tmp)) == 0);

	printf("a1 + a2 + a3 = %u bytes\n", (unsigned)(s1) + (unsigned)(s2) + (unsigned)(s3));
	printf("1. payload (%u) '%.*s'\n", buf_get_size(buf), buf_get_size(buf), buf_get_payload(buf));
	
	tmp[0] = '\0';
	strcat(tmp, a1);
	strcat(tmp, a3);
	buf_deallocate_record(buf, b2, s2);
	printf("2. payload (%u) '%.*s'\n", buf_get_size(buf), buf_get_size(buf), buf_get_payload(buf));
	assert(buf_get_size(buf) == strlen(tmp));
	assert(memcmp(buf_get_payload(buf), tmp, strlen(tmp)) == 0);

	tmp[0] = '\0';
	strcat(tmp, a3);
	buf_deallocate_record(buf, b1, s1);
	printf("3. payload (%u) '%.*s'\n", buf_get_size(buf), buf_get_size(buf), buf_get_payload(buf));
	assert(buf_get_size(buf) == strlen(tmp));
	assert(memcmp(buf_get_payload(buf), tmp, strlen(tmp)) == 0);

	tmp[0] = '\0';
	// prior memmoves invalidates b3
	buf_deallocate_section(buf, 0, s3); 
	printf("4. payload (%u) '%.*s'\n", buf_get_size(buf), buf_get_size(buf), buf_get_payload(buf));
	assert(buf_get_size(buf) == 0);
	
	accdb_cache_put(buf);
}

void
test_accdb_list(struct accdb *db)
{
	uint8_t *head, *buf[5], *record;
	unsigned i;
	uint16_t next, head_save;
	char tmp[32];

	head = accdb_allocate_buf(db, SECT_TYPE_BLOB);
	assert(head != NULL);

	buf_set_prev(head, 0xdead);
	assert(buf_get_prev(head) == 0xdead);
	buf_set_prev(head, 0);

	buf_set_next(head, 0xbeef);
	assert(buf_get_next(head) == 0xbeef);
	buf_set_next(head, 0);

	record = buf_allocate_record(head, 4);
	assert(record != NULL);
	memcpy(record, "HEAD", 4);
	assert(buf_get_size(head) == 4);
	assert(memcmp(buf_get_payload(head), "HEAD", 4) == 0);

	for (i = 0; i < 5; ++i) {
		buf[i] = accdb_allocate_buf(db, SECT_TYPE_BLOB);
		assert(buf[i] != NULL);
		sprintf(tmp, "SECT %u", i);
		record = buf_allocate_record(buf[i], strlen(tmp));
		assert(record != NULL);
		memcpy(record, tmp, strlen(tmp));
		assert(accdb_list_append(db, head, buf[i]) == 0);
	}

	printf("1. HEAD: 0x%x (next 0x%x, prev 0x%x)\n",
		accdb_cache_sector(head), buf_get_next(head),
		buf_get_prev(head));
	
	for (i = 0; i < 5; ++i) {
		printf(" SECT %u: 0x%x (next 0x%x, prev 0x%x)\n",
			i, accdb_cache_sector(buf[i]),
			buf_get_next(buf[i]),
			buf_get_prev(buf[i]));
	}

	assert(accdb_cache_flush(db) == 0);

	assert(buf_get_prev(head) == SECT_NULL);
	assert(buf_get_next(head) == accdb_cache_sector(buf[4]));
	assert(buf_get_prev(buf[4]) == accdb_cache_sector(head));
	assert(buf_get_next(buf[4]) == accdb_cache_sector(buf[3]));
	assert(buf_get_prev(buf[3]) == accdb_cache_sector(buf[4]));
	assert(buf_get_next(buf[3]) == accdb_cache_sector(buf[2]));
	assert(buf_get_prev(buf[2]) == accdb_cache_sector(buf[3]));
	assert(buf_get_next(buf[2]) == accdb_cache_sector(buf[1]));
	assert(buf_get_prev(buf[1]) == accdb_cache_sector(buf[2]));
	assert(buf_get_next(buf[1]) == accdb_cache_sector(buf[0]));
	assert(buf_get_prev(buf[0]) == accdb_cache_sector(buf[1]));
	assert(buf_get_next(buf[0]) == SECT_NULL);

	assert(accdb_cache_clear(db) == 0);

	head_save = accdb_cache_sector(head);
	next = buf_get_next(head);
	accdb_cache_put(head);
	i = 0;
	while (!SECT_IS_NULL(next)) {
		head = accdb_cache_get(db, next);
		assert(head != NULL);
		next = buf_get_next(head);
		accdb_cache_put(head);
		i++;
	}
	assert(i == 5);

	for (i = 0; i < 5; ++i)
		accdb_cache_put(buf[i]);

	assert(accdb_list_clear(db, head_save) == 0);
}

#include "vfs_test.h"

int
main(void)
{
	struct file fp;
	struct accdb db;

	if (vfs_open(&test_vfs, &fp, "test.db", VFS_RW) < 0)
		goto out;
	accdb_open(&db, &fp);

	if (accdb_format(&db) < 0)
		goto out;

	printf("\n\nIndex records:\n");
	test_accdb_rec(&db);
	printf("OK\n");

	printf("\n\nAllocation:\n");
	test_accdb_allocation(&db);
	printf("OK\n");

	printf("\n\nBuffers:\n");
	test_accdb_buffer(&db);
	printf("OK\n");

	printf("\n\nLists:\n");
	test_accdb_list(&db);
	printf("OK\n");

	printf("\n\nDB:\n");
	test_accdb(&db);
	printf("OK\n");

	if (accdb_cache_flush(&db) < 0)
		goto out;
	
	accdb_cache_print(&db, 0);

	return 0;

out:
	perror("accdb");
	return 0;
}