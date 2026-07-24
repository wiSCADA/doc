#define _GNU_SOURCE
#include "common.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

void wall_time_utc_now(wall_time_t *out) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t sec = tv.tv_sec;
    struct tm tm;
    gmtime_r(&sec, &tm);
    out->year = tm.tm_year + 1900;
    out->month = tm.tm_mon + 1;
    out->day = tm.tm_mday;
    out->hour = tm.tm_hour;
    out->minute = tm.tm_min;
    out->second = tm.tm_sec;
    out->msec = (int)(tv.tv_usec / 1000);
    out->wday = tm.tm_wday;
}

void wall_time_apply_offset(const wall_time_t *utc, int offset_min, wall_time_t *out) {
    struct tm tm = {0};
    tm.tm_year = utc->year - 1900;
    tm.tm_mon = utc->month - 1;
    tm.tm_mday = utc->day;
    tm.tm_hour = utc->hour;
    tm.tm_min = utc->minute;
    tm.tm_sec = utc->second;
    tm.tm_isdst = 0;
    time_t t = timegm(&tm);
    t += (time_t)offset_min * 60;
    struct tm local;
    gmtime_r(&t, &local);
    out->year = local.tm_year + 1900;
    out->month = local.tm_mon + 1;
    out->day = local.tm_mday;
    out->hour = local.tm_hour;
    out->minute = local.tm_min;
    out->second = local.tm_sec;
    out->msec = utc->msec;
    out->wday = local.tm_wday;
}

void wall_time_format_hms(const wall_time_t *t, char *buf, size_t n) {
    snprintf(buf, n, "%02d:%02d:%02d", t->hour, t->minute, t->second);
}

void wall_time_format_date_cn(const wall_time_t *t, char *buf, size_t n) {
    static const char *wday[] = {
        "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"
    };
    snprintf(buf, n, "%04d年%02d月%02d日 农历演示 %s",
             t->year, t->month, t->day, wday[t->wday % 7]);
}
