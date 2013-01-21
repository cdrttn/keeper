#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "intop.h"
#include "vfs.h"
#include "vfs_crypt.h"
#include "crypto.h"

#define SECT_SIZE VFS_SECT_SIZE
struct crypt_file {
	struct file base;
	struct cipher *cipher;
	uint8_t tmp[SECT_SIZE]; // XXX extra 512 bytes could be bad on MCU
	uint8_t IV[KEEPER_IV_SIZE];
	uint8_t key[KEEPER_KEY_SIZE];
	uint8_t unlocked:1;
};

#define CTX(fp) 		((struct crypt_file *)(fp)->ctx)
#define BASE(fp) 		(&CTX(fp)->base)
#define DATA_SECT_START 	2

#define PBKDF_ITERATIONS	100
#define PW_DIGEST_SIZE		20
#define PW_SALT_SIZE		32

#define MAGIC			"ACCDB\xff\xff\xff"
#define VERSION			0x0001

#define MAGIC_POS		0
#define VERSION_POS		8
#define MK_DIGEST_POS		10
#define MK_SALT_POS		30
#define MK_DIGEST_ITER_POS	62
#define PW_SALT_POS		64
#define PW_DIGEST_ITER_POS	96
#define HEADER_LENGTH		98

#ifdef VFS_CRYPT_DEBUG
static const char *
_hexdump(const void *k, size_t s)
{
	static char buf[PW_SALT_SIZE * 2 + 1];
	const uint8_t *a, *end;
	char *b = buf;
	a = k;
	b = buf;
	end = a + s;
	while (a < end) {
		snprintf(b, 3, "%02x", *a);
		b += 2;
		++a;
	}
	return buf;
}
#define _kd(k) _hexdump(k, PW_SALT_SIZE)
#define _dd(d) _hexdump(d, PW_DIGEST_SIZE)
#define LOG(x) printf x
#else
#define LOG(x) 
#endif

static inline void
header_add_magic(uint8_t *buf)
{
	memcpy(buf, MAGIC, strlen(MAGIC));
}

static inline uint8_t
header_check_magic(const uint8_t *buf)
{
	return memcmp(buf, MAGIC, strlen(MAGIC)) == 0;
}

static inline void
header_set_version(uint8_t *buf, uint16_t vers)
{
	set_uint16(buf, VERSION_POS, vers);
}

static inline uint16_t
header_get_version(const uint8_t *buf)
{
	return get_uint16(buf, VERSION_POS);
}

static inline void
header_set_master_key_digest(uint8_t *buf, const void *digest)
{
	memcpy(buf + MK_DIGEST_POS, digest, PW_DIGEST_SIZE);
}

static inline const void *
header_get_master_key_digest(const uint8_t *buf)
{
	return buf + MK_DIGEST_POS;
}

static inline void
header_set_master_key_digest_salt(uint8_t *buf, const void *salt)
{
	memcpy(buf + MK_SALT_POS, salt, PW_SALT_SIZE);
}

static inline const void *
header_get_master_key_digest_salt(const uint8_t *buf)
{
	return buf + MK_SALT_POS;
}

static inline void
header_set_master_key_digest_iterations(uint8_t *buf, uint16_t iter)
{
	set_uint16(buf, MK_DIGEST_ITER_POS, iter);
}

static inline uint16_t
header_get_master_key_digest_iterations(const uint8_t *buf)
{
	return get_uint16(buf, MK_DIGEST_ITER_POS);
}

static inline void
header_set_password_salt(uint8_t *buf, const void *salt)
{
	memcpy(buf + PW_SALT_POS, salt, PW_SALT_SIZE);
}

static inline const void *
header_get_password_salt(const uint8_t *buf)
{
	return buf + PW_SALT_POS;
}

static inline void
header_set_password_iterations(uint8_t *buf, uint16_t iter)
{
	set_uint16(buf, PW_DIGEST_ITER_POS, iter); 
}

static inline uint16_t
header_get_password_iterations(const uint8_t *buf)
{
	return get_uint16(buf, PW_DIGEST_ITER_POS);
}

