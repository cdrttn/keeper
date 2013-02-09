#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/power.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alloca.h>
//#include "usb_debug_only.h"
#include "ff.h"
#include "diskio.h"
#include "keyboard.h"
#include "keycodes.h"
#include "pool.h"
#include "test.h"
#include "vfs.h"
#include "vfs_fatfs.h"
#include "memdebug.h"
#include "sha1.h"
#include "crypto.h"
#include "vfs_crypt.h"
#include "accdb.h"
#include "sha204_util.h"
#include "hd44780.h"
#include "usb_serial.h"

static void cmd_free(const char **argv, uint8_t argc);

DWORD
get_fattime(void)
{
	return 0;
}

ISR(TIMER1_COMPA_vect)
{
	disk_timerproc();
}

#if 0
static inline uint8_t
kbgetch(void)
{
	uint8_t c;

	while ((c = getKey()) == 0)
		;
	return c;
}
#endif

enum escape_seq {
	ESC_NONE,
	ESC_ESC,
	ESC_BRACKET,
};
enum keys {
	ESC_SEQ = -8,
	ARROW_UP = -10,
	ARROW_DN = -20,
	ARROW_LT = -30,
	ARROW_RT = -40,
};
static int
getescape(void)
{
	static enum escape_seq state = ESC_NONE;
	int c;

	c = getchar();

	switch (state) {
	case ESC_NONE:
		if (c == 27) {
			state = ESC_ESC;
			return ESC_SEQ;
		}
		break;
	case ESC_ESC:
		if (c == '[') {
			state = ESC_BRACKET;
			return ESC_SEQ;
		}
		break;
	case ESC_BRACKET:
		state = ESC_NONE;
		switch (c) {
		case 'A': return ARROW_UP;
		case 'B': return ARROW_DN;
		case 'C': return ARROW_RT;
		case 'D': return ARROW_LT;
		}
		break;
	}
	
	return c;
}

static const char*
getline(char *buf, size_t amt, uint8_t echo)
{
	char *p = buf;
	char *end = buf + amt - 1;
	int c = 0;

	while (p < end && (c = getchar()) != '\r') {
		if (c == EOF)
			return NULL;
#if 0
		if (c < 0) {
			lcd_set_cursor(0, 3);
			fprintf_P(&lcd_stdout, _P("B: %04i"), c);
			continue;
		}
#endif
		if (c == 0x7f) {
			c = '\b';
			if (p > buf)
				p--;
			else
				echo = 0;
		} else
			*p++ = (char)c;
		if (echo)
			putchar(c);
	}
	*p = '\0';
	if (echo)
		putchar('\n');

	return buf;
}

static inline void
phex(uint8_t c)
{
	printf_P(_P("%02x"), c);
}

static void
hexdump(const void *buf, size_t amt)
{
	const uint8_t *b = buf;
	size_t i;

	for (i = 0; i < amt; ++i) {
		if ((i % 16) == 0)
			putchar('\n');
		phex(b[i]);
		putchar(' ');
	}
	putchar(' ');
}

#define MAXPATH 64
static char current_path[MAXPATH] = {0};

static void
cmd_cd(const char **argv, uint8_t argc)
{
	FRESULT rv;

	if (argc != 2) {
		puts_P(_P("ARGS: cd <path>\n"));
		return;
	}

	rv = f_chdir(argv[1]);
	if (rv != FR_OK) {
		printf_P(PSTR("Can't chdir: %u\n"), rv);
		return;
	}
	f_getcwd(current_path, sizeof(current_path));
	putchar('\n');
}

static void
cmd_cat(const char **argv, uint8_t argc)
{
	FIL f;
	FRESULT rv;
	UINT len;
	char buf[64];

	if (argc != 2) {
		puts_P(_P("ARGS: cat <path>\n"));
		return;
	}

	rv = f_open(&f, argv[1], FA_READ);
	if (rv != FR_OK) {
		printf_P(PSTR("Can't open %s: %u\n"), argv[1], rv);
		return;
	}

	do {
		rv = f_read(&f, buf, sizeof(buf), &len);
		if (rv != FR_OK) {
			printf_P(PSTR("Can't read: %u\n"), rv);
			break;
		}
		fwrite(buf, len, 1, stdout);
	} while (len == sizeof(buf));

	f_close(&f);
}

