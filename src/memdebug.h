#ifndef __MEMDBG_H_
#define __MEMDBG_H_
/*
 * memdebug.h
 *
 *  Created on: 15 Dec 2010
 *      Author: Andy Brown
 *
 *  Use without attribution is permitted provided that this 
 *  header remains intact and that these terms and conditions
 *  are followed:
 *
 *  http://andybrown.me.uk/ws/terms-and-conditions
 */
 
 
#ifdef DEBUG
 
#include <stddef.h>
#include <stdint.h>
 
 
size_t getMemoryUsed(void);
size_t getFreeMemory(void);
size_t getLargestAvailableMemoryBlock(void);
size_t getLargestBlockInFreeList(void);
int getNumberOfBlocksInFreeList(void);
size_t getFreeListSize(void);
size_t getLargestNonFreeListBlock(void);
 
 
 
#endif // DEBUG
#endif // __MEMDBG_H_
