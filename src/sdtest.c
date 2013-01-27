#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "usb_debug_only.h"
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
#include "pbkdf2.h"
#include "bcal_aes256.h"
#include "bcal-cbc.h"

static void cmd_free(const char **argv, uint8_t argc);
static int
dbg_putc(char c, FILE *f)
{
	usb_debug_putchar(c);
	return 0;
}

static FILE dbg_stdout = FDEV_SETUP_STREAM(dbg_putc, NULL, _FDEV_SETUP_WRITE);

DWORD
get_fattime(void)
{
	return 0;
}
#define CPU_PRESCALE(n)	(CLKPR = 0x80, CLKPR = (n))

ISR(TIMER1_COMPA_vect)
{
	disk_timerproc();
}

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
cmd_pbkdf2(const char **argv, uint8_t argc)
{
	uint8_t hash[32];
	uint8_t i;
	uint16_t iter;
	int rv;

	if (argc < 4) {
		logf((_P("ARGS: pbkdf2 <pw> <salt> <iter>\n")));
		return;
	}

	iter = atoi(argv[3]);

	rv = pbkdf2_sha1(argv[1], strlen(argv[1]),
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
	uint8_t key[32];
	uint8_t iv[16];
	//uint8_t buf[512];
	uint8_t *buf;
	bcal_cbc_ctx_t ctx;
	uint16_t i;
	size_t len;

	if (argc < 4) {
		logf((_P("ARGS: aes <key> <iv> <data>\n")));
		return;
	}

	len = strlen(argv[1]);
	if (len > sizeof(key))
		len = sizeof(key);
	memset(key, 0, sizeof(key));
	memcpy(key, argv[1], len);
	buf = calloc(1, 512);
	if (buf == NULL) {
		logf((_P("cant alloc buf\n")));
		return;
	}
	if (bcal_cbc_init(&aes256_desc, key, sizeof(key)*8, &ctx)) {
		logf((_P("bcal_cbc_init failed\n")));
		free(buf);
		return;
	}

	len = strlen(argv[2]);
	if (len > sizeof(iv))
		len = sizeof(iv);
	memset(iv, 0, sizeof(iv));
	memcpy(iv, argv[2], len);

	memcpy(buf, argv[3], strlen(argv[3]));
	bcal_cbc_encMsg(iv, buf, 512/16, &ctx);

	for (i = 0; i < 512; ++i) {
		if ((i % 16) == 0)
			pchar('\n');
		phex(buf[i]);
		pchar(' ');
	}
	pchar('\n');

	bcal_cbc_decMsg(iv, buf, 512/16, &ctx);
	for (i = 0; i < 512; ++i) {
		if ((i % 16) == 0)
			pchar('\n');
		phex(buf[i]);
		pchar(' ');
	}
	pchar('\n');
	pchar('\n');
	cmd_free(NULL,0);
	bcal_cbc_free(&ctx);
	free(buf);
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

	p = pool_init(10);
	if (!p) {
		logf(_P("can't alloc pool\n"));
		return;
	}
	cmd_free(NULL, 0);
	kbgetch();
	test_accdb_plaintext(&fatfs_vfs, p);

	pool_free(p);
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
	else if (strcmp_P(cmd, _P("free")) == 0)
		cmd_free(argv, argc);
	else
		printf_P(PSTR("Unknown command '%s'\n"), cmd);
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

	CPU_PRESCALE(0);
	stdout = &dbg_stdout;
	stderr = &dbg_stdout;

	keyboardInit();

	// set up 100hz tick
	OCR1A = F_CPU / 256 / 100 - 1;
	TCCR1B = _BV(WGM12) | _BV(CS12);
	TIMSK1 = _BV(OCIE1A);
	sei();

	// initialize the USB, but don't want for the host to
	// configure.  The first several messages sent will be
	// lost because the PC hasn't configured the USB yet,
	// but we care more about blinking than debug messages!
	usb_init();

	_delay_ms(2000);
	
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

	return 0;
}