static void
cmd_sha1(const char **argv, uint8_t argc)
{
	//sha1_ctx_t sha;
	uint8_t hash[20];
	uint8_t i;

	if (argc < 2) {
		logf((_P("ARGS: sha1 <data>\n")));
		return;
	}

	sha1(hash, argv[1], strlen(argv[1]) * 8);
	
	logf((_P("sha1('%s') = "), argv[1]));
	for (i = 0; i < sizeof(hash); ++i) {
		logf((_P("%02x"), (unsigned)hash[i]));
	}
	putchar('\n');
}

static void
cmd_rand(const char **argv, uint8_t argc)
{
	uint8_t *buf;
	size_t len;

	if (argc < 2) {
		logf((_P("ARGS: rand <nbytes>\n")));
		return;
	}

	len = atoi(argv[1]);

	buf = calloc(1, len);
	if (!buf) {
		logf((_P("failed to alloc buf\n")));
		return;
	}

	if (crypto_get_rand_bytes(buf, len) < 0)
		logf((_P("failed to get rand bytes\n")));
	else {
		hexdump(buf, len);
		putchar('\n');
	}
	
	free(buf);
}

static void
cmd_pbkdf2(const char **argv, uint8_t argc)
{
	uint8_t hash[32];
	uint8_t i;
	uint16_t iter;
	int8_t rv;

	if (argc < 4) {
		logf((_P("ARGS: pbkdf2 <pw> <salt> <iter>\n")));
		return;
	}

	iter = atoi(argv[3]);

	rv = crypto_pbkdf2_sha1(argv[1], strlen(argv[1]),
			        argv[2], strlen(argv[2]),
				hash, sizeof(hash), iter);
	
	for (i = 0; i < sizeof(hash); ++i) {
		logf((_P("%02x"), (unsigned)hash[i]));
	}
	putchar('\n');
}

static void
cmd_aes(const char **argv, uint8_t argc)
{
	uint8_t key[KEEPER_KEY_SIZE];
	uint8_t iv[KEEPER_IV_SIZE];
	uint8_t *p, *c;
	//uint8_t p[512], *c[512];
	size_t len;
	int8_t rv;
	
	if (argc < 4) {
		logf((_P("ARGS: aes <key> <iv> <data>\n")));
		return;
	}
	cmd_free(NULL,0);
	len = strlen(argv[1]);
	if (len > sizeof(key))
		len = sizeof(key);
	memset(key, 0, sizeof(key));
	memcpy(key, argv[1], len);
#if 1
	p = calloc(1, 512);
	c = calloc(1, 512);
	if (!p || !c) {
		logf((_P("cant alloc buf\n")));
		goto out;
	}
#endif
	len = strlen(argv[2]);
	if (len > sizeof(iv))
		len = sizeof(iv);
	memset(iv, 0, sizeof(iv));
	memcpy(iv, argv[2], len);

	memcpy(p, argv[3], strlen(argv[3]));
	rv = crypto_cipher_sector(NULL, key, iv, C_ENC, p, c);
	if (rv < 0) {
		logf((_P("encryption failed\n")));
		goto out;
	}
	//hexdump(c, 512);
	//putchar('\n');
	rv = crypto_cipher_sector(NULL, key, iv, C_DEC, c, p);
	if (rv < 0) {
		logf((_P("decryption failed\n")));
		goto out;
	}
	//hexdump(p, 512);
	//putchar('\n');
	if (memcmp(p, argv[3], strlen(argv[3]))) {
		logf((_P("what? failed match\n")));
		goto out;
	}
	logf((_P("crypto good!\n")));

out:
	free(p);
	free(c);
}

static void
cmd_hd(const char **argv, uint8_t argc)
{
	FIL f;
	FRESULT rv;
	UINT len, rd;
	DWORD start;
	DWORD span;
	char buf[64];

	if (argc != 4) {
		puts_P(_P("ARGS: hd <offset> <length> <path>\n"));
		return;
	}

	start = strtoul(argv[1], NULL, 0);
	span = strtoul(argv[2], NULL, 0);

	rv = f_open(&f, argv[3], FA_READ);
	if (rv != FR_OK) {
		printf_P(PSTR("Can't open %s: %u\n"), argv[3], rv);
		return;
	}

	rv = f_lseek(&f, start);
	if (rv != FR_OK) {
		printf_P(PSTR("Can't seek to %lu: %u\n"), start, rv);
		goto out;
	}

	printf_P(PSTR("Dump from %lu to %lu...\n\n"), start, start + span);
	do {
		unsigned off;

		rd = sizeof(buf);
		if (rd > span)
			rd = span;
		if (rd == 0)
			break;
		rv = f_read(&f, buf, rd, &len);
		if (rv != FR_OK) {
			printf_P(PSTR("Can't read: %u\n"), rv);
			goto out;
		}
		span -= rd;
		for (off = 0; off < len; ++off) {
			phex(buf[off]);
			putchar(' ');
			if (!((off+1) % 16))
				putchar('\n');
		}
	} while (len == rd);
	putchar('\n');

out:
	f_close(&f);
}

