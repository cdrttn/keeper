#include <string.h>
#include "sha204_util.h"

#if 0
static inline uint8_t
read4conf(uint8_t *txb, uint8_t *rxb, uint16_t addr)
{
	return sha204m_read(txb, rxb, SHA204_ZONE_CONFIG, addr);
}
#endif

static inline uint8_t
read32conf(uint8_t *txb, uint8_t *rxb, uint16_t addr)
{
	return sha204m_read(txb, rxb,
			    SHA204_ZONE_CONFIG | READ_ZONE_MODE_32_BYTES,
			    addr);
}

uint8_t
sha204_get_config(sha204_config_t conf)
{
	uint8_t txb[READ_COUNT];
	uint8_t rv;

	rv = read32conf(txb, conf, 0);
	if (rv)
		return rv;

	return 0;
}
