#include <string.h>
#define _GNU_SOURCE
#include <stdio.h>
#include "keeper.h"

// lcd lines. RW is tied low and ignored.

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

#if 0
	_delay_ms(100);
	lcd_command(LCD_FUNC_8BIT_1LN);
	_delay_ms(30);
	lcd_command(LCD_FUNC_8BIT_1LN);
	_delay_ms(10);
	lcd_command(LCD_FUNC_8BIT_1LN);
	_delay_ms(10);
#endif

	// initialization for DOGM 3-line display, 3.3v
	_delay_ms(200);
#if 0
	lcd_command(LCD_FUNC_8BIT_2LN_IS(1));
	lcd_command(LCD_IS1_BIAS_15B_3LN);
	lcd_command(LCD_IS1_PWR_BOOST(0b01));
	lcd_command(LCD_IS1_FOLLOWER_ON(0b110));
	_delay_ms(200);
	lcd_command(LCD_IS1_SET_CONTRAST(0b0010));

	// back to standard hitachi interface
	lcd_command(LCD_FUNC_8BIT_2LN);
	lcd_command(LCD_CLEAR);
	lcd_command(LCD_ENTRY_CURSOR_LEFT);
	lcd_command(LCD_ON);
#endif

#if 0
Function Set     0 0 0 0 1 1 1 0 0 1 $39 8 bit data length, 2 lines, instruction table 1
Bias Set         0 0 0 0 0 1 1 1 0 1 $1D BS: 1/4, 3 line LCD
Power Control    0 0 0 1 0 1 0 0 0 0 $50 booster off, contrast C5, set C4
Follower Control 0 0 0 1 1 0 1 1 0 0 $6C set voltage follower and gain
Contrast Set     0 0 0 1 1 1 1 1 0 0 $7C set contrast C3, C2, C1
Function Set     0 0 0 0 1 1 1 0 0 0 $38 switch back to instruction table 0
Display ON/OFF   0 0 0 0 0 0 1 1 1 1 $0F display on, cursor on, cursor blink
Clear Display    0 0 0 0 0 0 0 0 0 1 $01 delete display, cursor at home
                                                                                         Initialisation for 5V
Entry Mode Set   0 0 0 0 0 0 0 1 1 0 $06 cursor auto-increment
#endif
	lcd_command(0x39);
	lcd_command(0x1D);
	lcd_command(0x50);
	lcd_command(0x6c);
	_delay_ms(200);
	lcd_command(0x7c);
	_delay_ms(200);
	lcd_command(0x38);
	lcd_command(0x0f);
	lcd_command(0x01);
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
}

void
lcd_entry_render(const struct entry *ent)
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

	lcd_set_cursor(ent->x + ent->pos_cursor, ent->y);
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
