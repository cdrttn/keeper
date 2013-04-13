/* 
 * This file is part of keeper.
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

#ifndef _BUTTONS_H_
#define _BUTTONS_H_

#include "keeper.h"

struct button {
	uint16_t active;
	uint16_t pressed;
};

static inline bool_t
is_active_pressed(struct button *all, uint16_t b)
{
	return (all->active & all->pressed) & b;
}

static inline bool_t
is_active_released(struct button *all, uint16_t b)
{
	return (all->active & ~all->pressed) & b;
}

static inline bool_t
is_pressed(struct button *all, uint16_t b)
{
	return all->pressed & b;
}

static inline bool_t
is_released(struct button *all, uint16_t b)
{
	return (~all->pressed) & b;
}

void buttons_init(void);
typedef uint16_t (*sample_t)(void);
void buttons_start_sampling(sample_t sampler);
void buttons_end_sampling(void);
void buttons_wait(struct button *b);
msg_t buttons_wait_timeout(struct button *b, systime_t to);

#endif // _BUTTONS_H_
