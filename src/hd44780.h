#ifndef _HD44780_H_
#define _HD44780_H_

#include <stdio.h>
#include <stdint.h>

// commands
#define LCD_LINE_0 0x80
#define LCD_LINE_1 0xc0
#define LCD_LINE_2 0x94
#define LCD_LINE_3 0xd4

#define LCD_CLEAR 0b00000001
#define LCD_HOME 0b00000010

#define LCD_ENTRY_MODE		0b00000100
#define LCD_ENTRY_MODE_LEFT	0b00000010
#define LCD_ENTRY_MODE_SHIFT	0b00000001
#define LCD_ENTRY_CURSOR_RIGHT	(LCD_ENTRY_MODE)
#define LCD_ENTRY_CURSOR_LEFT	(LCD_ENTRY_MODE | LCD_ENTRY_MODE_LEFT)
#define LCD_ENTRY_SHIFT_RIGHT	(LCD_ENTRY_MODE | LCD_ENTRY_MODE_SHIFT)
#define LCD_ENTRY_SHIFT_LEFT	(LCD_ENTRY_SHIFT_RIGHT | LCD_ENTRY_MODE_LEFT)

#define LCD_ONOFF		0b00001000
#define LCD_ONOFF_ON		0b00000100
#define LCD_ONOFF_CURSOR	0b00000010
#define LCD_ONOFF_BLINK		0b00000001
#define LCD_ON			(LCD_ONOFF | LCD_ONOFF_ON)
#define LCD_ON_CURSOR		(LCD_ON | LCD_ONOFF_CURSOR)
#define LCD_ON_CURSOR_BLINK	(LCD_ON_CURSOR | LCD_ONOFF_BLINK)
#define LCD_OFF			LCD_ONOFF

#define LCD_SHIFT		0b00010000
#define LCD_SHIFT_RIGHT		0b00001000
#define LCD_SHIFT_DISPLAY	0b00000100
#define LCD_SHIFT_CURSOR_RIGHT	(LCD_SHIFT | LCD_SHIFT_RIGHT)
#define LCD_SHIFT_CURSOR_LEFT	(LCD_SHIFT)
#define LCD_SHIFT_DISPLAY_RIGHT	(LCD_SHIFT | LCD_SHIFT_RIGHT | LCD_SHIFT_DISPLAY)
#define LCD_SHIFT_DISPLAY_LEFT	(LCD_SHIFT | LCD_SHIFT_DISPLAY)
//#define LCD_ENABLE		LCD_SHIFT

#define LCD_FUNC		0b00100000
#define LCD_FUNC_8BIT		0b00010000
#define LCD_FUNC_2LN		0b00001000
#define LCD_FUNC_5X10		0b00000100
#define LCD_FUNC_8BIT_2LN	(LCD_FUNC | LCD_FUNC_8BIT | LCD_FUNC_2LN)
#define LCD_FUNC_8BIT_1LN	(LCD_FUNC | LCD_FUNC_8BIT)
#define LCD_FUNC_8BIT_1LN_5X10	(LCD_FUNC_8BIT_1LN | LCD_FUNC_5X10)


extern FILE lcd_stdout;

void lcd_init(void);
void lcd_set_cursor(uint8_t x, uint8_t y);
void lcd_command(uint8_t c);
void lcd_putc(uint8_t c);
void lcd_backlight_on(void);
void lcd_backlight_off(void);
void lcd_backlight_level_set(uint8_t lvl);
uint8_t lcd_backlight_level_get(void);

struct entry {
	// underlying buffer
	char *buf;
	// max size of the buffer
	size_t size;
	// current size of buffer
	size_t size_current;
	// starting position of buf visible on screen
	size_t pos;
	// starting column
	uint8_t x;
	// row of entry
	uint8_t y;
	// column width of the entry
	uint8_t width;
	// position of cursor within the entry on screen
	uint8_t pos_cursor;
	// password mode, hide chars
	uint8_t passwd:1;
};

void lcd_entry_init(struct entry *ent, char *buf, size_t size,
	       uint8_t x, uint8_t y, uint8_t width);
void lcd_entry_render(const struct entry *ent);

void lcd_entry_putc(struct entry *ent, char c);
void lcd_entry_left(struct entry *ent);
void lcd_entry_right(struct entry *ent);
void lcd_entry_backspace(struct entry *ent);

#endif // _HD44780_H_
