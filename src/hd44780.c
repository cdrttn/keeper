#include <string.h>
#define _GNU_SOURCE
#include <stdio.h>
#include "keeper.h"

// lcd lines. RW is tied low and ignored since we're only
// writing data to the lcd, not reading.

#define LCD_CS_HIGH() palWritePad(GPIOB, GPIOB_LCD_CS, 1)
#define LCD_CS_LOW() palWritePad(GPIOB, GPIOB_LCD_CS, 0)
#define LCD_RS_HIGH() palWritePad(GPIOB, GPIOB_LCD_RS, 1)
#define LCD_RS_LOW() palWritePad(GPIOB, GPIOB_LCD_RS, 0)
#define LCD_CLK_HIGH() palWritePad(GPIOB, GPIOB_LCD_CLK, 1)
#define LCD_CLK_LOW() palWritePad(GPIOB, GPIOB_LCD_CLK, 0)
#define LCD_SI_HIGH() palWritePad(GPIOB, GPIOB_LCD_SI, 1)
#define LCD_SI_LOW() palWritePad(GPIOB, GPIOB_LCD_SI, 0)

// start with backlight control OFF
static uint8_t backlight = 0;
FILE *lcd_stdout = NULL;

static inline void
shift_out(uint8_t c)
{
	uint8_t bit;

	LCD_CLK_HIGH();
	LCD_CS_LOW();
	_delay_us(1);
	for (bit = 0; bit < 8; ++bit) {
		if ((c << bit) & 0x80)
			LCD_SI_HIGH();
		else
			LCD_SI_LOW();
		LCD_CLK_LOW();
		_delay_us(1);
		LCD_CLK_HIGH();
		_delay_us(1);
	}
	LCD_CS_HIGH();
}

void
lcd_command(uint8_t c)
{
	//outf("cmd = 0x%x\r\n", (unsigned)c);
	LCD_RS_LOW();
	_delay_us(1);
	shift_out(c);
	_delay_ms(5);
}

void
lcd_putc(uint8_t c)
{
	LCD_RS_HIGH();
	_delay_us(1);
	shift_out(c);
	_delay_us(200);
}

void
lcd_set_cursor(uint8_t x, uint8_t y)
{
	switch (y) {
	case 0:
		lcd_command(LCD_LINE_0 + x);
		break;
	case 1:
		lcd_command(LCD_LINE_1 + x);
		break;
	case 2:
		lcd_command(LCD_LINE_2 + x);
		break;
#ifdef LCD_LINE_3
	case 3:
		lcd_command(LCD_LINE_3 + x);
		break;
#endif
	}
}

static ssize_t
_lcd_write(void *c, const char *buf, size_t s)
{
	size_t i;

	(void)c;
	for (i = 0; i < s; ++i)
		lcd_putc(buf[i]);

	return s;
}

void
lcd_init(void)
{
	cookie_io_functions_t lcdio = {
		.read = NULL,
		.write = _lcd_write,
		.seek = NULL,
		.close = NULL
	};
	lcd_stdout = fopencookie(NULL, "w", lcdio);
	chDbgAssert(lcd_stdout != NULL, "lcd_init #1", "low on memory");

	setvbuf(lcd_stdout, NULL, _IONBF, 0);

	LCD_SI_HIGH();
	LCD_CLK_HIGH();

	_delay_ms(200);
/*
From datasheet:
Initialisation for 5V DOGM 163 3-line, SPI
Function Set     0000111001 $39 8 bit data length, 2 lines, instruction table 1
Bias Set         0000011101 $1D BS: 1/4, 3 line LCD
Power Control    0001010000 $50 booster off, contrast C5, set C4
Follower Control 0001101100 $6C set voltage follower and gain
Contrast Set     0001111100 $7C set contrast C3, C2, C1
Function Set     0000111000 $38 switch back to instruction table 0
Display ON/OFF   0000001111 $0F display on, cursor on, cursor blink
Clear Display    0000000001 $01 delete display, cursor at home
Entry Mode Set   0000000110 $06 cursor auto-increment
*/
	// set instruction table 1
	lcd_command(LCD_FUNC_8BIT_2LN_IS(1));
	// set the bias, 1/4, FX bit for 3-line display
	lcd_command(LCD_IS1_BIAS_14B_FX);
	// set power mode, 5v mode, no booster, contrast bits C5 and C4 0
	lcd_command(LCD_IS1_PWR(0));
	// turn follower on, set gain
	lcd_command(LCD_IS1_FOLLOWER_ON(0x4));
	_delay_ms(200);
	// set contrast
	lcd_command(LCD_IS1_CONTRAST(0xc));
	_delay_ms(200);

	// almost done, go back to hitachi-compat table 0
	lcd_command(LCD_FUNC_8BIT_2LN_IS(0));
	// increment cursor
	lcd_command(LCD_ENTRY_CURSOR_RIGHT);
	// clear the display
	lcd_command(LCD_CLEAR);
	// turn on, no cursor
	lcd_command(LCD_ON);
}

