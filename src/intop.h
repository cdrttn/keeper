#ifndef _INTOP_H_
#define _INTOP_H_

#include <stdint.h>

static inline uint16_t 
get_uint16(const uint8_t *buf, uint16_t pos)
{
	return (buf[pos] << 8) | buf[pos + 1];
}

static inline uint32_t 
get_uint32(const uint8_t *buf, uint16_t pos)
{
	return (((uint32_t)buf[pos] << 24) |
		((uint32_t)buf[pos + 1] << 16) |
		((uint32_t)buf[pos + 2] << 8) |
		buf[pos + 3]);
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

#endif // _INTOP_H_
