/* 
 * This file is part of keeper.
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

/* 
 * Notice for pbkdf2 implemenation:
 *
 * Implementation of Password-Based Cryptography as per PKCS#5
 * Copyright (C) 2002,2003 Simon Josefsson
 * Copyright (C) 2004 Free Software Foundation
 *
 * cryptsetup related changes
 * Copyright (C) 2012, Red Hat, Inc. All rights reserved.
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */


#include <stdio.h>
#include <string.h>
#include <alloca.h>
#include "keeper.h"

#define rccEnableRNG(lp) rccEnableAHB2(RCC_AHB2ENR_RNGEN, (lp))
#define rccEnableHASH(lp) rccEnableAHB2(RCC_AHB2ENR_HASHEN, (lp))
#define rccResetHASH() rccResetAHB2(RCC_AHB2ENR_HASHEN)
#define rccEnableCRYP(lp) rccEnableAHB2(RCC_AHB2ENR_CRYPEN, (lp))

// for hmac, key > 64 bytes
#define HMAC_LONG_KEY 		64
#define HASH_LKEY 		(1 << 16)
#define HASH_DINNE 		(1 << 12)
#define HASH_ALGO_SHA1 		(0 << 7)
#define HASH_ALGO_MD5 		(1 << 7) 
#define HASH_HMAC 		(1 << 6)
#define HASH_DATATYPE_BYTES 	(1 << 5)


#define HASH_BUF_MAX 4
struct hash {
	uint8_t size;
	uint8_t buf[HASH_BUF_MAX];
	const void *key;
	size_t key_len;
};

#define SHA1_HASH_BYTES 20
typedef uint8_t sha1_digest_t[SHA1_HASH_BYTES];

enum cryp_mode {
	CRYP_MODE_CBC,
	CRYP_MODE_CTR,
	CRYP_MODE_ECB
};

enum cryp_key_size {
	CRYP_KEY_128,
	CRYP_KEY_192,
	CRYP_KEY_256
};

#define CRYP_CR_ENC 0
#define CRYP_CR_DEC CRYP_CR_ALGODIR
#define CRYP_CR_K128 0
#define CRYP_CR_K192 CRYP_CR_KEYSIZE_0
#define CRYP_CR_K256 CRYP_CR_KEYSIZE_1
#define CRYP_CR_AES_ECB CRYP_CR_ALGOMODE_2 
#define CRYP_CR_AES_CBC (CRYP_CR_ALGOMODE_2 | CRYP_CR_ALGOMODE_0)
#define CRYP_CR_AES_CTR (CRYP_CR_ALGOMODE_2 | CRYP_CR_ALGOMODE_1)
#define CRYP_CR_AES_DEC_PREP (CRYP_CR_ALGOMODE_2 | CRYP_CR_ALGOMODE_1 | CRYP_CR_ALGOMODE_0)
#define CRYP_CR_8_BIT CRYP_CR_DATATYPE_1
#define CRYP_AES_BLOCK_WORDS 4

struct cryp {
	uint8_t blockwords;
};

static void
cryp_init(void)
{
	rccEnableCRYP(FALSE);
}

static inline void
cryp_load_key_128(const uint32_t *k)
{
	CRYP->K0LR = 0;
	CRYP->K0RR = 0;
	CRYP->K1LR = 0;
	CRYP->K1RR = 0;
	CRYP->K2LR = __REV(k[0]);
	CRYP->K2RR = __REV(k[1]);
	CRYP->K3LR = __REV(k[2]);
	CRYP->K3RR = __REV(k[3]);
}

static inline void
cryp_load_key_192(const uint32_t *k)
{
	CRYP->K0LR = 0;
	CRYP->K0RR = 0;
	CRYP->K1LR = __REV(k[0]);
	CRYP->K1RR = __REV(k[1]);
	CRYP->K2LR = __REV(k[2]);
	CRYP->K2RR = __REV(k[3]);
	CRYP->K3LR = __REV(k[4]);
	CRYP->K3RR = __REV(k[5]);
}

static inline void
cryp_load_key_256(const uint32_t *k)
{
	CRYP->K0LR = __REV(k[0]);
	CRYP->K0RR = __REV(k[1]);
	CRYP->K1LR = __REV(k[2]);
	CRYP->K1RR = __REV(k[3]);
	CRYP->K2LR = __REV(k[4]);
	CRYP->K2RR = __REV(k[5]);
	CRYP->K3LR = __REV(k[6]);
	CRYP->K3RR = __REV(k[7]);
}

