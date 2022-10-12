/*
 * Copyright 2015, alex at staticlibs.net
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* 
 * File:   CronExprParser_test.cpp
 * Author: alex
 *
 * Created on February 24, 2015, 9:36 AM
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "ccronexpr.h"

#define MAX_SECONDS 60
#define CRON_MAX_MINUTES 60
#define CRON_MAX_HOURS 24
#define CRON_MAX_DAYS_OF_WEEK 8
#define CRON_MAX_DAYS_OF_MONTH 32
#define CRON_MAX_MONTHS 12

#define INVALID_INSTANT ((time_t) -1)

#define DATE_FORMAT "%Y-%m-%d_%H:%M:%S"

#ifdef CRON_TEST_MALLOC
static int cronAllocations = 0;
static int cronTotalAllocations = 0;
static int maxAlloc = 0;
void* cronMalloc(size_t n) {
    cronAllocations++;
    cronTotalAllocations++;
    if (cronAllocations > maxAlloc) {
        maxAlloc = cronAllocations;
    }
    return malloc(n);
}

void cronFree(void* p) {
    cronAllocations--;
    free(p);
}
#endif

#ifndef ANDROID
#ifndef _WIN32
time_t timegm(struct tm* __tp);
#else /* _WIN32 */
static time_t timegm(struct tm* tm) {
    return _mkgmtime(tm);
}
#endif /* _WIN32 */
#else /* ANDROID */
static time_t timegm(struct tm * const t) {
    /* time_t is signed on Android. */
    static const time_t kTimeMax = ~(1L << (sizeof (time_t) * CHAR_BIT - 1));
    static const time_t kTimeMin = (1L << (sizeof (time_t) * CHAR_BIT - 1));
    time64_t result = timegm64(t);
    if (result < kTimeMin || result > kTimeMax)
    return -1;
    return result;
}
#endif

static int crons_equal(cron_expr* cr1, cron_expr* cr2) {
    unsigned int i;
    for (i = 0; i < ARRAY_LEN(cr1->seconds); i++) {
        if (cr1->seconds[i] != cr2->seconds[i]) {
            printf("seconds not equal @%d %02x != %02x", i, cr1->seconds[i], cr2->seconds[i]);
            return 0;
        }
    }
    for (i = 0; i < ARRAY_LEN(cr1->minutes); i++) {
        if (cr1->minutes[i] != cr2->minutes[i]) {
            printf("minutes not equal @%d %02x != %02x", i, cr1->minutes[i], cr2->minutes[i]);
            return 0;
        }
    }
    for (i = 0; i < ARRAY_LEN(cr1->hours); i++) {
        if (cr1->hours[i] != cr2->hours[i]) {
            printf("hours not equal @%d %02x != %02x", i, cr1->hours[i], cr2->hours[i]);
            return 0;
        }
    }
    for (i = 0; i < ARRAY_LEN(cr1->days_of_week); i++) {
        if (cr1->days_of_week[i] != cr2->days_of_week[i]) {
            printf("days_of_week not equal @%d %02x != %02x", i, cr1->days_of_week[i], cr2->days_of_week[i]);
            return 0;
        }
    }
    for (i = 0; i < ARRAY_LEN(cr1->days_of_month); i++) {
        if (cr1->days_of_month[i] != cr2->days_of_month[i]) {
            printf("days_of_month not equal @%d %02x != %02x", i, cr1->days_of_month[i], cr2->days_of_month[i]);
            return 0;
        }
    }
    for (i = 0; i < ARRAY_LEN(cr1->months); i++) {
        if (cr1->months[i] != cr2->months[i]) {
            printf("months not equal @%d %02x != %02x", i, cr1->months[i], cr2->months[i]);
            return 0;
        }
    }
    return 1;
}

int one_dec_num(const char ch) {
    switch (ch) {
    case '0':
        return 0;
    case '1':
        return 1;
    case '2':
        return 2;
    case '3':
        return 3;
    case '4':
        return 4;
    case '5':
        return 5;
    case '6':
        return 6;
    case '7':
        return 7;
    case '8':
        return 8;
    case '9':
        return 9;
    default:
        return -1;
    }
}