static void
set_iv_plain(struct file *fp, uint16_t sector)
{
	memset(CTX(fp)->IV, 0, KEEPER_IV_SIZE);
	set_uint16(CTX(fp)->IV, 0, sector);
}

static int8_t
read_decrypt_sector_internal(struct file *fp, void *buf, uint16_t sector)
{
	int8_t rv;

	if (vfs_read_sector(BASE(fp), CTX(fp)->tmp, sector) < 0)
		return -1;
	set_iv_plain(fp, sector);
	rv = crypto_cipher_sector(CTX(fp)->cipher, CTX(fp)->key,
				  CTX(fp)->IV, C_DEC, CTX(fp)->tmp,
				  buf);
	if (rv < 0)
		return -1;

	return 0;
}

static int8_t
write_encrypt_sector_internal(struct file *fp, const void *buf, uint16_t sector)
{
	int8_t rv;

	set_iv_plain(fp, sector);
	rv = crypto_cipher_sector(CTX(fp)->cipher, CTX(fp)->key,
				  CTX(fp)->IV, C_ENC, buf,
				  CTX(fp)->tmp);
	if (rv < 0)
		return -1;
	if (vfs_write_sector(BASE(fp), CTX(fp)->tmp, sector) < 0)
		return -1;

	return 0;
}

int8_t
vfs_crypt_format(struct file *fp, const void *password, size_t len)
{
	struct crypt_file *ctx = CTX(fp);
	uint8_t salt[PW_SALT_SIZE];
	uint8_t key[KEEPER_KEY_SIZE];
	uint8_t userkey[KEEPER_KEY_SIZE];
	uint8_t digest[PW_DIGEST_SIZE];
	uint8_t buf[SECT_SIZE]; // XXX too much stack usage??
	int8_t rv = 0;

	memset(buf, 0, SECT_SIZE);
	header_add_magic(buf);
	header_set_version(buf, VERSION);

	// generate a master key
	rv = crypto_get_rand_bytes(key, KEEPER_KEY_SIZE);
	if (rv < 0)
		goto out;
	LOG(("format: create master key %s\n", _kd(key)));

	// create digest of mk
	rv = crypto_get_rand_bytes(salt, sizeof(salt));
	if (rv < 0)
		goto out;
	LOG(("format: master key salt %s\n", _kd(salt)));
	rv = crypto_pbkdf2_sha1(key, KEEPER_KEY_SIZE, salt, sizeof(salt),
				digest, sizeof(digest), PBKDF_ITERATIONS);
	if (rv < 0)
		goto out;
	LOG(("format: master key digest %s\n", _dd(digest)));
	header_set_master_key_digest(buf, digest);
	header_set_master_key_digest_salt(buf, salt);
	header_set_master_key_digest_iterations(buf, PBKDF_ITERATIONS);

	// make an encryption key out of user's pw
	rv = crypto_get_rand_bytes(salt, sizeof(salt));
	if (rv < 0)
		goto out;
	LOG(("format: password salt %s\n", _kd(salt)));
	rv = crypto_pbkdf2_sha1(password, len, salt, sizeof(salt),
				userkey, sizeof(userkey), PBKDF_ITERATIONS);
	if (rv < 0)
		goto out;
	LOG(("format: password key %s\n", _kd(userkey)));
	header_set_password_salt(buf, salt);
	header_set_password_iterations(buf, PBKDF_ITERATIONS);

	// store our header
	rv = vfs_write_sector(BASE(fp), buf, 0);
	if (rv < 0)
		goto out;

	// encrypt the master key with the user's key
	memset(buf, 0, SECT_SIZE);
	memcpy(buf, key, KEEPER_KEY_SIZE);
	memcpy(ctx->key, userkey, KEEPER_KEY_SIZE);
	rv = write_encrypt_sector_internal(fp, buf, 1);
	if (rv < 0)
		goto out;

	// finished!
	memcpy(ctx->key, key, KEEPER_KEY_SIZE);
	ctx->unlocked = 1;
	rv = 0;

out:
	// XXX keep file unlocked at this point?
	if (rv < 0)
		memset(ctx->key, 0, KEEPER_KEY_SIZE);
	memset(buf, 0, SECT_SIZE);
	memset(userkey, 0, sizeof(userkey));
	memset(key, 0, sizeof(key));

	return rv;
}

