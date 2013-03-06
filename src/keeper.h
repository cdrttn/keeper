#ifndef _KEEPER_H_
#define _KEEPER_H_

#include <string.h>
#include "ch.h"
#include "check.h"
#include "console.h"
#include "hal.h"
#include "chrtclib.h"
#include "intop.h"
#include "ff.h"
#include "diskio.h"
#include "pool.h"
#include "vfs.h"
#include "vfs_fatfs.h"
#include "crypto.h"
#include "vfs_crypt.h"
#include "accdb.h"
#include "hd44780.h"
#include "buttons.h"

// use the stm32 core coupled memory for a 'fast heap'
// note that this cannot be used for DMA
//
#define FAST_HEAP_ADDR ((void *)0x10000000)
#define FAST_HEAP_SIZE 0x10000
extern MemoryHeap *fast_heap;
#define fast_malloc(s) chHeapAlloc(fast_heap, (s))
#define fast_free(p) chHeapFree((p))
static inline void *
fast_mallocz(size_t size)
{
	void *p = fast_malloc(size);
	memset(p, 0, size);
	return p;
}

#define _delay_ms(x) chThdSleepMilliseconds(x)
#define _delay_us(x) halPolledDelay(US2RTT(x))

#endif // _KEEPER_H_
