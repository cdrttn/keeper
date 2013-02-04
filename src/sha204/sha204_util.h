#ifndef _SHAUTIL_H_
#define _SHAUTIL_H_

#include <stdint.h>
#include "sha204_comm_marshaling.h"

#define DATA(b) ((b) + SHA204_BUFFER_POS_DATA)

typedef uint8_t sha204_config_t[READ_32_RSP_SIZE];

#define SN_FIRST_LEN 4
static inline const uint8_t *
sha204_config_sn_first(const sha204_config_t conf)
{
	return DATA(conf);
}

#define SN_LAST_LEN 5
static inline const uint8_t *
sha204_config_sn_last(const sha204_config_t conf)
{
	return DATA(conf) + 8;
}

#define REV_LEN 4
static inline const uint8_t *
sha204_config_rev(const sha204_config_t conf)
{
	return DATA(conf) + 4;
}

static inline uint8_t
sha204_config_i2c_enabled(const sha204_config_t conf)
{
	return DATA(conf)[14] & 1;
}

static inline uint8_t
sha204_config_i2c_addr(const sha204_config_t conf)
{
	return DATA(conf)[16];
}

static inline uint8_t
sha204_config_otp_mode(const sha204_config_t conf)
{
	return DATA(conf)[18];
}

static inline uint8_t
sha204_config_selector_mode(const sha204_config_t conf)
{
	return DATA(conf)[19];
}

uint8_t sha204_get_config(sha204_config_t conf);

#endif // _SHAUTIL_H_
