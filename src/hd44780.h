/* 
 * This file is part of keeper.
 * 
 * Copyright 2013, cdavis
 *  
 * keeper is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * keeper is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with keeper.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _HD44780_H_
#define _HD44780_H_

#include <stdio.h>
#include <stdint.h>

// we're using a dog-m display 163, 16x3

#define LCD_MAX_X 16
#define LCD_MAX_Y 3
#define LCD_MAX_CGRAM 8

#define LCD_LINE_0 0x80
#define LCD_LINE_1 (0x80 | 0x10)
#define LCD_LINE_2 (0x80 | 0x20)

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

#define LCD_FUNC		0b00100000
#define LCD_FUNC_8BIT		0b00010000
#define LCD_FUNC_2LN		0b00001000
#define LCD_FUNC_5X10		0b00000100
#define LCD_FUNC_8BIT_2LN	(LCD_FUNC | LCD_FUNC_8BIT | LCD_FUNC_2LN)
#define LCD_FUNC_8BIT_1LN	(LCD_FUNC | LCD_FUNC_8BIT)
#define LCD_FUNC_8BIT_1LN_5X10	(LCD_FUNC_8BIT_1LN | LCD_FUNC_5X10)

#define LCD_CGRAM_SET		0b01000000
#define LCD_CGRAM_ADDR(a)	(LCD_CGRAM_SET | ((a) << 3))

// dog-m's ST7036 chipset Extension instruction tables.
// from these, we can set contrast and more.
// IS(0) is the "normal" hitachi-compat instruction table
#define LCD_FUNC_EXT_IS		0b00000011
#define LCD_FUNC_8BIT_2LN_IS(t)	(LCD_FUNC_8BIT_2LN | ((t) & LCD_FUNC_EXT_IS))
#define LCD_FUNC_8BIT_1LN_IS(t)	(LCD_FUNC_8BIT_1LN | ((t) & LCD_FUNC_EXT_IS))
#define LCD_FUNC_8BIT_1LN_5X10_IS(t) \
		(LCD_FUNC_8BIT_1LN_5X10 | ((t) & LCD_FUNC_EXT_IS))

#define LCD_IS1_BIAS_SET	0b00010100
#define LCD_IS1_BIAS_BS		0b00001000
#define LCD_IS1_BIAS_FX		0b00000001
#define LCD_IS1_BIAS_14B	(LCD_IS1_BIAS_SET | LCD_IS1_BIAS_BS)
#define LCD_IS1_BIAS_15B	(LCD_IS1_BIAS_SET)
#define LCD_IS1_BIAS_14B_FX	(LCD_IS1_BIAS_14B | LCD_IS1_BIAS_FX)
#define LCD_IS1_BIAS_15B_FX	(LCD_IS1_BIAS_15B | LCD_IS1_BIAS_FX)

#define LCD_IS1_SET_ICON_ADDR_	0b01000000
#define LCD_IS1_SET_ICON_MASK	0b00001111
#define LCD_IS1_SET_ICON_ADDR(a) (LCD_IS1_SET_ICON_ADDR_ | ((a) & LCD_IS1_SET_ICON_MASK))

#define LCD_IS1_PWR_	0b01010000
#define LCD_IS1_PWR_ION	0b00001000
#define LCD_IS1_PWR_BON	0b00000100
#define LCD_IS1_PWR_C54	0b00000011
#define LCD_IS1_PWR(c) (LCD_IS1_PWR_ | ((c) & LCD_IS1_PWR_C54))
#define LCD_IS1_PWR_ICON(c) (LCD_IS1_PWR(c) | LCD_IS1_PWR_ION)
#define LCD_IS1_PWR_BOOST(c) (LCD_IS1_PWR(c) | LCD_IS1_PWR_BON)
#define LCD_IS1_PWR_ICON_BOOST(c) (LCD_IS1_PWR_ICON(c) | LCD_IS1_PWR_BON)

#define LCD_IS1_FC		0b01100000
#define LCD_IS1_FC_FON		0b00001000
#define LCD_IS1_FC_R20		0b00000111
#define LCD_IS1_FOLLOWER_OFF	LCD_IS1_FC
#define LCD_IS1_FOLLOWER_ON(c) \
	(LCD_IS1_FC | LCD_IS1_FC_FON | ((c) & LCD_IS1_FC_R20))

#define LCD_IS1_CONTRAST_		0b01110000
#define LCD_IS1_CONTRAST_MASK	0b00001111
#define LCD_IS1_CONTRAST(a) (LCD_IS1_CONTRAST_ | ((a) & LCD_IS1_CONTRAST_MASK))

#define LCD_IS1_ICON_ADDR_	0b01000000
#define LCD_IS1_ICON_ADDR_MASK	0b00001111
#define LCD_IS1_ICON_ADDR(a) \
	(LCD_IS1_ICON_ADDR_ | ((a) & LCD_IS1_ICON_ADDR_MASK))

#define LCD_IS2_DHPS	0b00010000
#define LCD_IS2_DHPS_UD	0b00001000
#define LCD_IS2_DHPS_TOP	(LCD_IS2_DHPS | LCD_IS2_DHPS_UD)
#define LCD_IS2_DHPS_BOT	(LCD_IS2_DHPS)

extern FILE *lcd_stdout;

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
	// soft cursor mode
	uint8_t soft_cursor:1;
	// soft cursor on time
	uint8_t soft_cursor_on:1;
	// soft cursor flicker time
	systime_t soft_cursor_time;
};

void lcd_entry_init(struct entry *ent, char *buf, size_t size,
		    uint8_t x, uint8_t y, uint8_t width);
void lcd_entry_render(struct entry *ent);

void lcd_entry_set_cursor(const struct entry *ent);
char lcd_entry_getc(struct entry *ent);
void lcd_entry_putc(struct entry *ent, char c);
void lcd_entry_left(struct entry *ent);
void lcd_entry_right(struct entry *ent);
void lcd_entry_backspace(struct entry *ent);
void lcd_entry_delete(struct entry *ent);

struct menu {
	// upper left, x coord
	uint8_t x;
	// upper left, y coord
	uint8_t y;
	// height of the menu
	uint8_t height;
	// max number of items that can fit on screen
	uint8_t max_items;
	// width of menu
	uint8_t width;
	// the row of the cursor
	uint8_t row_cursor;
#define MAX_ROWS 4
	// cache of items 
	void *items[MAX_ROWS];
	// context for the menu
	void *ctx;
	// flag, menu must be repainted fully if dirty = 1
	uint8_t dirty:1;

	// return the item text, or null if empty item
	const char *(*get_item_text)(struct menu *, void *);

	// called when the cursor bumps into the bottom. the callback could
	// consider loading another screenful of items into the menu and
	// reseting the cursor to the top.
	void (*on_bottom_reached)(struct menu *);

	// called when the cursor bumps into top, and likewise.
	void (*on_top_reached)(struct menu *);
};

struct menu_item {
	const char *text;
	size_t index;
};

void lcd_menu_init(struct menu *menu, uint8_t x, uint8_t y,
	           uint8_t width, uint8_t height);
void lcd_menu_init_array(struct menu *menu, uint8_t x, uint8_t y,
		         uint8_t width, uint8_t height,
		         const struct menu_item *items);
void lcd_menu_render(struct menu *menu);
void lcd_menu_up(struct menu *menu);
void lcd_menu_down(struct menu *menu);
void lcd_menu_clear(struct menu *menu);
void lcd_menu_add_item(struct menu *menu, void *item);

static inline void *
lcd_menu_get_current_item(struct menu *mnu)
{
	return mnu->items[mnu->row_cursor];
}

static inline void
lcd_menu_dirty(struct menu *mnu)
{
	mnu->dirty = 1;
}

struct keyboard {
	uint8_t x;
	uint8_t y;
	uint8_t height;
	uint8_t width;
#define MAX_CHARSETS 5
	struct entry charset[MAX_CHARSETS];
	// row_charset = row_start + row_cur;
	uint8_t row_start;
	uint8_t row_cur;
};

void lcd_keyboard_load_cgram(void);
void lcd_keyboard_init(struct keyboard *cs, uint8_t x, uint8_t y,
		       uint8_t width, uint8_t height);
void lcd_keyboard_set_cursor(const struct keyboard *kb);
void lcd_keyboard_render(struct keyboard *cs);
void lcd_keyboard_up(struct keyboard *cs);
void lcd_keyboard_down(struct keyboard *cs);
void lcd_keyboard_left(struct keyboard *cs);
void lcd_keyboard_right(struct keyboard *cs);
char lcd_keyboard_getc(struct keyboard *cs);

#define BITMAP_ENTER 1
#define BITMAP_SPACE 2
#define BITMAP_BACKSPACE 8

#endif // _HD44780_H_
