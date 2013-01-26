 
#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__

extern unsigned char flags;
extern volatile unsigned char kbint;

void keyboardInit( void );
unsigned char getKey( void );

#endif
