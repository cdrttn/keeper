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

#include <string.h>
#include <stdio.h>
#include "keeper.h"

#define MAX_CHECKS 10
static uint16_t buttons_debounced = 0;
static uint16_t buttons_active = 0;
static uint16_t debounce_state[MAX_CHECKS] = {0};
static uint8_t debounce_index = 0;
static Thread *button_thread = NULL;
static sample_t buttons_sample = NULL;

static MUTEX_DECL(button_mtx);
static CONDVAR_DECL(button_cond);
static WORKING_AREA(button_thread_stack, 128);

// debouncing method from, www.eng.utah.edu/~cs5780/debouncing.pdf
static uint16_t
debounce_buttons(void)
{
	uint8_t i;
	uint16_t j;
	uint16_t rv;

	debounce_state[debounce_index++] = buttons_sample();
	j = 0xffff;
	for (i = 0; i < MAX_CHECKS; ++i) {
		j &= debounce_state[i];
	}
	rv = buttons_debounced ^ j;
	buttons_debounced = j;
	if (debounce_index >= MAX_CHECKS)
		debounce_index = 0;

	return rv;
}

// wait for key to be held down for REPEAT_WAIT time before 
// repeating
#define REPEAT_WAIT 750
// repeat press after REPEAT_TIME
#define REPEAT_TIME 100
static msg_t
button_thread_fn(void *a)
{
	(void)a;
	uint16_t pressed = 0;
	uint16_t repeat = 0;
	uint16_t wait = 0;
	uint16_t active, check;

	// sample buttons every 1ms	
	while (!chThdShouldTerminate()) {
		chMtxLock(&button_mtx);
		active = debounce_buttons();
		if (active) {
			buttons_active = active;
			check = buttons_active & buttons_debounced;

			if ((check & pressed) == 0) {
				// keystroke has changed, reset repeat state
				pressed = check;
				repeat = 0;
				wait = 0;
				if (pressed) {
					wait = REPEAT_WAIT;
				}
			}
			chCondBroadcast(&button_cond);
		} else if (wait && --wait == 0) {
			// repeat wait is finished, begin repeating...
			buttons_active = pressed;
			chCondBroadcast(&button_cond);
			repeat = REPEAT_TIME;
		} else if (repeat && --repeat == 0) {
			// continue repeating
			buttons_active = pressed;
			chCondBroadcast(&button_cond);
			repeat = REPEAT_TIME;
		}
		chMtxUnlock();

		chThdSleepMilliseconds(1);
	}

	return (msg_t)0;
}

void
buttons_init(void)
{
	chMtxInit(&button_mtx);
	chCondInit(&button_cond);
}

void
buttons_start_sampling(sample_t sampler)
{
	if (button_thread)
		return;

	buttons_sample = sampler;
	button_thread = chThdCreateStatic(button_thread_stack,
					  sizeof(button_thread_stack),
					  NORMALPRIO, button_thread_fn,
					  NULL);
}

void
buttons_end_sampling(void)
{
	if (!button_thread)
		return;
	chThdTerminate(button_thread);
	chThdWait(button_thread);
	button_thread = NULL;
}

void
buttons_wait(struct button *b)
{
	chMtxLock(&button_mtx);
	if (!buttons_active)
		chCondWait(&button_cond);
	b->active = buttons_active;
	b->pressed = buttons_debounced;
	buttons_active = 0;
	chMtxUnlock();
}

msg_t
buttons_wait_timeout(struct button *b, systime_t to)
{
	msg_t rv;

	chMtxLock(&button_mtx);
	if (!buttons_active) {
		rv = chCondWaitTimeout(&button_cond, to);
		if (rv == RDY_TIMEOUT)
			return rv;
	}
	b->active = buttons_active;
	b->pressed = buttons_debounced;
	buttons_active = 0;
	chMtxUnlock();

	return rv;
}
