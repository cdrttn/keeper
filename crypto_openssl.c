#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "crypto.h"
#include "vfs.h"

#define SECT_SIZE VFS_SECT_SIZE

struct cipher {
	EVP_CIPHER_CTX ctx;
	const EVP_CIPHER *cipher;
};

static unsigned initialized = 0;

int8_t
crypto_init(void)
{
	OpenSSL_add_all_algorithms();
	initialized = 1;

	return 0;
}

struct cipher *
crypto_cipher_init(void)
{
	struct cipher *cipher;
	assert(initialized);

	cipher = calloc(1, sizeof(struct cipher));
	if (cipher == NULL)
		return NULL;
	EVP_CIPHER_CTX_init(&cipher->ctx);
	cipher->cipher = EVP_aes_256_cbc();

	return cipher;
}

void
crypto_cipher_free(struct cipher *cipher)
{
	assert(initialized);
	if (!cipher)
		return;
	EVP_CIPHER_CTX_cleanup(&cipher->ctx);
	free(cipher);
}

int8_t
crypto_cipher_sector(struct cipher *cipher, void *key, void *iv, uint8_t mode,
		     const void *in, void *out)
{
	int outlen = 0;
	int rv;

	assert(initialized);

	rv = EVP_CipherInit_ex(&cipher->ctx, cipher->cipher, NULL,
			       key, iv, mode == C_ENC? 1 : 0);
	if (rv <= 0)
		return -1;
	rv = EVP_CIPHER_CTX_set_padding(&cipher->ctx, 0);
	if (rv <= 0)
		return -1;
	assert(KEEPER_KEY_SIZE == EVP_CIPHER_CTX_key_length(&cipher->ctx));
	assert(KEEPER_IV_SIZE == EVP_CIPHER_CTX_iv_length(&cipher->ctx));

	rv = EVP_CipherUpdate(&cipher->ctx, out, &outlen, in,
			      SECT_SIZE);
	if (rv <= 0 || outlen != SECT_SIZE)
		return -1;
	rv = EVP_CipherFinal_ex(&cipher->ctx, out, &outlen);
	if (rv <= 0 || outlen != 0)
		return -1;
	
	return 0;
}
 
int8_t
crypto_get_rand_bytes(void *buf, size_t amt)
{
	int rv;

	assert(initialized);

	rv = RAND_bytes(buf, amt);
	
	return rv? 0 : -1;
}

int8_t
crypto_pbkdf2_sha1(const void *password, size_t password_length,
		   const void *salt, size_t salt_length,
		   void *key, size_t key_length,
		   unsigned int iterations)
{
	assert(initialized);

        if (!PKCS5_PBKDF2_HMAC_SHA1(password, (int)password_length,
            (unsigned char *)salt, (int)salt_length,
            (int)iterations, (int)key_length, (unsigned char *)key))
                return -1;

        return 0;
}

void
test_crypto(void)
{
	struct cipher *cipher;
	uint8_t key[KEEPER_KEY_SIZE];
	uint8_t iv[KEEPER_IV_SIZE];
	uint8_t buf[SECT_SIZE];
	uint8_t orig[SECT_SIZE];
	uint8_t tmp[SECT_SIZE];
	uint8_t salt[32];
	uint8_t digest[20];
	int i;

	memset(orig, 'F', SECT_SIZE);
	memset(key, 'X', sizeof(key));
	memset(iv, 'Y', sizeof(iv));

	assert(crypto_init() == 0);
	assert((cipher = crypto_cipher_init()) != NULL);
	assert(crypto_cipher_sector(cipher, key, iv, C_ENC, orig, buf) == 0);
	assert(crypto_cipher_sector(cipher, key, iv, C_DEC, buf, tmp) == 0);
	assert(memcmp(orig, tmp, SECT_SIZE) == 0);
	assert(crypto_get_rand_bytes(salt, sizeof(salt)) == 0);
	assert(crypto_pbkdf2_sha1(key, sizeof(key), salt, sizeof(salt), digest, sizeof(digest), 100) == 0);
	for (i = 0; i < sizeof(digest); ++i)
		fprintf(stderr, "%02x", digest[i]);
	fprintf(stderr, "\n");
	crypto_cipher_free(cipher);
}