// backlight control
void
lcd_backlight_on(void)
{
	if (backlight)
		return;
	backlight = 100;
	pwmEnableChannel(&PWMD1, 0, backlight);
}

void
lcd_backlight_off(void)
{
	if (!backlight)
		return;

	backlight = 0;
	pwmDisableChannel(&PWMD1, 0);
}

void
lcd_backlight_level_set(uint8_t lvl)
{
	backlight = lvl;
	pwmEnableChannel(&PWMD1, 0, backlight);
}

uint8_t
lcd_backlight_level_get(void)
{
	return backlight;
}

void
lcd_entry_init(struct entry *ent, char *buf, size_t size,
	       uint8_t x, uint8_t y, uint8_t width)
{
	memset(ent, 0, sizeof(*ent));
	ent->buf = buf;
	ent->size = size;
	ent->x = x;
	ent->y = y;
	ent->width = width;
	ent->soft_cursor_time = 0;
}

void
lcd_entry_set_cursor(const struct entry *ent)
{
	lcd_set_cursor(ent->x + ent->pos_cursor, ent->y);
}

#define SOFT_CURSOR 23
#define SOFT_CURSOR_FLASH 400

void
lcd_entry_render(struct entry *ent)
{
	size_t i;
	uint8_t col;

	lcd_set_cursor(ent->x, ent->y);
	for (col = 0, i = ent->pos;
	     i < ent->size_current && col < ent->width;
	     ++i, ++col) {
		if (ent->passwd)
			fputc('*', lcd_stdout);
		else
			fputc(ent->buf[i], lcd_stdout);
	}
	for (; col < ent->width; ++col) {
		fputc(' ', lcd_stdout);
	}
	lcd_entry_set_cursor(ent);
	if (ent->soft_cursor) {
		systime_t now = chTimeNow();
		if (now >= ent->soft_cursor_time) {
			ent->soft_cursor_time = now + MS2ST(SOFT_CURSOR_FLASH);
			ent->soft_cursor_on ^= 1;
		}
		if (ent->soft_cursor_on)
			fputc(SOFT_CURSOR, lcd_stdout);
	}
}

char
lcd_entry_getc(struct entry *ent)
{
	return ent->buf[ent->pos + ent->pos_cursor];
}

void
lcd_entry_putc(struct entry *ent, char c)
{
	size_t pos = ent->pos + ent->pos_cursor;

	// no more room
	if (ent->size_current == ent->size)
		return;

	if (pos == ent->size_current) {
		// adding to end? just append c
		ent->buf[ent->size_current] = c;
	} else {
		// adding in middle, shift chars after pos
		// right one byte to make room
		memmove(ent->buf + pos + 1,
			ent->buf + pos,
			ent->size_current - pos);
		ent->buf[pos] = c;
	}

	ent->size_current++;
	lcd_entry_right(ent);
}

void
lcd_entry_backspace(struct entry *ent)
{
	size_t pos = ent->pos + ent->pos_cursor;

	if (pos == 0)
		return;

	if (pos < ent->size_current) {
		// shift chars left 1 byte
		memmove(ent->buf + pos - 1,
			ent->buf + pos,
			ent->size_current - pos);
	}

	ent->size_current--;
	lcd_entry_left(ent);
}

void
lcd_entry_delete(struct entry *ent)
{
	size_t pos = ent->pos + ent->pos_cursor;

	if (pos == ent->size_current)
		return;

	if (pos < ent->size_current) {
		// shift chars left 1 byte
		memmove(ent->buf + pos,
			ent->buf + pos + 1,
			ent->size_current - pos);
	}

	ent->size_current--;
}