int two_dec_num(const char* first) {
    return one_dec_num(first[0]) * 10 + one_dec_num(first[1]);
}

/* strptime is not available in msvc */
/* 2012-07-01_09:53:50 */
/* 0123456789012345678 */
struct tm* poors_mans_strptime(const char* str) {
    struct tm* cal = (struct tm*) malloc(sizeof(struct tm));
    int year;
    sscanf(str, "%4d", &year);
    cal->tm_year = year - 1900;
    cal->tm_mon = two_dec_num(str + 5) - 1;
    cal->tm_mday = two_dec_num(str + 8);
    cal->tm_wday = 0;
    cal->tm_yday = 0;
    cal->tm_hour = two_dec_num(str + 11);
    cal->tm_min = two_dec_num(str + 14);
    cal->tm_sec = two_dec_num(str + 17);
    return cal;
}

void check_next(const char* pattern, const char* initial, const char* expected) {
    const char* err = NULL;
    cron_expr parsed;
    memset(&parsed, 0, sizeof(parsed));
    cron_parse_expr(pattern, &parsed, &err);
    if (err) printf("Error: %s\nPattern: %s\n", err, pattern);

    struct tm* calinit = poors_mans_strptime(initial);
    time_t dateinit = timegm(calinit);
    assert(-1 != dateinit);
    time_t datenext = cron_next(&parsed, dateinit);
    struct tm* calnext = gmtime(&datenext);
    assert(calnext);
    char* buffer = (char*) malloc(21);
    memset(buffer, 0, 21);
    strftime(buffer, 20, DATE_FORMAT, calnext);
    if (0 != strcmp(expected, buffer)) {
        printf("Pattern: %s\n", pattern);
        printf("Initial: %s\n", initial);
        printf("Expected: %s\n", expected);
        printf("Actual: %s\n", buffer);
        assert(0);
    }
    free(buffer);
    free(calinit);
}

void check_same(const char* expr1, const char* expr2) {
    cron_expr parsed1;
    memset(&parsed1, 0, sizeof(parsed1));
    cron_parse_expr(expr1, &parsed1, NULL);
    cron_expr parsed2;
    memset(&parsed2, 0, sizeof(parsed2));
    cron_parse_expr(expr2, &parsed2, NULL);
    assert(crons_equal(&parsed1, &parsed2));
}

void check_calc_invalid() {
    cron_expr parsed;
    memset(&parsed, 0, sizeof(parsed));
    cron_parse_expr("0 0 0 31 6 *", &parsed, NULL);
    struct tm * calinit = poors_mans_strptime("2012-07-01_09:53:50");
    time_t dateinit = timegm(calinit);
    time_t res = cron_next(&parsed, dateinit);
    assert(INVALID_INSTANT == res);
    free(calinit);
}

void check_expr_invalid(const char* expr) {
    const char* err = NULL;
    cron_expr test;
    memset(&test, 0, sizeof(test));
    cron_parse_expr(expr, &test, &err);
    if (!err) {
        printf("Error: '%s' parsed without an error\n", expr);
        assert(err);
    }
}

int testing_hash_function(int seed, uint8_t idx) {
    return seed * idx;
}

int fake_custom_hash_function(int seed, uint8_t idx)
{
    return seed * (idx + 1);
}