static void
cryp_load_key(enum cryp_key_size ksz, const void *key, uint32_t *flags)
{
	switch (ksz) {
	case CRYP_KEY_128:
		cryp_load_key_128(key);
		*flags |= CRYP_CR_K128;
		break;
	case CRYP_KEY_192:
		cryp_load_key_192(key);
		*flags |= CRYP_CR_K192;
		break;
	case CRYP_KEY_256:
		cryp_load_key_256(key);
		*flags |= CRYP_CR_K256;
		break;
	}
}

static inline void
cryp_load_iv(const uint32_t *iv)
{
	CRYP->IV0LR = __REV(iv[0]);
	CRYP->IV0RR = __REV(iv[1]);
	CRYP->IV1LR = __REV(iv[2]);
	CRYP->IV1RR = __REV(iv[3]);
}

static inline void
cryp_enable(void)
{
	CRYP->CR |= CRYP_CR_CRYPEN;
}

static inline void
cryp_disable(void)
{
	CRYP->CR &= ~CRYP_CR_CRYPEN;
}

static inline void
cryp_flush(void)
{
	CRYP->CR |= CRYP_CR_FFLUSH;
}

static inline void
cryp_wait(void)
{
	while (CRYP->SR & CRYP_SR_BUSY)
		;
}

static void
cryp_aes_init(struct cryp *cryp, enum cryp_mode mode, enum cryp_key_size ksz,
	      enum cipher_dir dir, const void *key, const void *iv)
{
	uint32_t flags = 0;

	memset(cryp, 0, sizeof(*cryp));

	cryp->blockwords = CRYP_AES_BLOCK_WORDS;

	if (dir == C_DEC) {
		flags = CRYP_CR_DEC;
		if (mode == CRYP_MODE_CTR) {
			cryp_load_key(ksz, key, &flags);
		} else {
			// process key for decryption
			cryp_flush();
			cryp_load_key(ksz, key, &flags);
			CRYP->CR = flags | CRYP_CR_AES_DEC_PREP;
			cryp_enable();
			cryp_wait();
		}
	} else {
		flags = CRYP_CR_ENC;
		cryp_load_key(ksz, key, &flags);
	}

	switch (mode) {
	case CRYP_MODE_CBC:
		flags |= CRYP_CR_AES_CBC;
		cryp_load_iv(iv);
		break;
	case CRYP_MODE_CTR:
		flags |= CRYP_CR_AES_CTR;
		cryp_load_iv(iv);
		break;
	case CRYP_MODE_ECB:
		flags |= CRYP_CR_AES_ECB;
		break;
	}

	flags |= CRYP_CR_8_BIT;
	CRYP->CR = flags;

	cryp_flush();
	cryp_enable();
}

// iblock and oblock must be at least the size of one block, dependent on algo
static void
cryp_update(struct cryp *cryp, const void *iblock, void *oblock)
{
	const uint32_t *in = iblock;
	uint32_t *out = oblock;
	uint8_t i;

	while ((CRYP->SR & CRYP_SR_IFNF) == 0)
		;
	for (i = 0; i < cryp->blockwords; ++i) {
		CRYP->DR = in[i];
	}

	//cryp_wait();
	while ((CRYP->SR & CRYP_SR_OFNE) == 0)
		;
	for (i = 0; i < cryp->blockwords; ++i) {
		out[i] = CRYP->DOUT;
	}
}

static void
cryp_final(struct cryp *cryp)
{
	(void)cryp;
	cryp_disable();
}

static void
hash_init(void)
{
	rccEnableHASH(FALSE);
}

static inline void
hash_write_4(const uint8_t *b)
{
	HASH->DIN = *(uint32_t *)b;
}

static void
hash_update(struct hash *hash, const void *buf, size_t len)
{
	const uint8_t *b = buf;

	if (hash->size) {
		size_t copy = HASH_BUF_MAX - hash->size;

		if (copy > len)
			copy = len;
		memcpy(hash->buf + hash->size, b, copy);
		hash->size += copy;
		if (hash->size == HASH_BUF_MAX) {
			hash_write_4(hash->buf);
			hash->size = 0;
		}

		len -= copy;
		b += copy;
	}

	while (len >= 4) {
		hash_write_4(b);
		len -= 4;
		b += 4;
	}

	if (len) {
		memcpy(hash->buf, b, len);
		hash->size = len;
	}
}

