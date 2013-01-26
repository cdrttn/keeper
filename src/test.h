#ifndef _TEST_H_
#define _TEST_H_

#include <assert.h>
#include <stdlib.h>

/* Dead simple test framework. Pass the test or die!
   The state of an MCU might be ruined by a bad test,
   so it's probably safer to abort and let whoever's
   fixing things fix the problem instead of trying to
   recover.
*/
#define TEST_PR(x) printf x

#define TEST_ABORT() abort()

#ifdef TEST_ASSERT
#define v_assert(a) assert(a)
#else

#ifndef TEST_QUIET

#define v_assert(a) \
do {							\
	TEST_PR(("ASSERT '%s' %s:%s:%u: ", #a,		\
		__FILE__, __func__, __LINE__));		\
	if ((a)) {					\
		TEST_PR(("OK\n"));			\
	} else {					\
		TEST_PR(("FAIL\n"));			\
		TEST_ABORT();				\
	}						\
} while (0)

#else

#define v_assert(a) \
do {							\
	if (!(a)) {					\
		TEST_PR(("ASSERT FAIL '%s' %s:%s:%u: ", #a,	\
			__FILE__, __func__, __LINE__));	\
		TEST_PR(("FAIL\n"));			\
		TEST_ABORT();				\
	}						\
} while (0)
#endif

#endif

#endif // _TEST_H_
