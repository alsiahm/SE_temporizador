#ifndef TIMER_H
#define TIMER_H

#include <types.h>
#include <sys/param.h>
#include <sys/interrupts.h>
#include <sys/sio.h>
#include <sys/locks.h>

static uint8_t TCM_FACTOR;
void timer_init(void);

uint32_t timer_get_ticks(void);

uint32_t timer_get_us(void);
uint32_t timer_get_ms(void);

void timer_sleep_ms(uint32_t milisegundos);
void timer_sleep_us(uint32_t useg);

int8_t timer_set_timeout(uint32_t u_seg, void (*ptFuncion)(void));

int8_t timer_set_periodic(uint32_t u_seg, void (*ptFuncion)(void));

void timer_stop(uint8_t canal);

#endif //TIMER_H