static void
cmd_ls(const char **argv, uint8_t argc)
{
	DIR d;
	FILINFO f;
	FRESULT rv;
	char dirc;
	const char *path = current_path;

	if (argc >= 2) {
		rv = f_opendir(&d, argv[1]);
		path = argv[1];
	} else
		rv = f_opendir(&d, ".");

	if (rv != FR_OK) {
		printf_P(PSTR("Can't open dir: %u\n"), rv);
		return;
	}

	printf_P(PSTR("Contents of %s...\n"), path);
	while (1) {
		rv = f_readdir(&d, &f);
		if (rv != FR_OK || !f.fname[0])
			break;
		dirc = ' ';
		if (f.fattrib & AM_DIR)
			dirc = '/';
		printf_P(PSTR("% 12s%c %7lu Bytes\n"), f.fname, dirc, f.fsize);
	}

	if (rv != FR_OK) {
		printf_P(PSTR("failed dir list: %u\n"), rv);
		return;
	}
}

static void
cmd_rm(const char **argv, uint8_t argc)
{
	FRESULT rv;
	uint8_t i;

	if (argc < 2) {
		logf((_P("ARGS: rm file ...\n")));
		return;
	} 

	for (i = 1; i < argc; ++i) {
		rv = f_unlink(argv[i]);
		if (rv != FR_OK) {
			logf((_P("can't rm '%s': %u\n"), argv[i], rv));
		}
	}
}

static void
cmd_free(const char **argv, uint8_t argc)
{
	extern char *__brkval;

	logf((_P("%04u %04u %04u : used/free/large\n"),
		getMemoryUsed(),
		getFreeMemory(),
		getLargestAvailableMemoryBlock()));
#if 0
	logf((_P("AVR_STACK_POINTER_REG = %p\n"), AVR_STACK_POINTER_REG));
	logf((_P("__malloc_margin = %p\n"), __malloc_margin));
	logf((_P("__malloc_heap_start = %p\n"), __malloc_heap_start));
	logf((_P("__malloc_heap_end = %p\n"), __malloc_heap_end));
	logf((_P("__brkval = %p\n"), __brkval));
	logf((_P("freeListSize = %u\n"), (unsigned)getFreeListSize()));
#endif
}

static void
cmd_pool(const char **argv, int argc)
{
	test_pool_run_all();
	//v_assert(0 == 0);
}

static void
cmd_sha204(const char **argv, int argc)
{
	uint8_t rv;
	uint8_t i;
	const uint8_t *p;
	sha204_config_t conf;

	rv = sha204_get_config(conf);
	if (rv) {
		puts_P(_P("can't get sha204 config"));
		return;
	}

	printf_P(_P("serial = "));
	p = sha204_config_sn_first(conf);
	for (i = 0; i < SN_FIRST_LEN; ++i)
		phex(p[i]);
	p = sha204_config_sn_last(conf);
	for (i = 0; i < SN_LAST_LEN; ++i)
		phex(p[i]);
	putchar('\n');

	printf_P(_P("revision = "));
	p = sha204_config_rev(conf);
	for (i = 0; i < REV_LEN; ++i)
		phex(p[i]);
	putchar('\n');

	logf((_P("i2c_enabled = 0x%x\n"), sha204_config_i2c_enabled(conf)));
	logf((_P("i2c_addr = 0x%x\n"), sha204_config_i2c_addr(conf)));
	logf((_P("otp_mode = 0x%x\n"), sha204_config_otp_mode(conf)));
	logf((_P("selector_mode = 0x%x\n"), sha204_config_selector_mode(conf)));

	if (argc >= 2 && strcmp_P(argv[1], _P("lock")) == 0) {
		uint8_t *rxb, *txb;
	
		txb = alloca(LOCK_COUNT);
		rxb = alloca(LOCK_RSP_SIZE);
		puts_P(_P("Locking config..."));
		// XXX set ignore CRC, bit 7
		if (sha204m_lock(txb, rxb, _BV(7), 0)) {
			puts_P(_P("Failed to lock sha204 config"));
		}
	}
}

