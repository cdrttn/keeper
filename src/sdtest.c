#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/power.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include "usb_debug_only.h"
#include "print.h"
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

static const char*
kbgets(char *buf, size_t amt, uint8_t echo)
{
	char *p = buf;
	char *end = buf + amt - 1;
	uint8_t c = 0;

	while (p < end && (c = kbgetch()) != '\r') {
		if (c == BS) {
			if (p > buf)
				p--;
		} else
			*p++ = (char)c;
		if (echo)
			pchar(c);
	}
	*p = '\0';
	if (echo)
		pchar('\n');

	return buf;
}
#endif

static void
hexdump(const void *buf, size_t amt)
{
	const uint8_t *b = buf;
	size_t i;

	for (i = 0; i < amt; ++i) {
		if ((i % 16) == 0)
			pchar('\n');
		phex(b[i]);
		pchar(' ');
	}
	pchar(' ');
}

#define MAXPATH 64
static char current_path[MAXPATH] = {0};

static void
cmd_cd(const char **argv, uint8_t argc)
{
	FRESULT rv;

	if (argc != 2) {
		print("ARGS: cd <path>\n");
		return;
	}

	rv = f_chdir(argv[1]);
	if (rv != FR_OK) {
		printf_P(PSTR("Can't chdir: %u\n"), rv);
		return;
	}
	f_getcwd(current_path, sizeof(current_path));
}

static void
cmd_cat(const char **argv, uint8_t argc)
{
	FIL f;
	FRESULT rv;
	UINT len;
	char buf[64];

	if (argc != 2) {
		print("ARGS: cat <path>\n");
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
	pchar('\n');
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
		pchar('\n');
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
	pchar('\n');
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
	//pchar('\n');
	rv = crypto_cipher_sector(NULL, key, iv, C_DEC, c, p);
	if (rv < 0) {
		logf((_P("decryption failed\n")));
		goto out;
	}
	//hexdump(p, 512);
	//pchar('\n');
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
		print("ARGS: hd <offset> <length> <path>\n");
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
			pchar(' ');
			if (!((off+1) % 16))
				pchar('\n');
		}
	} while (len == rd);
	pchar('\n');

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
		print("can't get sha204 config");
		return;
	}

	print("serial = ");
	p = sha204_config_sn_first(conf);
	for (i = 0; i < SN_FIRST_LEN; ++i)
		phex(p[i]);
	p = sha204_config_sn_last(conf);
	for (i = 0; i < SN_LAST_LEN; ++i)
		phex(p[i]);
	pchar('\n');

	print("revision = ");
	p = sha204_config_rev(conf);
	for (i = 0; i < REV_LEN; ++i)
		phex(p[i]);
	pchar('\n');

	logf((_P("i2c_enabled = 0x%x\n"), sha204_config_i2c_enabled(conf)));
	logf((_P("i2c_addr = 0x%x\n"), sha204_config_i2c_addr(conf)));
	logf((_P("otp_mode = 0x%x\n"), sha204_config_otp_mode(conf)));
	logf((_P("selector_mode = 0x%x\n"), sha204_config_selector_mode(conf)));
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
		print("ARGS: loop <count> <prog> <arg1> <argn>\n");
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
#if 0
	FRESULT rv;
	FATFS vol;
	char ln[64];
#define ARGV_MAX 8
	const char *argv[ARGV_MAX];
	int argc;
	char *p;

	CPU_PRESCALE(0);
	stdout = &dbg_stdout;
	stderr = &dbg_stdout;

	keyboardInit();

	// set up 100hz tick
	OCR1A = F_CPU / 256 / 100 - 1;
	TCCR1B = _BV(WGM12) | _BV(CS12);
	TIMSK1 = _BV(OCIE1A);
	sei();

	//usb_init();

	//_delay_ms(2000);
	rv = f_mount(0, &vol);
	if (rv != FR_OK) {
		print("failed to mount!\n");
		usb_debug_flush_output();
		abort();
	}

	rv = f_chdir("/");
	if (rv != FR_OK) {
		print("failed to chdir '/'\n");
		usb_debug_flush_output();
		abort();
	}
	f_getcwd(current_path, sizeof(current_path));

	logf((_P("sizeof(FATFS) = %u\n"), sizeof(FATFS)));
	cmd_free(NULL, 0);

	/// XXX
	sha204p_init();

	while (1) {
		char *lnp;

		printf_P(PSTR(" %s $ "), current_path);
		usb_debug_flush_output();
		kbgets(ln, sizeof(ln), 1);
		argc = 0;
		lnp = ln;
		while ((p = strsep(&lnp, " \t")) != NULL && argc < ARGV_MAX) {
			argv[argc++] = p;
		}
		if (argc >= 1 && *argv[0])
			map_command(argv, argc);
		//print("1.HERE\n");
	}
#endif
	clock_prescale_set(clock_div_1);

	lcd_backlight_on();
	sei();
	usb_init();
	lcd_init();
	lcd_set_cursor(1, 1);
	fprintf_P(&lcd_stdout, _P("HELLO World!"));
	unsigned a = 0;

	while (!usb_configured())
		;

	lcd_backlight_level_set(a);
	usb_get_echo = 1;
	while (1) {
		if (usb_configured() && (usb_serial_get_control() & USB_SERIAL_DTR)) {
			char buf[16];

			fprintf_P(&usb_stdout, _P("Bright: "));
			if (fgets(buf, sizeof(buf), &usb_stdin) &&
			    sscanf(buf, "%u", &a) == 1) {
				fprintf_P(&usb_stdout, _P("Enter: %u\r"), a);
			}
			if (a != lcd_backlight_level_get()) {
				lcd_set_cursor(1, 2);
				fprintf_P(&lcd_stdout, _P("Bright: %03u"), a & 0xff);
				lcd_backlight_level_set(a);
			}
		}
	}
	return 0;
}

