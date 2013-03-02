#include <string.h>
#include <stdio.h>

#include "keeper.h"
#include "ch.h"
#include "test.h"
#include "evtimer.h"
#include "console.h"

#include "ff.h"

/*===========================================================================*/
/* Card insertion monitor.                                                   */
/*===========================================================================*/

#define POLLING_INTERVAL                10
#define POLLING_DELAY                   10
#if 0
/**
 * @brief   Card monitor timer.
 */
static VirtualTimer tmr;

/**
 * @brief   Debounce counter.
 */
static unsigned cnt;

/**
 * @brief   Card event sources.
 */
static EventSource inserted_event, removed_event;

/**
 * @brief   Insertion monitor timer callback function.
 *
 * @param[in] p         pointer to the @p BaseBlockDevice object
 *
 * @notapi
 */
static void tmrfunc(void *p) {
  BaseBlockDevice *bbdp = p;

  chSysLockFromIsr();
  if (cnt > 0) {
    if (blkIsInserted(bbdp)) {
      if (--cnt == 0) {
        chEvtBroadcastI(&inserted_event);
      }
    }
    else
      cnt = POLLING_INTERVAL;
  }
  else {
    if (!blkIsInserted(bbdp)) {
      cnt = POLLING_INTERVAL;
      chEvtBroadcastI(&removed_event);
    }
  }
  chVTSetI(&tmr, MS2ST(POLLING_DELAY), tmrfunc, bbdp);
  chSysUnlockFromIsr();
}

/**
 * @brief   Polling monitor start.
 *
 * @param[in] p         pointer to an object implementing @p BaseBlockDevice
 *
 * @notapi
 */
static void tmr_init(void *p) {

  chEvtInit(&inserted_event);
  chEvtInit(&removed_event);
  chSysLock();
  cnt = POLLING_INTERVAL;
  chVTSetI(&tmr, MS2ST(POLLING_DELAY), tmrfunc, p);
  chSysUnlock();
}
#endif
/*===========================================================================*/
/* FatFs related.                                                            */
/*===========================================================================*/

/**
 * @brief FS object.
 */
FATFS MMC_FS;

/**
 * MMC driver instance.
 */
MMCDriver MMCD1;

/* FS mounted and ready.*/
static bool_t fs_ready = FALSE;

/* Maximum speed SPI configuration (18MHz, CPHA=0, CPOL=0, MSb first).*/
static SPIConfig hs_spicfg = {NULL, GPIOA, GPIOA_SPI1_SS, 0};

/* Low speed SPI configuration (281.250kHz, CPHA=0, CPOL=0, MSb first).*/
static SPIConfig ls_spicfg = {NULL, GPIOA, GPIOA_SPI1_SS,
                              SPI_CR1_BR_2 | SPI_CR1_BR_1};

/* MMC/SD over SPI driver configuration.*/
static MMCConfig mmccfg = {&SPID1, &ls_spicfg, &hs_spicfg};

/* LCD backlight PWM (test) */
static PWMConfig lcd_pwmcfg = {
	1000000,
	256,
	NULL,
	{
		{PWM_OUTPUT_ACTIVE_HIGH, NULL},
		{PWM_OUTPUT_DISABLED, NULL},
		{PWM_OUTPUT_DISABLED, NULL},
		{PWM_OUTPUT_DISABLED, NULL}
	},
	0
};

/* ccm access */
static MemoryHeap fast_heap_obj;
MemoryHeap *fast_heap = &fast_heap_obj;

#if 0
/*
 * MMC card insertion event.
 */
static void InsertHandler(eventid_t id) {
  FRESULT err;

  (void)id;
  /*
   * On insertion MMC initialization and FS mount.
   */
  if (mmcConnect(&MMCD1)) {
    return;
  }
  err = f_mount(0, &MMC_FS);
  if (err != FR_OK) {
    mmcDisconnect(&MMCD1);
    return;
  }
  fs_ready = TRUE;
}

/*
 * MMC card removal event.
 */
static void RemoveHandler(eventid_t id) {

  (void)id;
  mmcDisconnect(&MMCD1);
  fs_ready = FALSE;
}
#endif

static void
mount_card(void)
{
  FRESULT err;

  /*
   * On insertion MMC initialization and FS mount.
   */
  if (mmcConnect(&MMCD1)) {
    return;
  }
  err = f_mount(0, &MMC_FS);
  if (err != FR_OK) {
    mmcDisconnect(&MMCD1);
    return;
  }
  fs_ready = TRUE;
}

/*
 * Application entry point.
 */
int main(void)
{
#if 0
	static const evhandler_t evhndl[] = {
		InsertHandler,
		RemoveHandler
	};
#endif
	/*
	* System initializations.
	* - HAL initialization, this also initializes the configured device drivers
	*   and performs the board-specific initializations.
	* - Kernel initialization, the main() function becomes a thread and the
	*   RTOS is active.
	*/
	halInit();
	chSysInit();
	crypto_init();
	buttons_init();

	chHeapInit(fast_heap, FAST_HEAP_ADDR, FAST_HEAP_SIZE);

	sdStart(&SD2, NULL);
	palSetPadMode(GPIOA, 2, PAL_MODE_ALTERNATE(7));
	palSetPadMode(GPIOA, 3, PAL_MODE_ALTERNATE(7));
	lcd_init();

	setvbuf(stdin, NULL, _IONBF, 0);

	mmcObjectInit(&MMCD1);
	mmcStart(&MMCD1, &mmccfg);
	mount_card();

	pwmStart(&PWMD1, &lcd_pwmcfg);
	palSetPadMode(GPIOA, 8, PAL_MODE_ALTERNATE(1));

	// XXX moveme
	palSetPadMode(GPIOB, 6, PAL_MODE_INPUT_PULLUP);
	palSetPadMode(GPIOB, 15, PAL_MODE_INPUT_PULLUP);
	palSetPadMode(GPIOB, 14, PAL_MODE_INPUT_PULLUP);
	palSetPadMode(GPIOC, 10, PAL_MODE_INPUT_PULLUP);
	palSetPadMode(GPIOC, 11, PAL_MODE_INPUT_PULLUP);

	fiprintf(lcd_stdout, "HIHI");
	while (TRUE) {
		console_cmd_loop();
		//chEvtDispatch(evhndl, chEvtWaitOne(ALL_EVENTS));
	}

	return 0;
}