void
lcd_entry_left(struct entry *ent)
{
	if (!ent->pos_cursor) {
		if (ent->pos)
			ent->pos--;
	} else {
		ent->pos_cursor--;
	}
}

void
lcd_entry_right(struct entry *ent)
{
	if (ent->pos_cursor + 1 == ent->width) {
		if (ent->pos + ent->width <= ent->size_current)
			ent->pos++;
	} else if (ent->pos + ent->pos_cursor < ent->size_current) {
		ent->pos_cursor++;
	}
}

void
lcd_menu_init(struct menu *menu, uint8_t x, uint8_t y,
	      uint8_t width, uint8_t height)
{
	memset(menu, 0, sizeof(*menu));
	menu->x = x;
	menu->y = y;
	menu->height = height;
	menu->width = width;
	menu->dirty = 1;
}

static void
menu_load_array(struct menu *menu)
{
	struct menu_item *p;
	uint8_t h;

	lcd_menu_clear(menu);

	for (h = menu->height, p = menu->ctx; h && p->text; ++p, --h) {
		lcd_menu_add_item(menu, p);
	}

	lcd_menu_dirty(menu);
}

static const char *
menu_get_text(struct menu *menu, void *p)
{
	struct menu_item *item = p;

	(void)menu;
	if (item && item->text) {
		return item->text;
	}

	return NULL;
}

static void
menu_bottom_reached(struct menu *menu)
{
	struct menu_item *p;
	uint8_t h;

	for (h = menu->height, p = menu->ctx; h && p->text; ++p, --h)
		;
	if (h)
		return;
	menu->row_cursor = 0;
	menu->ctx = p;
	menu_load_array(menu);
}

static void
menu_top_reached(struct menu *menu)
{
	struct menu_item *p;
	uint8_t h;

	for (h = menu->height, p = menu->ctx; h && p->text; --p, --h)
		;
	if (h)
		return;
	menu->row_cursor = menu->height - 1;
	menu->ctx = p;
	menu_load_array(menu);
}


void
lcd_menu_init_array(struct menu *menu, uint8_t x, uint8_t y,
		    uint8_t width, uint8_t height,
		    const struct menu_item *items)
{
	lcd_menu_init(menu, x, y, width, height);
	menu->ctx = (void *)&items[1];
	menu->get_item_text = menu_get_text;
	menu->on_bottom_reached = menu_bottom_reached;
	menu->on_top_reached = menu_top_reached;
	menu_load_array(menu);
}

void
lcd_menu_render(struct menu *menu)
{
	const char *label;
	uint8_t r;
	uint8_t w;

	for (r = 0; r < menu->height; ++r) {
		lcd_set_cursor(menu->x, menu->y + r);
		if (r == menu->row_cursor) {
			fputc('>', lcd_stdout);
		} else {
			fputc(' ', lcd_stdout);
		}
		if (menu->dirty) {
			chDbgAssert(menu->get_item_text != NULL,
				    "lcd_menu_render #1",
				    "no cb to get text");
			label = menu->get_item_text(menu, menu->items[r]);
			for (w = 1; label && *label && w < menu->width; ++w) {
				fputc(*label++, lcd_stdout);
			}
			for (; w < menu->width; ++w) {
				fputc(' ', lcd_stdout);
			}
		}
	}
	menu->dirty = 0;
}

// clears out items, resets item count
void
lcd_menu_clear(struct menu *menu)
{
	memset(menu->items, 0, sizeof(void*) * MAX_ROWS);
	menu->dirty = 1;
	menu->max_items = 0;
}

void
lcd_menu_add_item(struct menu *menu, void *item)
{
	chDbgAssert(menu->max_items < menu->height, "lcd_menu_add_item #1",
		    "too many items");
	menu->items[menu->max_items++] = item;
}

void
lcd_menu_up(struct menu *menu)
{
	if (menu->row_cursor == 0 && menu->on_top_reached) {
		menu->on_top_reached(menu);
		return;
	}
	menu->row_cursor--;
}

void
lcd_menu_down(struct menu *menu)
{
	if ((!menu->max_items || menu->row_cursor == menu->max_items - 1) &&
	    menu->on_bottom_reached) {
		menu->on_bottom_reached(menu);
		return;
	}
	menu->row_cursor++;
}

