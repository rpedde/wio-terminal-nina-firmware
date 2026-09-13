#include "nina_time.h"
int64_t nina_time_epoch(int year, int month, int day, int hour, int minute, int second) {
    if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 ||
        day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return 0;
    year -= month <= 2;
    int era = year / 400;
    unsigned y = year - era * 400;
    unsigned m = month > 2 ? month - 3 : month + 9;
    unsigned days = y * 365 + y / 4 - y / 100 + (153 * m + 2) / 5 + day - 1;
    return ((int64_t)era * 146097 + days - 719468) * 86400 + hour * 3600 + minute * 60 + second;
}

int64_t nina_time_sample(int64_t seconds, long micros, uint32_t elapsed_ms) {
    if (seconds < NINA_TIME_MIN || micros < 0 || micros >= 1000000) return 0;
    return seconds + elapsed_ms / 1000 + ((elapsed_ms % 1000) * 1000 + micros) / 1000000;
}
