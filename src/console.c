#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alloca.h>

#include "keeper.h"

struct console_cmd {
	void (*cb)(const char **, int);
	const char *name;
	const char *help;
};

enum escape_seq {
	ESC_NONE,
	ESC_ESC,
	ESC_BRACKET,
	ESC_3,
};
enum keys {
	ESC_SEQ = -8,
	ARROW_UP = -10,
	ARROW_DN = -20,
	ARROW_LT = -30,
	ARROW_RT = -40,
	DEL_KEY = -50
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
		case '3':
			state = ESC_3;
			return ESC_SEQ;
		}
		break;
	case ESC_3:
		state = ESC_NONE;
		if (c == '~')
			return DEL_KEY;
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
		puts("\r");

	return buf;
}

static inline void
phex(uint8_t c)
{
	outf("%02x", c);
}

static void
hexdump(const void *buf, size_t amt)
{
	const uint8_t *b = buf;
	size_t i;

	for (i = 0; i < amt; ++i) {
		if ((i % 16) == 0)
			puts("\r");
		phex(b[i]);
		putchar(' ');
	}
	putchar(' ');
}

#define MAXPATH 64
static char current_path[MAXPATH] = {0};

static void
cmd_cd(const char **argv, int argc)
{
	FRESULT rv;

	if (argc != 2) {
		outf("ARGS: cd <path>\r\n");
		return;
	}

	rv = f_chdir(argv[1]);
	if (rv != FR_OK) {
		outf("Can't chdir: %u\r\n", rv);
		return;
	}
	f_getcwd(current_path, sizeof(current_path));
	puts("\r");
}

static void
cmd_cat(const char **argv, int argc)
{
	FIL f;
	FRESULT rv;
	UINT len;
	char *buf = NULL;

	if (argc != 2) {
		outf("ARGS: cat <path>\r\n");
		return;
	}

	rv = f_open(&f, argv[1], FA_READ);
	if (rv != FR_OK) {
		outf("Can't open %s: %u\r\n", argv[1], rv);
		return;
	}

#define BUF_SZ
	buf = fast_malloc(128);
	if (!buf) {
		outf("Can't alloc buf\r\n");
		goto out;
	}
		
	do {
		rv = f_read(&f, buf, sizeof(buf), &len);
		if (rv != FR_OK) {
			outf("Can't read: %u\r\n", rv);
			break;
		}
		fwrite(buf, len, 1, stdout);
	} while (len == sizeof(buf));

out:
	if (buf)
		fast_free(buf);
	f_close(&f);
#undef BUF_SZ
}

#if 0
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
#endif

static void
cmd_rand(const char **argv, int argc)
{
	uint8_t *buf;
	size_t len;

	if (argc < 2) {
		outf("ARGS: rand <nbytes>\r\n");
		return;
	}

	len = atoi(argv[1]);

	buf = fast_mallocz(len);
	if (!buf) {
		outf("failed to alloc buf\r\n");
		return;
	}

	if (crypto_get_rand_bytes(buf, len) < 0)
		outf("failed to get rand bytes\r\n");
	else {
		hexdump(buf, len);
		puts("\r");
	}
	
	fast_free(buf);
}

static void
cmd_pbkdf2(const char **argv, int argc)
{
	uint8_t hash[32];
	uint8_t i;
	uint16_t iter;
	int8_t rv;

	if (argc < 4) {
		outf("ARGS: pbkdf2 <pw> <salt> <iter>\r\n");
		return;
	}

	iter = atoi(argv[3]);

	rv = crypto_pbkdf2_sha1(argv[1], strlen(argv[1]),
			        argv[2], strlen(argv[2]),
				hash, sizeof(hash), iter);
	
	for (i = 0; i < sizeof(hash); ++i) {
		phex(hash[i]);
	}
	puts("\r");
}

