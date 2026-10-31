#pragma once
#include <stdint.h>
#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif
typedef enum{ESP_SLEEP_WAKEUP_UNDEFINED=0,ESP_SLEEP_WAKEUP_TIMER=4}esp_sleep_wakeup_cause_t;
extern int gSleepCalls;extern esp_sleep_wakeup_cause_t gWakeCause;
inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(){return gWakeCause;}
inline int esp_sleep_enable_timer_wakeup(uint64_t){return 0;}
inline void esp_deep_sleep_start(){++gSleepCalls;}
