#ifndef _TEST_H_
#define _TEST_H_

#include <assert.h>
#include <stdlib.h>
#include "logging.h"
#include "memdebug.h"

/* Dead simple test framework. Pass the test or die!
   The state of an MCU might be ruined by a bad test,
   so it's probably safer to abort and let whoever's
   fixing things fix the problem instead of trying to
   recover.
*/

#ifdef BUILD_AVR
#define TEST_FLUSH() usb_serial_flush_output()
#else
#define TEST_FLUSH()
#endif

#define TEST_ABORT() do { \
	TEST_FLUSH(); 	\
	abort(); 	\
} while(0)

//#ifdef BUILD_AVR
//#define AFMT "'%S' MEM %u LINE %u: "
//#else
#define AFMT "'%s' LINE %u: "
//#endif

#ifdef TEST_ASSERT
#define v_assert(a) assert(a)
#else

#ifndef TEST_QUIET

#define v_assert(a) \
do {							\
	logf((_P("ASSERT "AFMT), _P(#a), __LINE__));	\
	if ((a)) {					\
		logf((_P("OK\n")));			\
	} else {					\
		logf((_P("FAIL\n")));			\
		TEST_ABORT();				\
	}						\
} while (0)

#else

#define v_assert(a) \
do {							\
	if (!(a)) {					\
		logf((_P("ASSERT FAIL "AFMT), _P(#a), __LINE__));	\
		logf((_P("FAIL\n")));			\
		TEST_ABORT();				\
	}						\
} while (0)
#endif

#endif

#endif // _TEST_H_
