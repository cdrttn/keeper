#ifndef _LOGGING_H_
#define _LOGGING_H_

#include <stdio.h>

#ifdef BUILD_AVR
#include <avr/pgmspace.h>
#define logf(c) printf_P c
#define _P(c) PSTR(c)
#else
#define logf(c) printf c
#define _P(c) c
#endif

#endif // _LOGGING_H_