static void
cmd_vfatfs(const char **argv, int argc)
{
	struct pool *p;

	p = pool_init(5);
	if (!p) {
		logf(_P("can't alloc pool\n"));
		return;
	}
	cmd_free(NULL, 0);
	
	test_vfs_run_all(&fatfs_vfs, p);

	pool_free(p);
}

static void
cmd_accdb(const char **argv, int argc)
{
	struct pool *p;

	p = pool_init(8);
	if (!p) {
		logf(_P("can't alloc pool\n"));
		return;
	}
	cmd_free(NULL, 0);
	if (argc > 1)
		test_accdb_crypt(&fatfs_vfs, p);
	else
		test_accdb_plaintext(&fatfs_vfs, p);

	pool_free(p);
}

static void
cmd_crypto(const char **argv, int argc)
{
	struct pool *p;

	p = pool_init(6);
	if (!p) {
		logf(_P("can't alloc pool\n"));
		return;
	}
	test_vfs_crypt_run_all(&fatfs_vfs, p);

	pool_free(p);
}

static void map_command(const char **argv, int argc);

static void
cmd_loop(const char **argv, int argc)
{
	unsigned long cnt;

	if (argc < 3) {
		puts_P(_P("ARGS: loop <count> <prog> <arg1> <argn>"));
		return;
	}
	
	cnt = strtoul(argv[1], NULL, 0);
	
	while (cnt) {
		logf((_P("\niteration: %lu\n"), cnt));
		map_command(argv + 2, argc - 2);
		cnt--;
	}
}

static void
cmd_lcd(const char **argv, int argc)
{
	int b;

	if (argc < 2) {
		puts_P(_P("ARGS: lcd <backlight|clear|puts>"));
		return;
	}


	if (!strcmp_P(argv[1], _P("backlight"))) {
		if (argc < 3 || (b = atoi(argv[2])) > 0xff) {
			puts_P(_P("ARGS: lcd backlight 0-255"));
			return;
		}
		if (b == 0) {
			lcd_backlight_off();
		} else {
			lcd_backlight_on();
			lcd_backlight_level_set(b);
		}
	} else if (!strcmp_P(argv[1], _P("clear"))) {
		lcd_command(LCD_CLEAR);
	} else if (!strcmp_P(argv[1], _P("puts"))) {
		uint8_t x, y;
		if (argc < 5) {
			puts_P(_P("ARGS: lcd puts x y str"));
			return;
		}
		x = atoi(argv[2]);
		y = atoi(argv[3]);
		lcd_set_cursor(x, y);
		fputs(argv[4], &lcd_stdout);
	} else if (!strcmp_P(argv[1], _P("mode"))) {
		if (argc < 3) {
			puts_P(_P("ARGS: lcd mode <rcurs|lcurs|rshift|lshift>"));
			return;
		}
		if (!strcmp_P(argv[2], _P("rcurs"))) {
			lcd_command(LCD_ENTRY_CURSOR_RIGHT);
		} else if (!strcmp_P(argv[2], _P("lcurs"))) {
			lcd_command(LCD_ENTRY_CURSOR_LEFT);
		} else if (!strcmp_P(argv[2], _P("rshift"))) {
			lcd_command(LCD_ENTRY_SHIFT_RIGHT);
		} else if (!strcmp_P(argv[2], _P("lshift"))) {
			lcd_command(LCD_ENTRY_SHIFT_LEFT);
		}
	} else if (!strcmp_P(argv[1], _P("entry"))) {
		uint8_t width, start;
		struct entry ent;
		int c;

		if (argc < 4) {
			puts_P(_P("ARGS: lcd entry width start buf"));
			return;
		}
		width = atoi(argv[2]);
		start = atoi(argv[3]);
		lcd_entry_init(&ent, argv[4], strlen(argv[4]), 0, 0, width);
		ent.size_current = strlen(argv[4]);
		ent.pos = start;
		lcd_command(LCD_ON_CURSOR_BLINK);
		lcd_entry_render(&ent);
		while ((c = getescape()) != '\r' && c != EOF) {
			switch (c) {
			case ESC_SEQ:
				break;
			case ARROW_UP:
				break;
			case ARROW_DN:
				break;
			case ARROW_LT:
				lcd_entry_left(&ent);
				break;
			case ARROW_RT:
				lcd_entry_right(&ent);
				break;
			case 127:
				lcd_entry_backspace(&ent);
				break;
			default:
				lcd_entry_putc(&ent, c);
				break;
			}
			lcd_entry_render(&ent);
		}
		lcd_command(LCD_ON);
	}

	putchar('\n');
}

