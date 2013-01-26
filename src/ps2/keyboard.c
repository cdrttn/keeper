
//---------------------------------------------------------------------------------

// keyboard.c

// Keyboard routine taken mostly from AVR313. It handles the logic of the keyboard,
// not the character translation. That is done in scancodes.c.

//---------------------------------------------------------------------------------

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdio.h>
#include <stdlib.h>
#include <avr/io.h>

#include "keycodes.h"
#include "scancodes.h"

#define RISE 1
#define FALL 0
#define MAX_KEYS 8

#define LAMP_SCROLL 0
#define LAMP_NUM 1
#define LAMP_CAPS 2

volatile unsigned char _edge;
volatile unsigned char _bitCount;
volatile unsigned char _parity;
volatile unsigned char _sending;
volatile unsigned char _lamps;
volatile unsigned char flags = 0;
volatile unsigned char keyBuffer[MAX_KEYS];
volatile unsigned char * inPtr, * outPtr, * endPtr;
volatile unsigned char data;

void parityGenerator( unsigned char value )
{
	char loopCount = 8;
	while (--loopCount)
	{
		_parity += value;
		value >>= 1;
	}
	_parity ^= 1;
}

void putKey( unsigned char key )
{
	*inPtr = key;
	if ( ++inPtr == endPtr )
		inPtr = keyBuffer;
	if ( inPtr == outPtr )
		if ( ++outPtr == endPtr )
			outPtr = keyBuffer;
}

unsigned char getKey( void )
{
	unsigned char key = '\0';

	if ( outPtr == endPtr )
		outPtr = keyBuffer;
	if ( outPtr != inPtr )
		key = *outPtr++;
	return key;
}

void decode( unsigned char data )
{
	unsigned char chr;
	
	// If we did not just receive the up (0xf0) code, we must have a keypress.
	if (( flags & UP_FLAG ) == 0 )
	{
		// Is it the up code?
		if ( data == 0xf0 )
		{
			flags |= UP_FLAG;
		}
		// Shift?
		else if (( data == 0x12 ) || ( data == 0x59 ))
		{
			flags |= SHIFT_FLAG;
		}
		// Caps Lock?
		else if ( data == 0x58 )
		{
			flags ^= CLOCK_FLAG;
		}
		// Control? There is only one control key on my keyboard.
		else if ( data == 0x14 )
		{
			flags |= CTL_FLAG;
		}
		// Alternate? Both return the same code on this keyboard.
		else if ( data == 0x11 )
		{
			flags |= ALT_FLAG;
		}
		// Extended code. There are no non-unique extended codes on this keyboard.
		else if ( data == 0xe0 )
		{
			return;
		}
		// Display or use the character.
		else
		{
			// Convert the character to ASCII.
			if (( chr = decodeScanCode( data, flags )) != 0 )
			{
				putKey( chr );
			}
		}
	}
	else
	{
		// If the up code is active, it becomes inactive after any key.
		flags &= ~UP_FLAG;

		// Shift key up? Reset the shift.
		if (( data == 0x12 ) || ( data == 0x59 ))
		{
			flags &= ~SHIFT_FLAG;
		}
		// Control key up? Reset control.
		else if ( data == 0x14 )
		{
			flags &= ~CTL_FLAG;
		}
		// Alt key up? reset alternate.
		else if ( data == 0x11 )
		{
			flags &= ~ALT_FLAG;
		}
	}
}

//---------------------------------------------------------------------------------

// keyboardInit( )

// Sets up the keyboard handler to receive keyboard data.

//---------------------------------------------------------------------------------

#define KBPIN (_BV(0) | _BV(1))
#define KBDATA (PIND & _BV(1))
#define KBFALLING() EICRA = _BV(ISC01)
#define KBRISING() EICRA = _BV(ISC01) | _BV(ISC00)

void keyboardInit( void )
{
	// ensure that KBPINs are inputs
	DDRD &= KBPIN;
	PORTD |= KBPIN;		// Set pullups on the keyboard lines.
	_edge = FALL;		// Next edge should be falling.
	_bitCount = 11;		// Get 11 bits from the keyboard.
	inPtr = outPtr = keyBuffer;
	endPtr = inPtr + (MAX_KEYS);

	// interrupt setup, INT0 is our clock line from KB
	KBFALLING();
	EIMSK = _BV(INT0);
#if 0
	MCUCR &= ~0x03;		// Setup INT0 for the falling edge.
	MCUCR |= 0x02;
	PCMSK |= (1<<PIND2);	// Enable pin change on INT0 (why is this required?)	
	GIMSK= 0x40;		// Enable INT0 interrupt
#endif
}

//---------------------------------------------------------------------------------

// INT0 is the clock that is used to strobe the keyboard data. It is supplied by
// the keyboard.

//---------------------------------------------------------------------------------

volatile unsigned char kbint = 0;

ISR( INT0_vect )
{
	++kbint;
	if ( _edge == FALL )
	{
		if ( _bitCount < 11 && _bitCount > 2 )
		{
			data = ( data >> 1 );	// Shift the data over.
			if (KBDATA)
				data |= 0x80;	// Add a bit if it is a one.
		}
		_edge = RISE;			// Ready for rising edge.
		KBRISING();
#if 0
		MCUCR |= 0x03;			// Setup INT0 for rising edge.
#endif
	}
	else
	{
		if( --_bitCount == 0 )		// All bits received?
		{
			decode( data );		// Figure out what it is.
			_bitCount = 11;		// Start over.
		}
		_edge = FALL;			// Setup routine the next falling edge.
		KBFALLING();
#if 0
		MCUCR &= ~0x03;			// Setup INT0 for the falling edge.
		MCUCR |= 0x02;
#endif
	}
}



