#ifndef NINA_TIME_H
#define NINA_TIME_H
#include <stdbool.h>
#include <stdint.h>
#define NINA_TIME_MIN 946684800LL
bool nina_time_init(void);
void nina_time_link_changed(void);
int64_t nina_time_now(void);
int64_t nina_time_sample(int64_t seconds, long micros, uint32_t elapsed_ms);
/* UTC Gregorian conversion, also used for SDK-independent certificate checks. */
int64_t nina_time_epoch(int year, int month, int day, int hour, int minute, int second);
#endif