static void
hash_final(struct hash *hash, void *digest)
{
	uint32_t *d = digest;
	uint8_t i;

	HASH->STR &= ~HASH_STR_NBW;
	if (hash->size) {
		hash_write_4(hash->buf);
		HASH->STR |= (hash->size << 3) & HASH_STR_NBW;
	}
	HASH->STR |= HASH_STR_DCAL;

	// wait...
	while (HASH->SR & HASH_SR_BUSY)
		;

	if (digest == NULL)
		return;

	// grab digest
	for (i = 0; i < 5; ++i) {
		d[i] = __REV(HASH->HR[i]);
	}
}

static void
hash_hmac_final(struct hash *hash, void *digest)
{
	// finalize data stream then send outer key, reusing our stored key
	hash_final(hash, NULL);
	hash->size = 0;
	hash_update(hash, hash->key, hash->key_len);
	// now finally, get the digest
	hash_final(hash, digest);
}

static void
hash_sha1_init(struct hash *sha1)
{
	memset(sha1, 0, sizeof(*sha1));
	//rccResetHASH();

	HASH->CR = HASH_ALGO_SHA1 | HASH_DATATYPE_BYTES;
	HASH->CR |= HASH_CR_INIT;
}

// arg key must persist while hash context active!!
static void
hash_hmac_sha1_init(struct hash *hash, const void *key, size_t key_len)
{
	memset(hash, 0, sizeof(*hash));

	HASH->CR = HASH_ALGO_SHA1 | HASH_DATATYPE_BYTES | HASH_HMAC;
	if (key_len > HMAC_LONG_KEY)
		HASH->CR |= HASH_LKEY;

	HASH->CR |= HASH_CR_INIT;

	// send key to hash module
	hash_update(hash, key, key_len);
	hash_final(hash, NULL);

	memset(hash, 0, sizeof(*hash));

	hash->key = key;
	hash->key_len = key_len;
}

static void
hash_sha1(const void *buf, size_t len, sha1_digest_t digest)
{
	struct hash sha1;

	hash_sha1_init(&sha1);
	hash_update(&sha1, buf, len);
	hash_final(&sha1, digest);
}

void
rng_init(void)
{
	// enable clock
	rccEnableRNG(FALSE);

	// enable interrupt
	//RNG->CR |= RNG_CR_IE;

	RNG->CR |= RNG_CR_RNGEN;
}

static int8_t 
rng_get(uint32_t *n)
{
	static uint32_t last = 0;
	static uint32_t v = 0;
#define RNG_ERR_BITS (RNG_SR_SEIS | RNG_SR_CEIS)

	// FIPS PUB 140-2, discard first, check with last
	while (v == last) {
		if ((RNG->SR & RNG_ERR_BITS) == 0 &&
		    (RNG->SR & RNG_SR_DRDY) == 1)
			v = RNG->DR;
	}

	*n = v;
	last = v;

	return 0;
}

/*
 * 5.2 PBKDF2
 *
 *  PBKDF2 applies a pseudorandom function (see Appendix B.1 for an
 *  example) to derive keys. The length of the derived key is essentially
 *  unbounded. (However, the maximum effective search space for the
 *  derived key may be limited by the structure of the underlying
 *  pseudorandom function. See Appendix B.1 for further discussion.)
 *  PBKDF2 is recommended for new applications.
 *
 *  PBKDF2 (P, S, c, dkLen)
 *
 *  Options:        PRF        underlying pseudorandom function (hLen
 *                             denotes the length in octets of the
 *                             pseudorandom function output)
 *
 *  Input:          P          password, an octet string (ASCII or UTF-8)
 *                  S          salt, an octet string
 *                  c          iteration count, a positive integer
 *                  dkLen      intended length in octets of the derived
 *                             key, a positive integer, at most
 *                             (2^32 - 1) * hLen
 *
 *  Output:         DK         derived key, a dkLen-octet string
 */

#define MAX_PRF_BLOCK_LEN 80