void test_expr() {
    check_next("*/15 * 1-4 * * *",  "2012-07-01_09:53:50", "2012-07-02_01:00:00");
    check_next("*/15 * 1-4 * * *",  "2012-07-01_09:53:00", "2012-07-02_01:00:00");
    check_next("0 */2 1-4 * * *",   "2012-07-01_09:00:00", "2012-07-02_01:00:00");
    check_next("* * * * * *",       "2012-07-01_09:00:00", "2012-07-01_09:00:01");
    check_next("* * * * * *",       "2012-12-01_09:00:58", "2012-12-01_09:00:59");
    check_next("10 * * * * *",      "2012-12-01_09:42:09", "2012-12-01_09:42:10");
    check_next("11 * * * * *",      "2012-12-01_09:42:10", "2012-12-01_09:42:11");
    check_next("10 * * * * *",      "2012-12-01_09:42:10", "2012-12-01_09:43:10");
    check_next("10-15 * * * * *",   "2012-12-01_09:42:09", "2012-12-01_09:42:10");
    check_next("10-15 * * * * *",   "2012-12-01_21:42:14", "2012-12-01_21:42:15");
    check_next("0 * * * * *",       "2012-12-01_21:10:42", "2012-12-01_21:11:00");
    check_next("0 * * * * *",       "2012-12-01_21:11:00", "2012-12-01_21:12:00");
    check_next("0 11 * * * *",      "2012-12-01_21:10:42", "2012-12-01_21:11:00");
    check_next("0 10 * * * *",      "2012-12-01_21:11:00", "2012-12-01_22:10:00");
    check_next("0 0 * * * *",       "2012-09-30_11:01:00", "2012-09-30_12:00:00");
    check_next("0 0 * * * *",       "2012-09-30_12:00:00", "2012-09-30_13:00:00");
    check_next("0 0 * * * *",       "2012-09-10_23:01:00", "2012-09-11_00:00:00");
    check_next("0 0 * * * *",       "2012-09-11_00:00:00", "2012-09-11_01:00:00");
    check_next("0 0 0 * * *",       "2012-09-01_14:42:43", "2012-09-02_00:00:00");
    check_next("0 0 0 * * *",       "2012-09-02_00:00:00", "2012-09-03_00:00:00");
    check_next("* * * 10 * *",      "2012-10-09_15:12:42", "2012-10-10_00:00:00");
    check_next("* * * 10 * *",      "2012-10-11_15:12:42", "2012-11-10_00:00:00");
    check_next("0 0 0 * * *",       "2012-09-30_15:12:42", "2012-10-01_00:00:00");
    check_next("0 0 0 * * *",       "2012-10-01_00:00:00", "2012-10-02_00:00:00");
    check_next("0 0 0 * * *",       "2012-08-30_15:12:42", "2012-08-31_00:00:00");
    check_next("0 0 0 * * *",       "2012-08-31_00:00:00", "2012-09-01_00:00:00");
    check_next("0 0 0 * * *",       "2012-10-30_15:12:42", "2012-10-31_00:00:00");
    check_next("0 0 0 * * *",       "2012-10-31_00:00:00", "2012-11-01_00:00:00");
    check_next("0 0 0 1 * *",       "2012-10-30_15:12:42", "2012-11-01_00:00:00");
    check_next("0 0 0 1 * *",       "2012-11-01_00:00:00", "2012-12-01_00:00:00");
    check_next("0 0 0 1 * *",       "2010-12-31_15:12:42", "2011-01-01_00:00:00");
    check_next("0 0 0 1 * *",       "2011-01-01_00:00:00", "2011-02-01_00:00:00");
    check_next("0 0 0 31 * *",      "2011-10-30_15:12:42", "2011-10-31_00:00:00");
    check_next("0 0 0 1 * *",       "2011-10-30_15:12:42", "2011-11-01_00:00:00");
    check_next("* * * * * 2",       "2010-10-25_15:12:42", "2010-10-26_00:00:00");
    check_next("* * * * * 2",       "2010-10-20_15:12:42", "2010-10-26_00:00:00");
    check_next("* * * * * 2",       "2010-10-27_15:12:42", "2010-11-02_00:00:00");
    check_next("55 5 * * * *",      "2010-10-27_15:04:54", "2010-10-27_15:05:55");
    check_next("55 5 * * * *",      "2010-10-27_15:05:55", "2010-10-27_16:05:55");
    check_next("20,40 5 * * * *",   "2010-10-27_15:06:30", "2010-10-27_16:05:20");
    check_next("20 6 * * * *",      "2010-10-27_15:06:30", "2010-10-27_16:06:20");
    check_next("20 5,7 16 * * *",   "2010-10-27_15:06:30", "2010-10-27_16:05:20");
    check_next("20,40 5 16 * * *",  "2010-10-27_15:06:30", "2010-10-27_16:05:20");
    check_next("20 5 15,17 28 * *", "2010-10-27_15:06:30", "2010-10-28_15:05:20");
    check_next("20,40 5 15,17 28 * *","2010-10-27_15:06:30", "2010-10-28_15:05:20");
    check_next("55 * 10 * * *",     "2010-10-27_09:04:54", "2010-10-27_10:00:55");
    check_next("55 * 10 * * *",     "2010-10-27_10:00:55", "2010-10-27_10:01:55");
    check_next("* 5 10 * * *",      "2010-10-27_09:04:55", "2010-10-27_10:05:00");
    check_next("* 5 10 * * *",      "2010-10-27_10:05:00", "2010-10-27_10:05:01");
    check_next("55 * * 3 * *",      "2010-10-02_10:05:54", "2010-10-03_00:00:55");
    check_next("55 * * 3 * *",      "2010-10-03_00:00:55", "2010-10-03_00:01:55");
    check_next("* * * 3 11 *",      "2010-10-02_14:42:55", "2010-11-03_00:00:00");
    check_next("* * * 3 11 *",      "2010-11-03_00:00:00", "2010-11-03_00:00:01");
    check_next("0 0 0 29 2 *",      "2007-02-10_14:42:55", "2008-02-29_00:00:00");
    check_next("0 0 0 29 2 *",      "2008-02-29_00:00:00", "2012-02-29_00:00:00");
    check_next("0 0 7 ? * MON-FRI", "2009-09-26_00:42:55", "2009-09-28_07:00:00");
    check_next("0 0 7 ? * MON-FRI", "2009-09-28_07:00:00", "2009-09-29_07:00:00");
    check_next("0 30 23 30 1/3 ?",  "2010-12-30_00:00:00", "2011-01-30_23:30:00");
    check_next("0 30 23 30 1/3 ?",  "2011-01-30_23:30:00", "2011-04-30_23:30:00");
    check_next("0 30 23 30 1/3 ?",  "2011-04-30_23:30:00", "2011-07-30_23:30:00");
    check_next("0 0 1 28 * ?",      "2022-02-28_02:00:00", "2022-03-28_01:00:00");
    check_next("0 0 0 * 12 ?",      "2022-01-01_00:00:00", "2022-12-01_00:00:00");
    // H Tests
    cron_init_custom_hash_fn(testing_hash_function);
    cron_init_hash(7);
    check_next("H H H H H ?",       "2022-05-12_00:00:00", "2022-05-22_14:07:00"); // 0 7 14 22 5 (1)
    check_next("H H H H H ?",       "2022-06-12_00:00:00", "2023-05-22_14:07:00");
    check_next("H H H ? H H",       "2022-05-12_00:00:00", "2022-05-16_14:07:00");
    check_next("H 0 1 * * ?",       "2022-05-12_00:00:00", "2022-05-12_01:00:00");
    check_next("H 0,12 1 * * ?",    "2022-05-12_01:01:00", "2022-05-12_01:12:00");
    check_next("H 0,H 1 * * ?",     "2022-05-12_01:01:00", "2022-05-12_01:07:00");
    check_next("H 0 1/4 * * ?",     "2022-05-12_01:01:00", "2022-05-12_05:00:00");
    check_next("H H 1 * * ?",       "2022-05-12_00:00:00", "2022-05-12_01:07:00");
    check_next("H H,H 1 * * ?",     "2022-05-12_00:00:00", "2022-05-12_01:07:00"); // H,H is same as H
    check_next("0 H/5 1 * * ?",     "2022-05-12_00:00:00", "2022-05-12_01:02:00");
    check_next("0 0 1 1 H/MAY ?",   "2022-05-12_00:00:00", "2022-06-01_01:00:00");
    check_next("0 0 1 1 H/MAY ?",   "2022-06-12_00:00:00", "2022-11-01_01:00:00");
    // TODO: Fix issue if generating iterator: Needs to be above 0?
    //check_next("0/H 0 1 * * *",     "2022-05-18_01:00:01", "2022-05-18_01:00:02");
    cron_init_hash(42);
    check_next("H H H H H ?",       "2022-05-12_00:00:00", "2023-01-19_12:42:00"); // 0 42 12(84) 19(126) 1(168) 1(210)
    check_next("H H H ? H H",       "2022-05-12_00:00:00", "2023-01-02_12:42:00");
    check_next("H 0 1 * * ?",       "2022-05-12_00:00:00", "2022-05-12_01:00:00");
    check_next("0 H/10 1 * * ?",    "2022-05-12_00:00:00", "2022-05-12_01:02:00");
    check_next("0 0 1 1 H/MAY ?",   "2022-05-12_00:00:00", "2022-06-01_01:00:00");
    cron_init_hash(12);
    check_next("H H H H H ?",       "2022-05-12_00:00:00", "2023-01-10_00:12:00"); // 0 12 0 10 1 4
    check_next("H H H ? H H",       "2022-05-12_00:00:00", "2023-01-05_00:12:00");
    // Tests for a custom hash function
    cron_custom_hash_fn custom_fn = fake_custom_hash_function;
    cron_init_custom_hash_fn(custom_fn);
    // TODO: Edit/check H values here
    check_next("H H H H H ?",       "2022-05-12_00:00:00", "2022-07-02_04:58:34"); // 7 14 21 2 3 0
    check_next("0 0 1 ? * H/TUE",   "2022-05-12_00:00:00", "2022-05-13_01:00:00");
    check_next("0 0 1 ? * TUE/H",   "2022-05-18_00:00:00", "2022-05-18_01:00:00");
    cron_init_custom_hash_fn(testing_hash_function);
    // W Tests
    check_next("0 0 1 4W * ?",      "2022-04-12_00:00:00", "2022-05-04_01:00:00");
    check_next("0 0 1 4W * ?",      "2022-05-12_00:00:00", "2022-06-03_01:00:00");
    check_next("0 0 1 1W * ?",      "2022-10-01_00:00:00", "2022-10-03_01:00:00");
    check_next("0 0 1 1W * ?",      "2022-10-03_00:00:00", "2022-10-03_01:00:00");
    check_next("0 0 1 16W * ?",     "2022-07-16_00:00:00", "2022-08-16_01:00:00");
    check_next("0 0 1 20W * ?",     "2022-08-20_00:00:00", "2022-09-20_01:00:00");
    check_next("0 0 1 1W * ?",      "2022-10-03_02:00:00", "2022-11-01_01:00:00");
    check_next("0 0 1 1W * ?",      "2022-09-01_00:00:00", "2022-09-01_01:00:00");
    check_next("0 0 1 1,3W * ?",    "2022-09-01_00:00:00", "2022-09-01_01:00:00");
    check_next("0 0 1 1,3W * ?",    "2022-09-02_00:00:00", "2022-09-02_01:00:00");
    check_next("0 0 1 1,3W * ?",    "2022-09-03_00:00:00", "2022-10-01_01:00:00");
    check_next("0 0 1 1,3W * ?",    "2022-10-02_00:00:00", "2022-10-03_01:00:00");
    check_next("0 0 1 1W,4W * ?",   "2022-09-01_00:00:00", "2022-09-01_01:00:00");
    check_next("0 0 1 1W,4W * ?",   "2022-09-02_00:00:00", "2022-09-05_01:00:00");
    check_next("0 0 1 1W,4W * ?",   "2022-06-03_00:00:00", "2022-06-03_01:00:00");
    check_next("0 0 1 1W,4W * ?",   "2022-09-03_00:00:00", "2022-09-05_01:00:00");
    check_next("0 0 1 1W,4W * ?",   "2022-10-01_00:00:00", "2022-10-03_01:00:00");
    check_next("0 0 1 1W,15W * ?",  "2022-09-01_00:00:00", "2022-09-01_01:00:00");
    check_next("0 0 1 1W,15W * ?",  "2022-10-01_00:00:00", "2022-10-03_01:00:00");
    check_next("0 0 1 1W,15W * ?",  "2022-09-02_00:00:00", "2022-09-15_01:00:00");
    check_next("0 0 1 1W,15W * ?",  "2022-01-01_00:00:00", "2022-01-03_01:00:00");
    check_next("0 0 1 1W,15W * ?",  "2022-01-04_00:00:00", "2022-01-14_01:00:00");
    check_next("0 0 1 1W,15W * ?",  "2022-01-15_00:00:00", "2022-02-01_01:00:00");
    check_next("0 0 1 8W,26W * ?",  "2022-01-06_00:00:00", "2022-01-07_01:00:00");
    check_next("0 0 1 8W,26W * ?",  "2022-01-26_00:00:00", "2022-01-26_01:00:00");
    check_next("0 0 1 8W,26W * ?",  "2022-02-26_00:00:00", "2022-03-08_01:00:00");
    check_next("0 0 1 8W,26W * ?",  "2022-03-09_00:00:00", "2022-03-25_01:00:00");
    check_next("0 0 1 29W * ?",     "2022-02-28_00:00:00", "2022-03-29_01:00:00");
    check_next("0 0 1 29W * ?",     "2024-02-28_00:00:00", "2024-02-29_01:00:00");
    check_next("0 0 1 31W * ?",     "2022-02-28_00:00:00", "2022-03-31_01:00:00");
    check_next("0 0 1 31W * ?",     "2022-06-17_00:00:00", "2022-07-29_01:00:00");
    check_next("0 0 1 31W * ?",     "2022-07-30_00:00:00", "2022-08-31_01:00:00");
    check_next("0 0 1 26W * ?",     "2022-06-27_00:00:00", "2022-06-27_01:00:00");
    check_next("H 0 1 26W * ?",     "2022-06-27_00:00:00", "2022-06-27_01:00:27");
    check_next("H 0 1 26W */H ?",   "2022-06-27_02:00:00", "2022-09-26_01:00:27");
    check_next("H 0 1 HW */H ?",    "2022-06-27_02:00:00", "2022-09-12_01:00:27");
    // L Tests
    check_next("0 0 1 LW * ?",    "2022-06-22_00:00:00", "2022-06-30_01:00:00");
    check_next("0 0 1 LW * ?",    "2022-07-01_00:00:00", "2022-07-29_01:00:00");
    check_next("0 0 1 LW * ?",    "2022-07-29_02:00:00", "2022-08-31_01:00:00");
    check_next("0 0 1 LW * ?",    "2022-10-01_00:00:00", "2022-10-31_01:00:00");
    check_next("0 0 1 LW * ?",    "2022-07-31_00:00:00", "2022-08-31_01:00:00");
    check_next("0 0 1 LW * ?",    "2022-07-30_00:00:00", "2022-08-31_01:00:00");
    check_next("H 0 H LW * ?",    "2022-10-01_00:00:00", "2022-10-31_22:00:27");
    check_next("0 0 1 L * ?",     "2022-05-12_00:00:00", "2022-05-31_01:00:00");
    check_next("0 0 1 L * ?",     "2022-02-12_00:00:00", "2022-02-28_01:00:00");
    check_next("0 0 1 L * ?",     "2020-02-12_00:00:00", "2020-02-29_01:00:00");
    check_next("0 0 1 L * ?",     "2021-02-12_00:00:00", "2021-02-28_01:00:00");
    check_next("0 0 1 ? * L",     "2022-05-12_00:00:00", "2022-05-15_01:00:00");
    check_next("0 0 1 ? * 4L",    "2022-05-12_00:00:00", "2022-05-26_01:00:00");
    check_next("0 0 1 ? * 1L",    "2022-03-29_00:00:00", "2022-04-25_01:00:00");
    check_next("0 0 1 ? * 5L",    "2022-06-25_00:00:00", "2022-07-29_01:00:00");
    check_next("0 0 1 L-2 * ?",   "2022-05-12_00:00:00", "2022-05-29_01:00:00");
    check_next("0 0 1 L-3 * ?",   "2020-02-12_00:00:00", "2020-02-26_01:00:00");
    check_next("0 0 1 L-30 * ?",  "2022-03-01_00:00:00", "2022-03-01_01:00:00");
    check_next("0 0 1 L-30 * ?",  "2022-01-02_00:00:00", "2022-02-01_01:00:00");
    check_next("0 0 1 L-31 * ?",  "2022-05-12_00:00:00", "2022-06-01_01:00:00");
    check_next("0 0 1 L-32 * ?",  "2022-05-12_00:00:00", "2022-06-01_01:00:00");
    check_next("0 0 1 L-31 2 ?",  "2022-01-01_00:00:00", "2022-02-01_01:00:00");
}