static void
cmd_aes(const char **argv, int argc)
{
	uint8_t key[KEEPER_KEY_SIZE];
	uint8_t iv[KEEPER_IV_SIZE];
	uint8_t *p, *c;
	size_t len;
	int8_t rv;
	
	if (argc < 4) {
		outf("ARGS: aes <key> <iv> <data>\r\n");
		return;
	}

	len = strlen(argv[1]);
	if (len > sizeof(key))
		len = sizeof(key);
	memset(key, 0, sizeof(key));
	memcpy(key, argv[1], len);
	p = fast_mallocz(512);
	c = fast_mallocz(512);
	if (!p || !c) {
		outf("cant alloc buf\r\n");
		goto out;
	}

	len = strlen(argv[2]);
	if (len > sizeof(iv))
		len = sizeof(iv);
	memset(iv, 0, sizeof(iv));
	memcpy(iv, argv[2], len);

	memcpy(p, argv[3], strlen(argv[3]));
	rv = crypto_cipher_sector(NULL, key, iv, C_ENC, p, c);
	if (rv < 0) {
		outf("encryption failed\r\n");
		goto out;
	}
	hexdump(c, 512);
	puts("\r");
	rv = crypto_cipher_sector(NULL, key, iv, C_DEC, c, p);
	if (rv < 0) {
		outf("decryption failed\r\n");
		goto out;
	}
	hexdump(p, 512);
	puts("\r");
	if (memcmp(p, argv[3], strlen(argv[3]))) {
		outf("what? failed match\r\n");
		goto out;
	}
	outf("crypto good!\r\n");

out:
	fast_free(p);
	fast_free(c);
}

static void
cmd_hd(const char **argv, int argc)
{
	FIL f;
	FRESULT rv;
	UINT len, rd;
	DWORD start;
	DWORD span;
	char *buf = NULL;

	if (argc != 4) {
		outf("ARGS: hd <offset> <length> <path>\r\n");
		return;
	}

	start = strtoul(argv[1], NULL, 0);
	span = strtoul(argv[2], NULL, 0);

	rv = f_open(&f, argv[3], FA_READ);
	if (rv != FR_OK) {
		outf("Can't open %s: %u\r\n", argv[3], rv);
		return;
	}

	rv = f_lseek(&f, start);
	if (rv != FR_OK) {
		outf("Can't seek to %lu: %u\r\n", start, rv);
		goto out;
	}

#define BUF_SZ 128
	buf = fast_malloc(BUF_SZ);
	if (buf == NULL) {
		outf("Can't alloc buf\r\n");
		goto out;
	}

	outf("Dump from %lu to %lu...\r\n\r\n", start, start + span);
	do {
		unsigned off;

		rd = BUF_SZ;
		if (rd > span)
			rd = span;
		if (rd == 0)
			break;
		rv = f_read(&f, buf, rd, &len);
		if (rv != FR_OK) {
			outf("Can't read: %u\r\n", rv);
			goto out;
		}
		span -= rd;
		for (off = 0; off < len; ++off) {
			phex(buf[off]);
			putchar(' ');
			if (!((off+1) % 16))
				puts("\r");
		}
	} while (len == rd);
	puts("\r");

out:
	if (buf)
		fast_free(buf);
	f_close(&f);
#undef BUF_SZ
}

static void
cmd_ls(const char **argv, int argc)
{
	DIR d;
	FILINFO f;
	FRESULT rv;
	char dirc;
	const char *path = current_path;
	char *lfnbuf;
	const char *fn;

	lfnbuf = fast_malloc(_MAX_LFN + 1);
	f.lfname = lfnbuf;
	f.lfsize = _MAX_LFN + 1;

	if (argc >= 2) {
		rv = f_opendir(&d, argv[1]);
		path = argv[1];
	} else
		rv = f_opendir(&d, ".");

	if (rv != FR_OK) {
		outf("Can't open dir: %u\r\n", rv);
		goto out;
	}

	outf("Contents of %s...\r\n", path);
	while (1) {
		rv = f_readdir(&d, &f);
		if (rv != FR_OK || !f.fname[0])
			break;
		fn = f.fname;
		if (*f.lfname)
			fn = f.lfname;
		dirc = ' ';
		if (f.fattrib & AM_DIR)
			dirc = '/';
		outf("%12s%c %7lu Bytes\r\n", fn, dirc, f.fsize);
	}

	if (rv != FR_OK) {
		outf("failed dir list: %u\r\n", rv);
		goto out;
	}

out:
	fast_free(lfnbuf);
}

static void
cmd_rm(const char **argv, int argc)
{
	FRESULT rv;
	uint8_t i;

	if (argc < 2) {
		outf("ARGS: rm file ...\r\n");
		return;
	} 

	for (i = 1; i < argc; ++i) {
		rv = f_unlink(argv[i]);
		if (rv != FR_OK) {
			outf("can't rm '%s': %u\r\n", argv[i], rv);
		}
	}
}