#define BITMAP_LEN 8
static const uint8_t bitmap_enter[] = {
	0b00000001,
	0b00000001,
	0b00000101,
	0b00001001,
	0b00011111,
	0b00001000,
	0b00000100,
	0b00000000
};

#define BITMAP_SPACE 2
static const uint8_t bitmap_space[] = {
	0b00000000,
	0b00000000,
	0b00000000,
	0b00000000,
	0b00000000,
	0b00011011,
	0b00001010,
	0b00001110
};

#define BITMAP_BACKSPACE 8

static const char *charset_special = "~!@#$%^&*()_+=[]{}\\|;:'\",.<>/?";
static const char *charset_lower =   "abcdefghijklmnopqrstuvwxyz      ";
static const char *charset_upper =   "ABCDEFGHIJKLMNOPQRSTUVWXYZ      ";
static const char *charset_num =     "0123456789                      ";
static const char *charset_action =  "\x01\x02\x08                             ";

void
lcd_keyboard_load_cgram(void)
{
	int i;

	lcd_command(LCD_CGRAM_ADDR(BITMAP_ENTER));
	for (i = 0; i < BITMAP_LEN; ++i) {
		lcd_putc(bitmap_enter[i]);
	}
	lcd_command(LCD_CGRAM_ADDR(BITMAP_SPACE));
	for (i = 0; i < BITMAP_LEN; ++i) {
		lcd_putc(bitmap_space[i]);
	}
	lcd_set_cursor(0, 0);
}

void
lcd_keyboard_init(struct keyboard *cs, uint8_t x, uint8_t y,
		  uint8_t width, uint8_t height)
{
	cs->x = x;
	cs->y = y;
	cs->width = width;
	cs->height = height;
	cs->row_start = 0;
	cs->row_cur = 0;

	lcd_entry_init(&cs->charset[0], (char *)charset_lower,
		       strlen(charset_lower), 0, 0, width);
	cs->charset[0].size_current = cs->charset[0].size;
	lcd_entry_init(&cs->charset[1], (char *)charset_upper,
		       strlen(charset_upper), 0, 0, width);
	cs->charset[1].size_current = cs->charset[1].size;
	lcd_entry_init(&cs->charset[2], (char *)charset_num,
		       strlen(charset_num), 0, 0, width);
	cs->charset[2].size_current = cs->charset[2].size;
	lcd_entry_init(&cs->charset[3], (char *)charset_special,
		       strlen(charset_special), 0, 0, width);
	cs->charset[3].size_current = cs->charset[3].size;
	lcd_entry_init(&cs->charset[4], (char *)charset_action,
		       strlen(charset_action), 0, 0, width);
	cs->charset[4].size_current = cs->charset[4].size;
}

void
lcd_keyboard_set_cursor(const struct keyboard *kb)
{
	lcd_entry_set_cursor(&kb->charset[kb->row_start + kb->row_cur]);
}

void
lcd_keyboard_render(struct keyboard *cs)
{
	uint8_t i;

	for (i = 0; i < cs->height && cs->row_start + i < MAX_CHARSETS; ++i) {
		struct entry *ent = &cs->charset[cs->row_start + i];
		ent->y = cs->y + i;
		lcd_entry_render(ent);
	}

	lcd_keyboard_set_cursor(cs);
}

void
lcd_keyboard_up(struct keyboard *cs)
{
	if (cs->row_cur == 0) {
		if (cs->row_start)
			cs->row_start--;
	} else {
		cs->row_cur--;
	}
}

void
lcd_keyboard_down(struct keyboard *cs)
{
	if (cs->row_cur == cs->height - 1) {
		if (cs->row_start + cs->height < MAX_CHARSETS)
			cs->row_start++;
	} else {
		cs->row_cur++;
	}
}

void
lcd_keyboard_left(struct keyboard *cs)
{
	int i;

	for (i = 0; i < MAX_CHARSETS; ++i)
		lcd_entry_left(&cs->charset[i]);
}

void
lcd_keyboard_right(struct keyboard *cs)
{
	int i;

	for (i = 0; i < MAX_CHARSETS; ++i)
		lcd_entry_right(&cs->charset[i]);
}

char
lcd_keyboard_getc(struct keyboard *cs)
{
	return lcd_entry_getc(&cs->charset[cs->row_start + cs->row_cur]);
}