void test_parse() {

    check_same("* * * 2 * *", "* * * 2 * ?");
    check_same("57,59 * * * * *", "57/2 * * * * *");
    check_same("1,3,5 * * * * *", "1-6/2 * * * * *");
    check_same("* * 4,8,12,16,20 * * *", "* * 4/4 * * *");
    check_same("* * * * * 0-6", "* * * * * TUE,WED,THU,FRI,SAT,SUN,MON");
    check_same("* * * * * 0", "* * * * * SUN");
    check_same("* * * * * 0", "* * * * * 7");
    check_same("* * * * 1-12 *", "* * * * FEB,JAN,MAR,APR,MAY,JUN,JUL,AUG,SEP,OCT,NOV,DEC *");
    check_same("* * * * 2 *", "* * * * Feb *");
    check_same("*  *  * *  1 *", "* * * * 1 *");
    check_same("* * * * 1 L", "* * * * 1 SUN");
    check_same("* * * * * *", "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19-59,H 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18-59,H 0,1,2,3,4,5,6,7,8,9,10,11-23,H 1,2,3,4,5,6,7,8,9,10,11,12,13,14-31,H jan,feb,mar,apr,may,jun,jul,aug,sep,oct,nov,dec,H mon,tue,wed,thu,fri,sat,sun,H");
    // check default hash func
    // TODO: Implement check for valid expression
    /*cron_init_custom_hash_fn(NULL);
    check_expr_valid("H H H H H ?");
    check_expr_valid("H H H ? H H");
    cron_init_custom_hash_fn(testing_hash_function)*/

    check_expr_invalid("77 * * * * *");
    check_expr_invalid("44-77 * * * * *");
    check_expr_invalid("* 77 * * * *");
    check_expr_invalid("* 44-77 * * * *");
    check_expr_invalid("* * 27 * * *");
    check_expr_invalid("* * 23-28 * * *");
    check_expr_invalid("* * * 45 * *");
    check_expr_invalid("* * * 28-45 * *");
    check_expr_invalid("0 0 0 25 13 ?");
    check_expr_invalid("0 0 0 25 0 ?");
    check_expr_invalid("0 0 0 32 12 ?");
    check_expr_invalid("* * * * 11-13 *");
    check_expr_invalid("0 0 1 1-3W * ?");
    check_expr_invalid("0 0 1 1/3W * ?");
    check_expr_invalid("0 0 1 1W/3 * ?");
    check_expr_invalid("0 0 1 16WL * ?");
    check_expr_invalid("0 0 1 16LW * ?");
    check_expr_invalid("0 0 1 W3 * ?");
    check_expr_invalid("0 0 1 WL * ?");
    check_expr_invalid("0 0 1 10L * ?");
    check_expr_invalid("0 0 1 L/7 * ?");
    check_expr_invalid("0 0 1 HLW * ?");
    check_expr_invalid("0 0 1 HL/H * ?");
    check_expr_invalid("0 0 1 HL/HW * ?");
    check_expr_invalid("0 0 1 ? * 5L,SUN");
    check_expr_invalid("0 0 1 ? * H/L");
    check_expr_invalid("0 0 1 ? * 19L");
    check_expr_invalid("0 0 1 17 * 5L");
    check_expr_invalid("0 0 1 ? * L-7");
    check_expr_invalid("0 0 1 ? * 5L-7");
    check_expr_invalid("0 0 1 5L-7 * ?");
    check_expr_invalid("0 0 1 L12 * ?");
    check_expr_invalid("0 0 1 L12- * ?");
    check_expr_invalid("0 0 1 L1-4 * ?");
    // H can not be used in ranges
    check_expr_invalid("H H-H 1 * * ?" ); // H-H Must be error, H can not be used in ranges
    check_expr_invalid("H H-H 1 * * ?"); // H-60 Must be error, H can not be used in ranges
    check_expr_invalid("H H-60 1 * * ?"); // H-60 Must be error, H can not be used in ranges
    check_expr_invalid("H 1-H 1 * * ?"); // H can not be used in ranges
    check_expr_invalid("1-H 0 1 * * ?"); // H can not be used in ranges
    check_expr_invalid("1-H 0 1 * * ?"); // H can not be used in ranges
    check_expr_invalid("0 0 1-H * * ?"); // H can not be used in ranges
    check_expr_invalid("0 0 1 1-H * ?"); // H can not be used in ranges
    check_expr_invalid("0 0 1 * 1-H ?"); // H can not be used in ranges
    check_expr_invalid("0 0 1 ? * 1-H"); // H can not be used in ranges
    // Invalid iterator values
    check_expr_invalid("0/60 * * * * *");
    check_expr_invalid("* 0/60 * * * *");
    check_expr_invalid("* * 0/24 * * *");
    check_expr_invalid("* * * 1/32 * *");
    check_expr_invalid("* * * * 1/13 *");
    check_expr_invalid("* * * * * 1/8");
    check_expr_invalid("* * * * * 1/-1");
}

