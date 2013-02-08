#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "vfs.h"
#include "crypto.h"
#include "bcal-cbc.h"
//#include "bcal_aes256.h"
//#include "bcal_aes128.h"
//#include "bcal_serpent.h"
#include "bcal_cast5.h"
//#include "cast5.h"
#include "pbkdf2.h"
#include "sha204_comm_marshaling.h"

int8_t
crypto_init(void)
{
	// nothing to do on AVR
	return 0;
}

int8_t
crypto_cipher_sector(struct cipher *cipher, void *key, void *iv, uint8_t mode,
		     const void *in, void *out)
{
#if 1
	bcal_cbc_ctx_t ctx;

	if (bcal_cbc_init(&cast5_desc, key, 128, &ctx)) {
		return -1;
	}
	memcpy(out, in, VFS_SECT_SIZE);
	if (mode == C_ENC) {
		bcal_cbc_encMsg(iv, out, VFS_SECT_SIZE / 16, &ctx);
	} else {
		bcal_cbc_decMsg(iv, out, VFS_SECT_SIZE / 16, &ctx);
	}		
	bcal_cbc_free(&ctx);
#else
#define BS 16
	aes128_ctx_t ctx;
	size_t amt = VFS_SECT_SIZE;
	uint8_t *b = out;
	//cast5_init(key, 128, &ctx);
	aes128_init(key, &ctx);

	memcpy(out, in, VFS_SECT_SIZE);
	if (mode == C_ENC) {
		while (amt) {
			aes128_enc(b, &ctx);
			amt -= BS;
			b += BS;
		}
	} else {
		while (amt) {
			aes128_dec(b, &ctx);
			amt -= BS;
			b += BS;
		}
	}
#endif
	return 0;
}

int8_t
crypto_get_rand_bytes(void *buf, size_t amt)
{
	uint8_t txb[RANDOM_COUNT];
	uint8_t rxb[RANDOM_RSP_SIZE];
	uint8_t *p = buf;

	while (amt >= 32) {
		if (sha204m_random(txb, rxb,
		    RANDOM_NO_SEED_UPDATE))
			return -1;
		memcpy(p, rxb + 1, 32);
		p += 32;
		amt -= 32;
	}
	if (amt) {
		if (sha204m_random(txb, rxb,
		    RANDOM_NO_SEED_UPDATE))
			return -1;
		memcpy(p, rxb + 1, amt);
	}

	return 0;
}

int8_t
crypto_pbkdf2_sha1(const void *password, size_t password_length,
		   const void *salt, size_t salt_length,
		   void *key, size_t key_length,
		   unsigned int iterations)
{
	return pbkdf2_sha1(password, password_length, salt, salt_length,
			   key, key_length, iterations);
}