static void
cmd_free(const char **argv, int argc)
{
	size_t n, size;

	(void)argv;
	(void)argc;
	outf("core free memory    : %u bytes\r\n", chCoreStatus());
	n = chHeapStatus(NULL, &size);
	outf("heap fragments      : %u\r\n", n);
	outf("heap free total     : %u bytes\r\n", size);
	n = chHeapStatus(fast_heap, &size);
	outf("fast heap fragments : %u\r\n", n);
	outf("fast heap free total: %u bytes\r\n", size);
}

static void
cmd_pool(const char **argv, int argc)
{
	(void)argv;
	(void)argc;
	test_pool_run_all();
	//v_assert(0 == 0);
}

static void
cmd_vfatfs(const char **argv, int argc)
{
	struct pool *p;

	(void)argv;
	(void)argc;

	p = pool_init(5);
	if (!p) {
		outf("can't alloc pool\r\n");
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

	(void)argv;
	p = pool_init(10);
	if (!p) {
		outf("can't alloc pool\r\n");
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

	(void)argv;
	(void)argc;

	p = pool_init(6);
	if (!p) {
		outf("can't alloc pool\r\n");
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
		outf("ARGS: loop <count> <prog> [<arg1> <argn>]\r\n");
		return;
	}
	
	cnt = strtoul(argv[1], NULL, 0);
	
	while (cnt) {
		outf("\r\niteration: %lu\r\n", cnt);
		map_command(argv + 2, argc - 2);
		cnt--;
	}
}

static void
cmd_time(const char **argv, int argc)
{
	struct TimeMeasurement tm;

	if (argc < 2) {
		outf("ARGS: time <prog> [<arg1> <argn>]\r\n");
		return;
	}

	tmObjectInit(&tm);
	tmStartMeasurement(&tm);
	map_command(argv + 1, argc - 1);
	tmStopMeasurement(&tm);

	outf("\r\n\r\nElapsed: %us %ums %uus (%u ticks)\r\n",
	     (unsigned)RTT2S(tm.last),
	     (unsigned)RTT2MS(tm.last) % 1000,
	     (unsigned)RTT2US(tm.last) % 1000,
	     (unsigned)tm.last);
}

#if 0
static const char line_1[] PROGMEM = "This line 1";
static const char line_2[] PROGMEM = "This line 2";
static const char line_3[] PROGMEM = "This line 3";
static const char line_4[] PROGMEM = "This line 4";
static const char line_5[] PROGMEM = "This line 5";
static const char line_6[] PROGMEM = "This line 6";
static const char line_7[] PROGMEM = "This line 7";
static const char line_8[] PROGMEM = "This line 8";
static const char line_9[] PROGMEM = "This line 9";
static const char line_10[] PROGMEM = "This line 10";
static const char line_11[] PROGMEM = "This line 11";

static const struct menu_item test_items[] = {
	{NULL, 0},
	{line_1, 0},
	{line_2, 1},
	{line_3, 2},
	{line_4, 3},
	{line_5, 4},
	{line_6, 5},
	{line_7, 6},
	{line_8, 7},
	{line_9, 8},
	{line_10, 9},
	{line_11, 10},
	{NULL, 0}
};

static void
cmd_lcd(const char **argv, int argc)
{
	int b;
	int c;

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
		char *buf;

		if (argc < 6 || (b = atoi(argv[5])) <= 0) {
			puts_P(_P("ARGS: lcd entry width start buf max [pw]"));
			return;
		}
		buf = alloca(b);
		memset(buf, 0, b);
		memcpy(buf, argv[4], strlen(argv[4]));
		width = atoi(argv[2]);
		start = atoi(argv[3]);
		lcd_set_cursor(0, 0);
		fprintf_P(&lcd_stdout, _P("IN: "));
		lcd_entry_init(&ent, buf, b, 4, 0, width);
		ent.size_current = strlen(argv[4]);
		ent.pos = start;
		if (argc > 6)
			ent.passwd = 1;
		lcd_command(LCD_ON_CURSOR_BLINK);
		lcd_entry_render(&ent);
		while ((c = getescape()) != '\r' && c != EOF) {
			switch (c) {
			case ESC_SEQ:
			case ARROW_UP:
			case ARROW_DN:
				continue;
			case ARROW_LT:
				lcd_entry_left(&ent);
				break;
			case ARROW_RT:
				lcd_entry_right(&ent);
				break;
			case 127:
				lcd_entry_backspace(&ent);
				break;
			case DEL_KEY:
				lcd_entry_delete(&ent);
				break;
			default:
				lcd_entry_putc(&ent, c);
				break;
			}
			lcd_set_cursor(0, 2);
			fprintf_P(&lcd_stdout,
				  _P("sz |szc|ps |psc"));
			lcd_set_cursor(0, 3);
			fprintf_P(&lcd_stdout,
				  _P("%03u|%03u|%03u|%03u"),
				  ent.size, ent.size_current,
				  ent.pos, ent.pos_cursor);
			lcd_entry_render(&ent);
		}
		putchar('\'');
		fwrite(buf, ent.size_current, 1, stdout);
		putchar('\'');
		putchar('\n');
		lcd_command(LCD_ON);
	} else if (!strcmp_P(argv[1], _P("menu"))) {
		struct menu menu;
		struct menu_item *item;

		lcd_set_cursor(0, 0);
		fprintf_P(&lcd_stdout, _P("Menu:"));
		lcd_menu_init_array(&menu, 0, 1, 20, 3, test_items);
		lcd_menu_render(&menu);
		while ((c = getescape()) != '\r' && c != EOF) {
			switch (c) {
			case ARROW_LT:
			case ARROW_RT:
			case ESC_SEQ:
				continue;
			case ARROW_UP:
				lcd_menu_up(&menu);
				break;
			case ARROW_DN:
				lcd_menu_down(&menu);
				break;
			}
			lcd_menu_render(&menu);
		}
	
		item = lcd_menu_get_current_item(&menu);
		if (item) {
			printf_P(_P("Item: '%S', '%u'\n"), item->text, item->index);
		}	
	}

	putchar('\n');
}
#endif

static void cmd_help(const char **argv, int argc);

static const struct console_cmd command_list[] = {
	{cmd_help, "help", "command help"},
	{cmd_free, "free", "display free ram"},
	{cmd_loop, "loop", "repeat command"},
	{cmd_time, "time", "get elapsed time for cmd"},
	{cmd_ls, "ls", "list files"},
	{cmd_ls, "dir", "list files"},
	{cmd_cd, "cd", "change directory"},
	{cmd_rm, "rm", "remove file"},
	{cmd_rm, "del", "remove file"},
	{cmd_cat, "cat", "view contents of file"},
	{cmd_cat, "type", "view contents of file"},
	{cmd_hd, "hd", "hex dump of file"},
	{cmd_rand, "rand", "test RNG"},
	{cmd_pbkdf2, "pbkdf2", "test pbkdf2_sha1"},
	{cmd_aes, "aes", "test aes-256-cbc"},
	{cmd_crypto, "crypto", "test crypto subsystem"},
	{cmd_accdb, "accdb", "test accdb subsystem"},
	{cmd_pool, "pool", "test pool subsystem"},
	{cmd_vfatfs, "vfatfs", "test vfatfs subsystem"},
	{NULL}
};

static void
cmd_help(const char **argv, int argc)
{
	const struct console_cmd *list;

	(void)argv;
	(void)argc;

	outf("Commands:\r\n\r\n");
	for (list = command_list; list->cb; ++list) {
		outf("%12s : %s\r\n", list->name, list->help);
	}
}

static void
map_command(const char **argv, int argc)
{
	const char *cmd = argv[0];
	const struct console_cmd *list;

	for (list = command_list; list->cb; ++list) {
		if (strcmp(list->name, cmd) == 0) {
			list->cb(argv, argc);
			return;
		}
	}

	outf("Unknown command, '%s'\r\n", cmd);
	outf("Try one of these:\r\n");
	for (list = command_list; list->cb; ++list) {
		outf("%s ", list->name);
	}
	puts("\r");
}

void
console_cmd_loop(void)
{
	char *ln;
#define ARGV_MAX 32
	const char *argv[ARGV_MAX];
	int argc;
	char *p;

#define LINE_MAX 128
	ln = fast_mallocz(LINE_MAX);
	f_getcwd(current_path, sizeof(current_path));

	while (1) {
		char *lnp;

		outf("%s $ ", current_path);
		//outf(" $ ");
		if (!getline(ln, LINE_MAX, 1))
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

	fast_free(ln);
}

