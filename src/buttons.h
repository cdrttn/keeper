#ifndef _BUTTONS_H_
#define _BUTTONS_H_

#include "keeper.h"

struct button {
	uint16_t active;
	uint16_t pressed;
};

static inline bool_t
is_button_pressed(struct button *all, uint16_t b)
{
	return (all->active & all->pressed) & b;
}

static inline bool_t
is_button_released(struct button *all, uint16_t b)
{
	return (all->active & ~all->pressed) & b;
}

void buttons_init(void);
typedef uint16_t (*sample_t)(void);
void buttons_start_sampling(sample_t sampler);
void buttons_end_sampling(void);
void buttons_wait(struct button *b);
msg_t buttons_wait_timeout(struct button *b, systime_t to);

#endif // _BUTTONS_H_
