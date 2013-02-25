#ifndef _CRYPTO_H_
#define _CRYPTO_H_

#include <stdint.h>

enum cipher_dir {
	C_DEC,
	C_ENC
};

#define KEEPER_KEY_SIZE_BITS	256
#define KEEPER_KEY_SIZE		(KEEPER_KEY_SIZE_BITS / 8)
#define KEEPER_IV_SIZE		16

struct cipher;

int8_t crypto_init(void);
struct cipher *crypto_cipher_init(void);
void crypto_cipher_free(struct cipher *cipher);
int8_t crypto_cipher_sector(struct cipher *cipher, void *key, void *iv,
			    uint8_t mode, const void *in, void *out);
int8_t crypto_get_rand_bytes(void *buf, size_t amt);
int8_t crypto_pbkdf2_sha1(const void *password, size_t password_length,
		   	  const void *salt, size_t salt_length,
		   	  void *key, size_t key_length,
		   	  unsigned int iterations);
#endif