int8_t
vfs_crypt_unlock(struct file *fp, const void *password, size_t len)
{
	struct crypt_file *ctx = CTX(fp);
	int8_t rv;
	uint8_t mkdigest[PW_DIGEST_SIZE];
	uint8_t mkdigest_test[PW_DIGEST_SIZE];
	uint8_t mksalt[PW_SALT_SIZE];
	uint8_t usersalt[PW_SALT_SIZE];
	uint8_t userkey[KEEPER_KEY_SIZE];
	uint8_t buf[SECT_SIZE]; // XXX too much stack usage??
	uint16_t mkiter, useriter;

	// read header, preform sanity checks
	rv = vfs_read_sector(BASE(fp), buf, 0);
	if (rv < 0)
		goto out;
	if (!header_check_magic(buf) ||
	    header_get_version(buf) != VERSION) {
		rv = -1;
		goto out;
	}
	memcpy(mkdigest, header_get_master_key_digest(buf),
	       PW_DIGEST_SIZE);
	LOG(("unlock: master key digest %s\n", _dd(mkdigest)));
	memcpy(mksalt, header_get_master_key_digest_salt(buf),
	       PW_SALT_SIZE);
	LOG(("unlock: master key salt %s\n", _kd(mksalt)));
	memcpy(usersalt, header_get_password_salt(buf),
	       PW_SALT_SIZE);
	LOG(("unlock: password salt %s\n", _kd(usersalt)));
	mkiter = header_get_master_key_digest_iterations(buf);
	useriter = header_get_password_iterations(buf);
	LOG(("unlock: mkiter %u, pwiter %u\n",
	    (unsigned)mkiter, (unsigned)useriter));

	// create user key
	rv = crypto_pbkdf2_sha1(password, len, usersalt, sizeof(usersalt),
				userkey, sizeof(userkey), useriter);
	if (rv < 0)
		goto out;
	LOG(("unlock: password key %s\n", _kd(userkey)));

	// attempt unlocking
	memcpy(ctx->key, userkey, sizeof(userkey));
	rv = read_decrypt_sector_internal(fp, buf, 1);
	if (rv < 0)
		goto out;
	rv = crypto_pbkdf2_sha1(buf, KEEPER_KEY_SIZE, mksalt, sizeof(mksalt),
				mkdigest_test, sizeof(mkdigest_test), mkiter);
	if (rv < 0)
		goto out;
	LOG(("unlock: master key digest (test) %s\n", _dd(mkdigest_test)));
	if (memcmp(mkdigest, mkdigest_test, sizeof(mkdigest)) != 0) {
		rv = -1;
		goto out;
	}

	// success!
	memcpy(ctx->key, buf, KEEPER_KEY_SIZE);
	ctx->unlocked = 1;
	rv = 0;

out:
	if (rv < 0)
		memset(ctx->key, 0, KEEPER_KEY_SIZE);
	memset(userkey, 0, sizeof(userkey));
	memset(buf, 0, sizeof(buf));

	return rv;
}

