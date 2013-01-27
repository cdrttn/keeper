#ifndef _PBKDEF_H_
#define _PBKDEF_H_

#include "hmac-sha1.h"

#define BUF_LEN HMAC_SHA1_BLOCK_BYTES

struct hmac_sha1_buffer {
	hmac_sha1_ctx_t ctx;
	uint8_t buf[BUF_LEN];
	uint8_t size;
};
void hmac_sha1_buffer_init(struct hmac_sha1_buffer *hmac, const void *key, size_t len);
void hmac_sha1_buffer_update(struct hmac_sha1_buffer *hmac, const void *buf, size_t len);
void hmac_sha1_buffer_final(struct hmac_sha1_buffer *hmac, void *hash);

int pbkdf2_sha1(const char *P, size_t Plen,
		const char *S, size_t Slen,
		char *DK, unsigned int dkLen,
		unsigned int c);

#endif // _PBKDEF_H_
