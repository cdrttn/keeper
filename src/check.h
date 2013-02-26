#ifndef _TEST_H_
#define _TEST_H_

#include <stdlib.h>
#include "ch.h"

/* Dead simple test framework. Pass the test or die!
   The state of an MCU might be ruined by a bad test,
   so it's probably safer to abort and let whoever's
   fixing things fix the problem instead of trying to
   recover.
*/

#define TEST_ABORT() do { \
} while(1)

//#ifdef BUILD_AVR
//#define AFMT "'%S' MEM %u LINE %u: "
//#else
#define AFMT "'%s' LINE %u: "
//#endif

#ifdef TEST_ASSERT
//#define v_assert(a) assert(a)
#else

#ifndef TEST_QUIET

#define v_assert(a) \
do {							\
	outf("ASSERT "AFMT, #a, __LINE__);		\
	if ((a)) {					\
		outf("OK\r\n");				\
	} else {					\
		outf("FAIL\r\n");			\
		TEST_ABORT();				\
	}						\
} while (0)

#else

#define v_assert(a) \
do {							\
	if (!(a)) {					\
		outf("ASSERT FAIL "AFMT, #a, __LINE__);	\
		outf("FAIL\r\n");			\
		TEST_ABORT();				\
	}						\
} while (0)
#endif

#endif

#endif // _TEST_H_