void test_bits() {

    uint8_t testbyte[8];
    memset(testbyte, 0, 8);
    int err = 0;
    int i;

    for (i = 0; i <= 63; i++) {
        cron_setBit(testbyte, i);
        if (!cron_getBit(testbyte, i)) {
            printf("Bit set error! Bit: %d!\n", i);
            err = 1;
        }
        cron_delBit(testbyte, i);
        if (cron_getBit(testbyte, i)) {
            printf("Bit clear error! Bit: %d!\n", i);
            err = 1;
        }
        assert(!err);
    }

    for (i = 0; i < 12; i++) {
        cron_setBit(testbyte, i);
    }
    if (testbyte[0] != 0xff) {
        err = 1;
    }
    if (testbyte[1] != 0x0f) {
        err = 1;
    }

    assert(!err);
}

/* For this test to work you need to set "-DCRON_TEST_MALLOC=1"*/
#ifdef CRON_TEST_MALLOC
void test_memory() {
    cron_expr cron;
    const char* err;

    cron_parse_expr("* * * * * *", &cron, &err);
    if (cronAllocations != 0) {
        printf("Allocations != 0 but %d\n", cronAllocations);
        assert(0);
    }
    printf("Allocations: total: %d, max: %d\n", cronTotalAllocations, maxAlloc);
}
// TODO: Add test for non-nulled cron_expr
// TODO: Add tests for empty expressions if only unused bits are set
#endif

int main() {

    test_bits();

    test_expr();
    test_parse();
    check_calc_invalid();
    #ifdef CRON_TEST_MALLOC
    test_memory(); /* For this test to work you need to set "-DCRON_TEST_MALLOC=1"*/
    #endif
    printf("\nAll OK!");
    return 0;
}

