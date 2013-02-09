#include <avr/io.h>
#include <avr/interrupt.h>
#include <string.h>
#include <stdio.h>
#include "hd44780.h"
//borrow this from the sha204 lib
#include "delay_x.h"

// lcd control lines. RW is tied low and ignored.
#define LCD_CTRL PORTF
#define LCD_CTRL_DDR DDRF
#define LCD_RS _BV(3)
#define LCD_E _BV(2)

// shift register lines
#define LCD_DATA PORTC
#define LCD_DATA_DDR DDRC
#define LCD_DATA_SERIAL _BV(5)
#define LCD_DATA_LATCH _BV(6)
#define LCD_DATA_CLOCK _BV(7)

// start with backlight control OFF
static uint8_t backlight = 0;

static inline void
shift_out(uint8_t c)
{
	uint8_t bit;

	LCD_DATA &= ~LCD_DATA_LATCH;
	for (bit = 0; bit < 8; ++bit) {
		//if (c & _BV(bit))
		if ((c << bit) & 0x80)
			LCD_DATA |= LCD_DATA_SERIAL;
		else
			LCD_DATA &= ~LCD_DATA_SERIAL;
		LCD_DATA |= LCD_DATA_CLOCK;
		_delay_us(1);
		LCD_DATA &= ~LCD_DATA_CLOCK;
	}
	LCD_DATA |= LCD_DATA_LATCH;
}

void
lcd_command(uint8_t c)
{
	shift_out(c);
	LCD_CTRL &= ~LCD_RS;
	LCD_CTRL |= LCD_E;
	_delay_us(1);
	LCD_CTRL &= ~LCD_E;
	_delay_ms(5);
}

static int
_lcd_stdio_putc(char c, FILE *fp)
{
	shift_out(c);
	LCD_CTRL |= LCD_E | LCD_RS;
	_delay_us(1);
	LCD_CTRL &= ~LCD_E;
	_delay_us(200);

	return 0;
}

void
lcd_putc(uint8_t c)
{
	_lcd_stdio_putc(c, NULL);
}

FILE lcd_stdout = FDEV_SETUP_STREAM(_lcd_stdio_putc, NULL, _FDEV_SETUP_WRITE);

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
	case 3:
		lcd_command(LCD_LINE_3 + x);
		break;
	}
}

void
lcd_init(void)
{
	LCD_CTRL_DDR |= LCD_RS | LCD_E;
	LCD_DATA_DDR |= LCD_DATA_SERIAL | LCD_DATA_CLOCK | LCD_DATA_LATCH;
	_delay_ms(100);
	lcd_command(LCD_FUNC_8BIT_1LN);
	_delay_ms(30);
	lcd_command(LCD_FUNC_8BIT_1LN);
	_delay_ms(10);
	lcd_command(LCD_FUNC_8BIT_1LN);
	_delay_ms(10);

	lcd_command(LCD_FUNC_8BIT_2LN);
	//lcd_command(LCD_ENABLE);
	lcd_command(LCD_CLEAR);
	lcd_command(LCD_ENTRY_CURSOR_LEFT);
	lcd_command(LCD_ON);
}

// backlight control, 8-bit pwm on PORTB.7
void
lcd_backlight_on(void)
{
	if (backlight)
		return;

	DDRB |= _BV(7);
	PORTB &= ~_BV(7);
	OCR0A = 0xF;
	TCCR0A |= _BV(COM0A1) | _BV(WGM00);
	TCCR0B |= _BV(CS00);

	backlight = 1;
}

void
lcd_backlight_off(void)
{
	backlight = 0;
	OCR0A = 0x0;
	TCCR0A &= ~(_BV(COM0A1) | _BV(WGM00));
	TCCR0B &= ~_BV(CS00);
	DDRB &= ~_BV(7);
	PORTB &= ~_BV(7);
}

void
lcd_backlight_level_set(uint8_t lvl)
{
	OCR0A = lvl;
}

uint8_t
lcd_backlight_level_get(void)
{
	return OCR0A;
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
		fputc(ent->buf[i], &lcd_stdout);
	}
	for (; col < ent->width; ++col) {
		fputc(' ', &lcd_stdout);
	}

	lcd_set_cursor(ent->x + ent->pos_cursor, ent->y);
}

void
lcd_entry_putc(struct entry *ent, char c)
{

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
		if (ent->pos + ent->width < ent->size_current)
			ent->pos++;
	} else {
		ent->pos_cursor++;
	}
}

void
lcd_entry_backspace(struct entry *ent)
{
}