// requires file to be previously unlocked
int8_t
vfs_crypt_chpass(struct file *fp, const void *newpw, size_t len)
{
	struct crypt_file *ctx = CTX(fp);
	uint8_t salt[PW_SALT_SIZE];
	uint8_t key[KEEPER_KEY_SIZE];
	uint8_t userkey[KEEPER_KEY_SIZE];
	uint8_t buf[SECT_SIZE];
	int8_t rv;

	if (!ctx->unlocked)
		return -1;

	// create key for new pw
	rv = crypto_get_rand_bytes(salt, sizeof(salt));
	if (rv < 0)
		goto out;
	LOG(("chpass: newpass salt %s\n", _kd(salt)));
	rv = crypto_pbkdf2_sha1(newpw, len, salt, sizeof(salt),
				userkey, sizeof(userkey),
				PBKDF_ITERATIONS);
	LOG(("chpass: newpass key %s\n", _kd(userkey)));
	if (rv < 0)
		goto out;
	rv = vfs_read_sector(BASE(fp), buf, 0);
	if (rv < 0)
		goto out;
	header_set_password_salt(buf, salt);
	header_set_password_iterations(buf, PBKDF_ITERATIONS);
	rv = vfs_write_sector(BASE(fp), buf, 0);
	if (rv < 0)
		goto out;

	// write it out
	memset(buf, 0, SECT_SIZE);
	memcpy(key, ctx->key, sizeof(key));
	memcpy(buf, key, KEEPER_KEY_SIZE);
	memcpy(ctx->key, userkey, KEEPER_KEY_SIZE);
	rv = write_encrypt_sector_internal(fp, buf, 1);
	memcpy(ctx->key, key, sizeof(key));
	if (rv < 0)
		goto out;

	// finished!
	rv = 0;

out:
	memset(key, 0, sizeof(key));
	memset(buf, 0, SECT_SIZE);
	
	return rv;
}

static void
vfs_crypt_close(struct file *fp)
{
	vfs_close(BASE(fp));
	crypto_cipher_free(CTX(fp)->cipher);
	memset(CTX(fp), 0, sizeof(struct crypt_file));
	free(CTX(fp));
}

static int8_t
vfs_crypt_read_sector(struct file *fp, void *buf, uint16_t sector)
{
	if (sector < DATA_SECT_START || !CTX(fp)->unlocked)
		return -1;

	return read_decrypt_sector_internal(fp, buf, sector);
}

static int8_t
vfs_crypt_write_sector(struct file *fp, const void *buf, uint16_t sector)
{
	if (sector < DATA_SECT_START || !CTX(fp)->unlocked)
		return -1;

	return write_encrypt_sector_internal(fp, buf, sector);
}

static uint16_t
vfs_crypt_start_sector(struct file *fp)
{
	return DATA_SECT_START;
}

static uint16_t
vfs_crypt_end_sector(struct file *fp)
{
	return 0xffff;
}

struct vfs crypto_vfs = {
	.name = "crypto_vfs",
	.vopen = NULL,
	.vclose = vfs_crypt_close,
	.vread_sector = vfs_crypt_read_sector,
	.vwrite_sector = vfs_crypt_write_sector,
	.vstart_sector = vfs_crypt_start_sector,
	.vend_sector = vfs_crypt_end_sector,
};

int8_t
vfs_crypt_init(struct file *base)
{
	struct crypt_file *crypt;

	crypt = calloc(1, sizeof(struct crypt_file));
	if (crypt == NULL)
		return -1;

	crypt->cipher = crypto_cipher_init();
	if (crypt->cipher == NULL)
		goto out;

	crypt->base = *base;
	base->vfs = &crypto_vfs;
	base->ctx = crypt;
	crypt->unlocked = 0;

	return 0;

out:
	if (crypt->cipher)
		crypto_cipher_free(crypt->cipher);
	free(crypt);

	return -1;
}

#define TEST_FN "test_crypt.bin"
#define TEST_PW "oogabooganooga"
#define TEST_PW_LEN 14
#define TEST_N_PW "roborobomofoman"
#define TEST_N_PW_LEN 15
#define TEST_SECT_COUNT 8
void
test_vfs_crypt_format(const struct vfs *meth)
{
	struct file f;
	uint8_t buf[SECT_SIZE];
	uint16_t start;
	uint8_t count;
	
	assert(vfs_open(meth, &f, TEST_FN, VFS_RW) == 0);
	assert(vfs_crypt_init(&f) == 0);	
	assert(vfs_crypt_format(&f, TEST_PW, TEST_PW_LEN) == 0);

	start = vfs_start_sector(&f); 
	for (count = 0; count < TEST_SECT_COUNT; ++count) {
		memset(buf, count, sizeof(buf));
		assert(vfs_write_sector(&f, buf, start + count) == 0);
	}

	vfs_close(&f);
}

