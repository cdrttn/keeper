/*
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

#include <string.h>
#include <alloca.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include "pbkdf2.h"

void
hmac_sha1_buffer_init(struct hmac_sha1_buffer *hmac, const void *key, size_t len)
{
	memset(hmac, 0, sizeof(*hmac));
	hmac_sha1_init(&hmac->ctx, key, len * 8);
}

void
hmac_sha1_buffer_update(struct hmac_sha1_buffer *hmac, const void *buf, size_t len)
{
	const uint8_t *p = buf;

	if (hmac->size) {
		size_t copy = BUF_LEN - hmac->size;
		if (copy > len)
			copy = len;
		memcpy(hmac->buf + hmac->size, p, copy);
		hmac->size += copy;
		if (hmac->size == BUF_LEN) {
			hmac_sha1_nextBlock(&hmac->ctx, hmac->buf);
			hmac->size = 0;
		}
		len -= copy;
		p += copy;
	}

	while (len >= BUF_LEN) {
		hmac_sha1_nextBlock(&hmac->ctx, p);
		len -= BUF_LEN;
		p += BUF_LEN;
	}

	if (len) {
		assert(hmac->size == 0);
		assert(len < BUF_LEN);
		memcpy(hmac->buf, p, len);
		hmac->size = len;
	}
}

void
hmac_sha1_buffer_final(struct hmac_sha1_buffer *hmac, void *hash)
{
	hmac_sha1_lastBlock(&hmac->ctx, hmac->buf, hmac->size << 3);
	hmac_sha1_final(hash, &hmac->ctx);
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

#define MAX_PRF_BLOCK_LEN SHA1_BLOCK_BYTES

int pbkdf2_sha1(const char *P, size_t Plen,
		const char *S, size_t Slen,
		char *DK, unsigned int dkLen,
		unsigned int c)
{
	char U[MAX_PRF_BLOCK_LEN];
	char T[MAX_PRF_BLOCK_LEN];
	int i, k, rc = -1;
	unsigned int u, hLen, l, r;
	size_t tmplen = Slen + 4;
	char *tmp;
	struct hmac_sha1_buffer hmac;

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

	/* XXX
	if (dkLen > 4294967295U)
		return -1;
	*/
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

	hmac_sha1_buffer_init(&hmac, P, Plen);

	/*
	if (crypt_hmac_init(&hmac, hash, P, Plen))
		return -1;
	*/

	for (i = 1; (unsigned int) i <= l; i++) {
		memset(T, 0, hLen);

		for (u = 1; u <= c ; u++) {
			if (u == 1) {
				memcpy(tmp, S, Slen);
				tmp[Slen + 0] = (i & 0xff000000) >> 24;
				tmp[Slen + 1] = (i & 0x00ff0000) >> 16;
				tmp[Slen + 2] = (i & 0x0000ff00) >> 8;
				tmp[Slen + 3] = (i & 0x000000ff) >> 0;
				/*
				if (crypt_hmac_write(hmac, tmp, tmplen))
					goto out;
				HMAC_Update(&hmac, (const unsigned char *)tmp, tmplen);
				*/
				hmac_sha1_buffer_update(&hmac, tmp, tmplen);
			} else {
				/*
				if (crypt_hmac_write(hmac, U, hLen))
					goto out;
				HMAC_Update(&hmac, (const unsigned char *)U, hLen);
				*/
				hmac_sha1_buffer_update(&hmac, U, hLen);
			}

			/*
			if (crypt_hmac_final(hmac, U, hLen))
				goto out;
			HMAC_Final(&hmac, (unsigned char *)U, &finallen);
			HMAC_Init_ex(&hmac, NULL, 0, hash_id, NULL);
			*/
			hmac_sha1_buffer_final(&hmac, U);
			hmac_sha1_buffer_init(&hmac, P, Plen);
			
			for (k = 0; (unsigned int) k < hLen; k++)
				T[k] ^= U[k];
		}

		memcpy(DK + (i - 1) * hLen, T, (unsigned int) i == l ? r : hLen);
	}
	rc = 0;
out:

	return rc;
}
