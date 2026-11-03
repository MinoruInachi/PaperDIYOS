// extmod/modtime.c から取り込まれる。時刻は OS の時計（ESP32 の RTC タイマー）。

#include <sys/time.h>
#include <time.h>

#include "py/obj.h"
#include "shared/timeutils/timeutils.h"

static void mp_time_localtime_get(timeutils_struct_time_t *tm) {
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    tm->tm_year = t.tm_year + 1900;
    tm->tm_mon = t.tm_mon + 1;
    tm->tm_mday = t.tm_mday;
    tm->tm_hour = t.tm_hour;
    tm->tm_min = t.tm_min;
    tm->tm_sec = t.tm_sec;
    tm->tm_wday = (t.tm_wday + 6) % 7;
    tm->tm_yday = t.tm_yday + 1;
}

static mp_obj_t mp_time_time_get(void) {
    return mp_obj_new_int_from_ll((long long)time(NULL));
}