void
test_vfs_crypt_unlock(const struct vfs *meth)
{
	struct file f;
	uint8_t buf[SECT_SIZE];
	uint16_t start;
	uint8_t count;

	assert(vfs_open(meth, &f, TEST_FN, VFS_RW) == 0);
	assert(vfs_crypt_init(&f) == 0);	
	assert(vfs_crypt_unlock(&f, TEST_PW, TEST_PW_LEN) == 0);

	start = vfs_start_sector(&f); 
	for (count = 0; count < TEST_SECT_COUNT; ++count) {
		unsigned i;
		assert(vfs_read_sector(&f, buf, start + count) == 0);
		for (i = 0; i < sizeof(buf); ++i)
			assert(buf[i] == count);
	}

	vfs_close(&f);
}

void
test_vfs_crypt_chpass(const struct vfs *meth)
{
	struct file f;

	assert(vfs_open(meth, &f, TEST_FN, VFS_RW) == 0);
	assert(vfs_crypt_init(&f) == 0);	
	assert(vfs_crypt_unlock(&f, TEST_PW, TEST_PW_LEN) == 0);
	assert(vfs_crypt_chpass(&f, TEST_N_PW, TEST_N_PW_LEN) == 0);
	vfs_close(&f);

	assert(vfs_open(meth, &f, TEST_FN, VFS_RW) == 0);
	assert(vfs_crypt_init(&f) == 0);	
	assert(vfs_crypt_unlock(&f, TEST_N_PW, TEST_N_PW_LEN) == 0);
	vfs_close(&f);
}

void
test_vfs_crypt_header(void)
{
	uint8_t buf[SECT_SIZE];
	uint8_t digest[PW_DIGEST_SIZE];
	uint8_t salt1[PW_SALT_SIZE];
	uint8_t salt2[PW_SALT_SIZE];
	uint16_t iter1 = 0xdead;
	uint16_t iter2 = 0xb33f;
	unsigned i;

	memset(buf, 0, sizeof(buf));
	memset(digest, 'D', sizeof(digest));
	memset(salt1, 'S', sizeof(salt1));
	memset(salt2, 'S', sizeof(salt2));
	salt1[0] = '1';
	salt2[0] = '2';

	header_add_magic(buf);
	header_set_version(buf, VERSION);
	header_set_master_key_digest(buf, digest);
	header_set_master_key_digest_salt(buf, salt1);
	header_set_master_key_digest_iterations(buf, iter1);
	header_set_password_salt(buf, salt2);
	header_set_password_iterations(buf, iter2);

	assert(header_check_magic(buf) == 1);
	assert(header_get_version(buf) == VERSION);
	assert(memcmp(header_get_master_key_digest(buf), digest, sizeof(digest)) == 0);
	assert(memcmp(header_get_master_key_digest_salt(buf), salt1, sizeof(salt1)) == 0);
	assert(header_get_master_key_digest_iterations(buf) == iter1);
	assert(memcmp(header_get_password_salt(buf), salt2, sizeof(salt2)) == 0);
	assert(header_get_password_iterations(buf) == iter2);

	for (i = HEADER_LENGTH; i < SECT_SIZE; ++i) {
		assert(buf[i] == 0x00);
	}	
}

void
test_vfs_crypt(const struct vfs *meth)
{
	printf("Header:\n");
	test_vfs_crypt_header();
	printf("OK\n");

	printf("Format:\n");
	test_vfs_crypt_format(meth);
	printf("OK\n");

	printf("Unlock:\n");
	test_vfs_crypt_unlock(meth);
	printf("OK\n");

	printf("Chpass:\n");
	test_vfs_crypt_chpass(meth);
	printf("OK\n");
}

#include "vfs_pc.h"
int
main(void)
{
	crypto_init();
	test_vfs_crypt(&pc_vfs);
	
	return 0;
}
