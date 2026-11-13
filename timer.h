#ifndef TIMER_H
#define TIMER_H

#include <types.h>
#include <sys/param.h>
#include <sys/interrupts.h>
#include <sys/sio.h>
#include <sys/locks.h>

uint8_t TCM_FACTOR;
void timer_init(void);

uint32_t timer_get_ticks(void);

uint32_t timer_get_us(void);
uint32_t timer_get_ms(void);

void timer_sleep_ms(uint32_t milisegundos);
void timer_sleep_us(uint32_t useg);

void timer_set_timeout(uint32_t u_seg, void (*ptFuncion)(void));

void timer_set_periodic(uint32_t u_seg, void (*ptFuncion)(void));

void timer_stop_periodic(void);

#endif //TIMER_H