int8_t
crypto_pbkdf2_sha1(const void *P, size_t Plen,
		   const void *S, size_t Slen,
		   void *DK, size_t dkLen,
		   unsigned int c)
{
	char U[MAX_PRF_BLOCK_LEN];
	char T[MAX_PRF_BLOCK_LEN];
	int i, k, rc = -1;
	size_t u, hLen, l, r;
	size_t tmplen = Slen + 4;
	char *tmp;
	struct hash hmac;

	tmp = alloca(tmplen);
	if (tmp == NULL)
		return -1;

	if (c == 0)
		return -1;

	if (dkLen == 0)
		return -1;

	hLen = SHA1_HASH_BYTES;

	/*
	 *
	 *  Steps:
	 *
	 *     1. If dkLen > (2^32 - 1) * hLen, output "derived key too long" and
	 *        stop.
	 */
	if (dkLen > 4294967295U)
		return -1;

	/*
	 *     2. Let l be the number of hLen-octet blocks in the derived key,
	 *        rounding up, and let r be the number of octets in the last
	 *        block:
	 *
	 *                  l = CEIL (dkLen / hLen) ,
	 *                  r = dkLen - (l - 1) * hLen .
	 *
	 *        Here, CEIL (x) is the "ceiling" function, i.e. the smallest
	 *        integer greater than, or equal to, x.
	 */

	l = dkLen / hLen;
	if (dkLen % hLen)
		l++;
	r = dkLen - (l - 1) * hLen;

	/*
	 *     3. For each block of the derived key apply the function F defined
	 *        below to the password P, the salt S, the iteration count c, and
	 *        the block index to compute the block:
	 *
	 *                  T_1 = F (P, S, c, 1) ,
	 *                  T_2 = F (P, S, c, 2) ,
	 *                  ...
	 *                  T_l = F (P, S, c, l) ,
	 *
	 *        where the function F is defined as the exclusive-or sum of the
	 *        first c iterates of the underlying pseudorandom function PRF
	 *        applied to the password P and the concatenation of the salt S
	 *        and the block index i:
	 *
	 *                  F (P, S, c, i) = U_1 \xor U_2 \xor ... \xor U_c
	 *
	 *        where
	 *
	 *                  U_1 = PRF (P, S || INT (i)) ,
	 *                  U_2 = PRF (P, U_1) ,
	 *                  ...
	 *                  U_c = PRF (P, U_{c-1}) .
	 *
	 *        Here, INT (i) is a four-octet encoding of the integer i, most
	 *        significant octet first.
	 *
	 *     4. Concatenate the blocks and extract the first dkLen octets to
	 *        produce a derived key DK:
	 *
	 *                  DK = T_1 || T_2 ||  ...  || T_l<0..r-1>
	 *
	 *     5. Output the derived key DK.
	 *
	 *  Note. The construction of the function F follows a "belt-and-
	 *  suspenders" approach. The iterates U_i are computed recursively to
	 *  remove a degree of parallelism from an opponent; they are exclusive-
	 *  ored together to reduce concerns about the recursion degenerating
	 *  into a small set of values.
	 *
	 */

	hash_hmac_sha1_init(&hmac, P, Plen);

	for (i = 1; (unsigned int) i <= l; i++) {
		memset(T, 0, hLen);

		for (u = 1; u <= c ; u++) {
			if (u == 1) {
				memcpy(tmp, S, Slen);
				tmp[Slen + 0] = (i & 0xff000000) >> 24;
				tmp[Slen + 1] = (i & 0x00ff0000) >> 16;
				tmp[Slen + 2] = (i & 0x0000ff00) >> 8;
				tmp[Slen + 3] = (i & 0x000000ff) >> 0;
				hash_update(&hmac, tmp, tmplen);
			} else {
				hash_update(&hmac, U, hLen);
			}

			hash_hmac_final(&hmac, U);
			hash_hmac_sha1_init(&hmac, P, Plen);
			
			for (k = 0; (unsigned int) k < hLen; k++)
				T[k] ^= U[k];
		}

		memcpy(DK + (i - 1) * hLen, T, (unsigned int) i == l ? r : hLen);
	}

	rc = 0;

	return rc;
}

int8_t
crypto_init(void)
{
	cryp_init();
	hash_init();
	rng_init();

	return 0;
}

struct cipher *
crypto_cipher_init(void)
{
	return NULL;
}

void
crypto_cipher_free(struct cipher *cipher)
{
	(void)cipher;
}

int8_t
crypto_cipher_sector(struct cipher *cipher, void *key, void *iv,
		     uint8_t dir, const void *in, void *out)
{
	struct cryp cryp;
	uint8_t *o = out;
	const uint8_t *i = in;
	const uint8_t *end = i + VFS_SECT_SIZE;

	(void)cipher;
	cryp_aes_init(&cryp, CRYP_MODE_CBC, CRYP_KEY_256, (enum cipher_dir)dir,
		      key, iv);

	while (i < end) {
		cryp_update(&cryp, i, o);
		i += 16;
		o += 16;
	}

	cryp_final(&cryp);

	return 0;
}

int8_t
crypto_get_rand_bytes(void *buf, size_t amt)
{
	uint32_t n;
	uint8_t *p = buf;

	while (amt >= 4) {
		if (rng_get(&n) < 0)
			return -1;
		memcpy(p, &n, 4);
		p += 4;
		amt -= 4;
	}
	if (amt) {
		if (rng_get(&n) < 0)
			return -1;
		memcpy(p, &n, amt);
	}

	return 0;
}