static void
map_command(const char **argv, int argc)
{
	const char *cmd = argv[0];

	if (strcmp_P(cmd, _P("ls")) == 0)
		cmd_ls(argv, argc);
	else if (strcmp_P(cmd, _P("rm")) == 0)
		cmd_rm(argv, argc);
	else if (strcmp_P(cmd, _P("cd")) == 0)
		cmd_cd(argv, argc);
	else if (strcmp_P(cmd, _P("cat")) == 0)
		cmd_cat(argv, argc);
	else if (strcmp_P(cmd, _P("sha1")) == 0)
		cmd_sha1(argv, argc);
	else if (strcmp_P(cmd, _P("rand")) == 0)
		cmd_rand(argv, argc);
	else if (strcmp_P(cmd, _P("pbkdf2")) == 0)
		cmd_pbkdf2(argv, argc);
	else if (strcmp_P(cmd, _P("aes")) == 0)
		cmd_aes(argv, argc);
	else if (strcmp_P(cmd, _P("hd")) == 0)
		cmd_hd(argv, argc);
	else if (strcmp_P(cmd, _P("pool")) == 0)
		cmd_pool(argv, argc);
	else if (strcmp_P(cmd, _P("vfatfs")) == 0)
		cmd_vfatfs(argv, argc);
	else if (strcmp_P(cmd, _P("accdb")) == 0)
		cmd_accdb(argv, argc);
	else if (strcmp_P(cmd, _P("crypto")) == 0)
		cmd_crypto(argv, argc);
	else if (strcmp_P(cmd, _P("free")) == 0)
		cmd_free(argv, argc);
	else if (strcmp_P(cmd, _P("loop")) == 0)
		cmd_loop(argv, argc);
	else if (strcmp_P(cmd, _P("sha204")) == 0)
		cmd_sha204(argv, argc);
	else if (strcmp_P(cmd, _P("lcd")) == 0)
		cmd_lcd(argv, argc);
	else
		printf_P(PSTR("Unknown command '%s'\n"), cmd);
}

uint16_t
analog_read(uint8_t p)
{
	uint8_t high, low;

	ADCSRA = _BV(ADEN);
	ADCSRB = p & 0x20;

	ADMUX = _BV(REFS0) | (p & 0x1f);

	ADCSRA |= _BV(ADSC);
	while (bit_is_set(ADCSRA, ADSC))
		;

	low = ADCL;
	high = ADCH;

	return (high << 8) | low;
}

int
main(void)
{
	FRESULT rv;
	FATFS vol;
	char ln[64];
#define ARGV_MAX 8
	const char *argv[ARGV_MAX];
	int argc;
	char *p;

	clock_prescale_set(clock_div_1);
	sei();

	lcd_backlight_on();
	sei();
	usb_init();
	lcd_init();

	lcd_set_cursor(0,0);
	fputs("I'm on baby", &lcd_stdout);
	stdout = &usb_stdout;
	stderr = &usb_stdout;
	stdin = &usb_stdin;

	//keyboardInit();

	// set up 100hz tick
	OCR1A = F_CPU / 256 / 100 - 1;
	TCCR1B = _BV(WGM12) | _BV(CS12);
	TIMSK1 = _BV(OCIE1A);

	rv = f_mount(0, &vol);
	if (rv != FR_OK) {
		abort();
	}

	rv = f_chdir("/");
	if (rv != FR_OK) {
		abort();
	}
	f_getcwd(current_path, sizeof(current_path));

	logf((_P("sizeof(FATFS) = %u\n"), sizeof(FATFS)));
	cmd_free(NULL, 0);

	/// XXX
	sha204p_init();
	lcd_backlight_level_set(255);

	while (1) {
		char *lnp;

		if (!usb_configured() ||
		    !(usb_serial_get_control() & USB_SERIAL_DTR))
			continue;
		printf_P(_P("%s $ "), current_path);
		if (!getline(ln, sizeof(ln), 1))
			continue;
		argc = 0;
		lnp = ln;
		while ((p = strsep(&lnp, " \t")) != NULL && argc < ARGV_MAX) {
			if (*p)
				argv[argc++] = p;
		}
		if (argc >= 1 && *argv[0])
			map_command(argv, argc);
	}

	return 0;
}

