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
#include <stdbool.h>

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

void *cronMalloc(size_t n) {
    cronAllocations++;
    cronTotalAllocations++;
    if (cronAllocations > maxAlloc) {
        maxAlloc = cronAllocations;
    }
    return malloc(n);
}

void cronFree(void *p) {
    cronAllocations--;
    free(p);
}

#endif

#ifndef ANDROID
#ifndef _WIN32

time_t timegm(struct tm *__tp);

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

static int crons_equal(cron_expr *cr1, cron_expr *cr2) {
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

int two_dec_num(const char *first) {
    return one_dec_num(first[0]) * 10 + one_dec_num(first[1]);
}

/* strptime is not available in msvc */
/* 2012-07-01_09:53:50 */
/* 0123456789012345678 */
struct tm *poors_mans_strptime(const char *str) {
    struct tm *cal = (struct tm *) malloc(sizeof(struct tm));
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

bool check_next(const char *pattern, const char *initial, const char *expected) {
    const char *err = NULL;
    cron_expr parsed;
    cron_parse_expr(pattern, &parsed, &err);
    if (err) {
        printf("Error: %s\nPattern: %s\n", err, pattern);
        return false;
    }

    struct tm *calinit = poors_mans_strptime(initial);
    time_t dateinit = timegm(calinit);
    if (-1 == dateinit) return false;
    time_t datenext = cron_next(&parsed, dateinit);
    struct tm *calnext = gmtime(&datenext);
    if (calnext == NULL) return false;
    char *buffer = (char *) malloc(21);
    memset(buffer, 0, 21);
    strftime(buffer, 20, DATE_FORMAT, calnext);
    if (0 != strcmp(expected, buffer)) {
        printf("Pattern: %s\n", pattern);
        printf("Initial: %s\n", initial);
        printf("Expected: %s\n", expected);
        printf("Actual: %s\n", buffer);
        return false;
    }
    free(buffer);
    free(calinit);
    return true;
}

bool check_same(const char *expr1, const char *expr2) {
    cron_expr parsed1;
    cron_parse_expr(expr1, &parsed1, NULL);
    cron_expr parsed2;
    cron_parse_expr(expr2, &parsed2, NULL);
    if (!crons_equal(&parsed1, &parsed2)) {
        printf("\nThe following CRONs aren't equal, although they should be:\n");
        printf("%s\t%s\n", expr1, expr2);
        return false;
        //assert(crons_equal(&parsed1, &parsed2));
    }
    return true;
}

bool check_calc_invalid() {
    cron_expr parsed;
    cron_parse_expr("0 0 0 31 6 *", &parsed, NULL);
    struct tm *calinit = poors_mans_strptime("2012-07-01_09:53:50");
    time_t dateinit = timegm(calinit);
    time_t res = cron_next(&parsed, dateinit);
    free(calinit);
    if (INVALID_INSTANT != res) return false;
    return true;
}

bool check_expr_invalid(const char *expr) {
    const char *err = NULL;
    cron_expr test;
    cron_parse_expr(expr, &test, &err);
    if (!err) {
        printf("Error: '%s' parsed without an error (but it should)\n", expr);
        return false;
    }
    return true;
}

bool check_expr_valid(const char *expr) {
    const char *err = NULL;
    cron_expr test;
    cron_parse_expr(expr, &test, &err);
    if (err) {
        printf("Error: '%s' parsed with an error: %s\n", expr, err);
        return false;
    }
    return true;
}

int testing_hash_function(int seed, uint8_t idx) {
    return seed * idx;
}

int fake_custom_hash_function(int seed, uint8_t idx) {
    return seed * (idx + 1);
}

void test_expr() {
    assert(check_next("*/15 * 1-4 * * *",       "2012-07-01_09:53:50", "2012-07-02_01:00:00"));
    assert(check_next("*/15 * 1-4 * * *",       "2012-07-01_09:53:00", "2012-07-02_01:00:00"));
    assert(check_next("0 */2 1-4 * * *",        "2012-07-01_09:00:00", "2012-07-02_01:00:00"));
    assert(check_next("* * * * * *",            "2012-07-01_09:00:00", "2012-07-01_09:00:01"));
    assert(check_next("* * * * * *",            "2012-12-01_09:00:58", "2012-12-01_09:00:59"));
    assert(check_next("10 * * * * *",           "2012-12-01_09:42:09", "2012-12-01_09:42:10"));
    assert(check_next("11 * * * * *",           "2012-12-01_09:42:10", "2012-12-01_09:42:11"));
    assert(check_next("10 * * * * *",           "2012-12-01_09:42:10", "2012-12-01_09:43:10"));
    assert(check_next("10-15 * * * * *",        "2012-12-01_09:42:09", "2012-12-01_09:42:10"));
    assert(check_next("10-15 * * * * *",        "2012-12-01_21:42:14", "2012-12-01_21:42:15"));
    assert(check_next("0 * * * * *",            "2012-12-01_21:10:42", "2012-12-01_21:11:00"));
    assert(check_next("0 * * * * *",            "2012-12-01_21:11:00", "2012-12-01_21:12:00"));
    assert(check_next("0 11 * * * *",           "2012-12-01_21:10:42", "2012-12-01_21:11:00"));
    assert(check_next("0 10 * * * *",           "2012-12-01_21:11:00", "2012-12-01_22:10:00"));
    assert(check_next("0 0 * * * *",            "2012-09-30_11:01:00", "2012-09-30_12:00:00"));
    assert(check_next("0 0 * * * *",            "2012-09-30_12:00:00", "2012-09-30_13:00:00"));
    assert(check_next("0 0 * * * *",            "2012-09-10_23:01:00", "2012-09-11_00:00:00"));
    assert(check_next("0 0 * * * *",            "2012-09-11_00:00:00", "2012-09-11_01:00:00"));
    assert(check_next("0 0 0 * * *",            "2012-09-01_14:42:43", "2012-09-02_00:00:00"));
    assert(check_next("0 0 0 * * *",            "2012-09-02_00:00:00", "2012-09-03_00:00:00"));
    assert(check_next("* * * 10 * *",           "2012-10-09_15:12:42", "2012-10-10_00:00:00"));
    assert(check_next("* * * 10 * *",           "2012-10-11_15:12:42", "2012-11-10_00:00:00"));
    assert(check_next("0 0 0 * * *",            "2012-09-30_15:12:42", "2012-10-01_00:00:00"));
    assert(check_next("0 0 0 * * *",            "2012-10-01_00:00:00", "2012-10-02_00:00:00"));
    assert(check_next("0 0 0 * * *",            "2012-08-30_15:12:42", "2012-08-31_00:00:00"));
    assert(check_next("0 0 0 * * *",            "2012-08-31_00:00:00", "2012-09-01_00:00:00"));
    assert(check_next("0 0 0 * * *",            "2012-10-30_15:12:42", "2012-10-31_00:00:00"));
    assert(check_next("0 0 0 * * *",            "2012-10-31_00:00:00", "2012-11-01_00:00:00"));
    assert(check_next("0 0 0 1 * *",            "2012-10-30_15:12:42", "2012-11-01_00:00:00"));
    assert(check_next("0 0 0 1 * *",            "2012-11-01_00:00:00", "2012-12-01_00:00:00"));
    assert(check_next("0 0 0 1 * *",            "2010-12-31_15:12:42", "2011-01-01_00:00:00"));
    assert(check_next("0 0 0 1 * *",            "2011-01-01_00:00:00", "2011-02-01_00:00:00"));
    assert(check_next("0 0 0 31 * *",           "2011-10-30_15:12:42", "2011-10-31_00:00:00"));
    assert(check_next("0 0 0 1 * *",            "2011-10-30_15:12:42", "2011-11-01_00:00:00"));
    assert(check_next("* * * * * 2",            "2010-10-25_15:12:42", "2010-10-26_00:00:00"));
    assert(check_next("* * * * * 2",            "2010-10-20_15:12:42", "2010-10-26_00:00:00"));
    assert(check_next("* * * * * 2",            "2010-10-27_15:12:42", "2010-11-02_00:00:00"));
    assert(check_next("55 5 * * * *",           "2010-10-27_15:04:54", "2010-10-27_15:05:55"));
    assert(check_next("55 5 * * * *",           "2010-10-27_15:05:55", "2010-10-27_16:05:55"));
    assert(check_next("20,40 5 * * * *",        "2010-10-27_15:06:30", "2010-10-27_16:05:20"));
    assert(check_next("20 6 * * * *",           "2010-10-27_15:06:30", "2010-10-27_16:06:20"));
    assert(check_next("20 5,7 16 * * *",        "2010-10-27_15:06:30", "2010-10-27_16:05:20"));
    assert(check_next("20,40 5 16 * * *",       "2010-10-27_15:06:30", "2010-10-27_16:05:20"));
    assert(check_next("20 5 15,17 28 * *",      "2010-10-27_15:06:30", "2010-10-28_15:05:20"));
    assert(check_next("20,40 5 15,17 28 * *",   "2010-10-27_15:06:30", "2010-10-28_15:05:20"));
    assert(check_next("55 * 10 * * *",          "2010-10-27_09:04:54", "2010-10-27_10:00:55"));
    assert(check_next("55 * 10 * * *",          "2010-10-27_10:00:55", "2010-10-27_10:01:55"));
    assert(check_next("* 5 10 * * *",           "2010-10-27_09:04:55", "2010-10-27_10:05:00"));
    assert(check_next("* 5 10 * * *",           "2010-10-27_10:05:00", "2010-10-27_10:05:01"));
    assert(check_next("55 * * 3 * *",           "2010-10-02_10:05:54", "2010-10-03_00:00:55"));
    assert(check_next("55 * * 3 * *",           "2010-10-03_00:00:55", "2010-10-03_00:01:55"));
    assert(check_next("* * * 3 11 *",           "2010-10-02_14:42:55", "2010-11-03_00:00:00"));
    assert(check_next("* * * 3 11 *",           "2010-11-03_00:00:00", "2010-11-03_00:00:01"));
    assert(check_next("0 0 0 29 2 *",           "2007-02-10_14:42:55", "2008-02-29_00:00:00"));
    assert(check_next("0 0 0 29 2 *",           "2008-02-29_00:00:00", "2012-02-29_00:00:00"));
    assert(check_next("0 0 7 ? * MON-FRI",      "2009-09-26_00:42:55", "2009-09-28_07:00:00"));
    assert(check_next("0 0 7 ? * MON-FRI",      "2009-09-28_07:00:00", "2009-09-29_07:00:00"));
    assert(check_next("0 30 23 30 1/3 ?",       "2010-12-30_00:00:00", "2011-01-30_23:30:00"));
    assert(check_next("0 30 23 30 1/3 ?",       "2011-01-30_23:30:00", "2011-04-30_23:30:00"));
    assert(check_next("0 30 23 30 1/3 ?",       "2011-04-30_23:30:00", "2011-07-30_23:30:00"));
    assert(check_next("0 0 1 28 * ?",           "2022-02-28_02:00:00", "2022-03-28_01:00:00"));
    assert(check_next("0 0 0 * 12 ?",           "2022-01-01_00:00:00", "2022-12-01_00:00:00"));
    // H Tests
    cron_init_custom_hash_fn(testing_hash_function);
    cron_init_hash(7);
    assert(check_next("H H H H H ?",            "2022-05-12_00:00:00", "2022-05-22_14:07:00")); // 0 7 14 22 5 (1)
    assert(check_next("H H H H H ?",            "2022-06-12_00:00:00", "2023-05-22_14:07:00"));
    assert(check_next("H H H ? H H",            "2022-05-12_00:00:00", "2022-05-16_14:07:00"));
    assert(check_next("H 0 1 * * ?",            "2022-05-12_00:00:00", "2022-05-12_01:00:00"));
    assert(check_next("H 0,12 1 * * ?",         "2022-05-12_01:01:00", "2022-05-12_01:12:00"));
    assert(check_next("H 0,H 1 * * ?",          "2022-05-12_01:01:00", "2022-05-12_01:07:00"));
    assert(check_next("H 0 1/4 * * ?",          "2022-05-12_01:01:00", "2022-05-12_05:00:00"));
    assert(check_next("H H 1 * * ?",            "2022-05-12_00:00:00", "2022-05-12_01:07:00"));
    // H,H is same as H
    assert(check_next("H H,H 1 * * ?",          "2022-05-12_00:00:00", "2022-05-12_01:07:00"));
    assert(check_next("0 H/5 1 * * ?",          "2022-05-12_00:00:00", "2022-05-12_01:02:00"));
    assert(check_next("0 0 1 1 H/MAY ?",        "2022-05-12_00:00:00", "2022-06-01_01:00:00"));
    assert(check_next("0 0 1 1 H/MAY ?",        "2022-06-12_00:00:00", "2022-11-01_01:00:00"));
    // Tests for H in custom range
    assert(check_next("0 H(0-5) 1 1 * ?",       "2022-06-12_00:00:00", "2022-07-01_01:01:00")); // 0 1 1 1 * *
    assert(check_next("0 H,H(0-5) 1 1 * ?",     "2022-06-12_00:00:00", "2022-07-01_01:01:00")); // 0 1,1 1 1 * *
    assert(check_next("0 H(0-5),H(2-9) 1 1 * ?","2022-06-12_02:00:00", "2022-07-01_01:01:00")); // 0 1,9 1 1 * *
    assert(check_next("0 H(0-5),H(2-9) 1 1 * ?","2022-07-01_01:01:01", "2022-07-01_01:09:00")); // 0 1,9 1 1 * *
    assert(check_next("0 H(0-5),H(2-7) 1 1 * ?","2022-06-12_02:00:00", "2022-07-01_01:01:00")); // 0 1,3 1 1 * *
    assert(check_next("0 H(0-5),H(2-7) 1 1 * ?","2022-07-01_01:01:01", "2022-07-01_01:03:00")); // 0 1,3 1 1 * *
    assert(check_next("0 0 0 H(1-5),H(1-2) * ?","2022-07-01_01:01:01", "2022-07-02_00:00:00")); // 0 0 0 2,3 * *
    assert(check_next("0 0 0 H(1-5),H(1-2) * ?","2022-07-02_01:01:01", "2022-08-02_00:00:00")); // 0 0 0 2,3 * *
    assert(check_next("0 0 1 H(1-9)W * ?",      "2022-06-12_00:00:00", "2022-07-04_01:00:00")); // Day is 4
    assert(check_next("0 0 1 H(1-9)W * ?",      "2022-06-01_00:00:00", "2022-06-03_01:00:00"));
    assert(check_next("0 0 1 ? * HL",           "2022-06-12_00:00:00", "2022-06-27_01:00:00"));
    assert(check_next("0 0 1 ? * H(1-6)L",      "2022-06-12_00:00:00", "2022-06-25_01:00:00"));
    cron_init_hash(42);
    assert(check_next("H H H H H ?",            "2022-05-12_00:00:00", "2023-01-19_12:42:00")); // 0 42 12(84) 19(126) 1(168) 1(210)
    assert(check_next("H H H ? H H",            "2022-05-12_00:00:00", "2023-01-02_12:42:00"));
    assert(check_next("H 0 1 * * ?",            "2022-05-12_00:00:00", "2022-05-12_01:00:00"));
    assert(check_next("0 H/10 1 * * ?",         "2022-05-12_00:00:00", "2022-05-12_01:02:00"));
    assert(check_next("0 0 1 1 H/MAY ?",        "2022-05-12_00:00:00", "2022-06-01_01:00:00"));
    cron_init_hash(12);
    assert(check_next("H H H H H ?",            "2022-05-12_00:00:00", "2023-01-10_00:12:00")); // 0 12 0 10 1 5
    assert(check_next("H H H ? H H",            "2022-05-12_00:00:00", "2023-01-06_00:12:00"));
    // Tests for a custom hash function
    cron_custom_hash_fn custom_fn = fake_custom_hash_function;
    cron_init_custom_hash_fn(custom_fn);
    assert(check_next("H H H H H ?",            "2022-05-12_00:00:00", "2023-01-22_12:24:12")); // 12 24 12 22 1 3
    assert(check_next("H H H ? H H",            "2022-05-12_00:00:00", "2023-01-04_12:24:12")); // 12 24 12 22 1 3
    assert(check_next("0 0 1 ? * H/TUE",        "2022-05-12_00:00:00", "2022-05-13_01:00:00")); // 1/TUE
    cron_init_custom_hash_fn(testing_hash_function);
    // W Tests
    assert(check_next("0 0 1 4W * ?",           "2022-04-12_00:00:00", "2022-05-04_01:00:00"));
    assert(check_next("0 0 1 4W * ?",           "2022-05-12_00:00:00", "2022-06-03_01:00:00"));
    assert(check_next("0 0 1 1W * ?",           "2022-10-01_00:00:00", "2022-10-03_01:00:00"));
    assert(check_next("0 0 1 1W * ?",           "2022-10-03_00:00:00", "2022-10-03_01:00:00"));
    assert(check_next("0 0 1 16W * ?",          "2022-07-16_00:00:00", "2022-08-16_01:00:00"));
    assert(check_next("0 0 1 20W * ?",          "2022-08-20_00:00:00", "2022-09-20_01:00:00"));
    assert(check_next("0 0 1 1W * ?",           "2022-10-03_02:00:00", "2022-11-01_01:00:00"));
    assert(check_next("0 0 1 1W * ?",           "2022-05-01_02:00:00", "2022-05-02_01:00:00"));
    assert(check_next("0 0 1 1W * ?",           "2022-09-01_00:00:00", "2022-09-01_01:00:00"));
    assert(check_next("0 0 1 1,3W * ?",         "2022-09-01_00:00:00", "2022-09-01_01:00:00"));
    assert(check_next("0 0 1 1,3W * ?",         "2022-09-02_00:00:00", "2022-09-02_01:00:00"));
    assert(check_next("0 0 1 1,3W * ?",         "2022-09-03_00:00:00", "2022-10-01_01:00:00"));
    assert(check_next("0 0 1 1,3W * ?",         "2022-10-02_00:00:00", "2022-10-03_01:00:00"));
    // Check behaviour with more days mixed with W fields
    assert(check_next("0 0 1 1,3W,15 * ?",      "2022-09-01_00:00:00", "2022-09-01_01:00:00"));
    assert(check_next("0 0 1 1,3W,15 * ?",      "2022-09-02_00:00:00", "2022-09-02_01:00:00"));
    assert(check_next("0 0 1 1,3W,15 * ?",      "2022-09-03_00:00:00", "2022-09-15_01:00:00"));
    assert(check_next("0 0 1 1,3W,15 * ?",      "2022-09-16_00:00:00", "2022-10-01_01:00:00"));
    assert(check_next("0 0 1 1,3W,15 * ?",      "2022-10-02_00:00:00", "2022-10-03_01:00:00"));
    assert(check_next("0 0 1 1,3W,15,16W * ?",  "2022-09-01_00:00:00", "2022-09-01_01:00:00"));
    assert(check_next("0 0 1 1,3W,15,16W * ?",  "2022-09-02_00:00:00", "2022-09-02_01:00:00"));
    assert(check_next("0 0 1 1,3W,15,16W * ?",  "2022-09-03_00:00:00", "2022-09-15_01:00:00"));
    assert(check_next("0 0 1 1,3W,15,16W * ?",  "2022-09-16_00:00:00", "2022-09-16_01:00:00"));
    assert(check_next("0 0 1 1,3W,15,16W * ?",  "2022-09-17_00:00:00", "2022-10-01_01:00:00"));
    assert(check_next("0 0 1 1,3W,15,16W * ?",  "2022-10-02_00:00:00", "2022-10-03_01:00:00"));
    assert(check_next("0 0 1 1,3W,15,16W * ?",  "2025-02-16_00:00:00", "2025-02-17_01:00:00"));
    assert(check_next("0 0 1 1W,4W * ?",        "2022-09-01_00:00:00", "2022-09-01_01:00:00"));
    assert(check_next("0 0 1 1W,4W * ?",        "2022-09-02_00:00:00", "2022-09-05_01:00:00"));
    assert(check_next("0 0 1 1W,4W * ?",        "2022-06-03_00:00:00", "2022-06-03_01:00:00"));
    assert(check_next("0 0 1 1W,4W * ?",        "2022-09-03_00:00:00", "2022-09-05_01:00:00"));
    assert(check_next("0 0 1 1W,4W * ?",        "2022-10-01_00:00:00", "2022-10-03_01:00:00"));
    assert(check_next("0 0 1 1W,15W * ?",       "2022-09-01_00:00:00", "2022-09-01_01:00:00"));
    assert(check_next("0 0 1 1W,15W * ?",       "2022-10-01_00:00:00", "2022-10-03_01:00:00"));
    assert(check_next("0 0 1 1W,15W * ?",       "2022-09-02_00:00:00", "2022-09-15_01:00:00"));
    assert(check_next("0 0 1 1W,15W * ?",       "2022-01-01_00:00:00", "2022-01-03_01:00:00"));
    assert(check_next("0 0 1 1W,15W * ?",       "2022-01-04_00:00:00", "2022-01-14_01:00:00"));
    assert(check_next("0 0 1 1W,15W * ?",       "2022-01-15_00:00:00", "2022-02-01_01:00:00"));
    assert(check_next("0 0 1 8W,26W * ?",       "2022-01-06_00:00:00", "2022-01-07_01:00:00"));
    assert(check_next("0 0 1 8W,26W * ?",       "2022-01-26_00:00:00", "2022-01-26_01:00:00"));
    assert(check_next("0 0 1 8W,26W * ?",       "2022-02-26_00:00:00", "2022-03-08_01:00:00"));
    assert(check_next("0 0 1 8W,26W * ?",       "2022-03-09_00:00:00", "2022-03-25_01:00:00"));
    assert(check_next("0 0 1 29W * ?",          "2022-02-28_00:00:00", "2022-03-29_01:00:00"));
    assert(check_next("0 0 1 29W * ?",          "2022-02-28_00:00:00", "2022-03-29_01:00:00"));
    assert(check_next("0 0 1 1-3,29W * ?",      "2024-02-28_00:00:00", "2024-02-29_01:00:00"));
    assert(check_next("0 0 1 1-3,29W * ?",      "2024-03-01_00:00:00", "2024-03-01_01:00:00"));
    assert(check_next("0 0 1 1-3,29W * ?",      "2024-03-03_00:00:00", "2024-03-03_01:00:00"));
    assert(check_next("0 0 1 31W * ?",          "2022-02-28_00:00:00", "2022-03-31_01:00:00"));
    assert(check_next("0 0 1 31W * ?",          "2022-06-17_00:00:00", "2022-07-29_01:00:00"));
    assert(check_next("0 0 1 31W * ?",          "2022-07-30_00:00:00", "2022-08-31_01:00:00"));
    assert(check_next("0 0 1 26W * ?",          "2022-06-27_00:00:00", "2022-06-27_01:00:00"));
    assert(check_next("H 0 1 26W * ?",          "2022-06-27_00:00:00", "2022-06-27_01:00:00"));
    assert(check_next("H 0 1 26W * ?",          "2022-06-27_02:00:00", "2022-07-26_01:00:00"));
    assert(check_next("H 0 1 HW * ?",           "2022-06-27_02:00:00", "2022-07-11_01:00:00")); // 10W
    assert(check_next("H 0 1 HW * ?",           "2022-05-27_02:00:00", "2022-06-10_01:00:00")); // 10W
    // L Tests
    assert(check_next("0 0 1 LW * ?",           "2022-06-22_00:00:00", "2022-06-30_01:00:00"));
    assert(check_next("0 0 1 LW * ?",           "2022-07-01_00:00:00", "2022-07-29_01:00:00"));
    assert(check_next("0 0 1 LW * ?",           "2022-07-29_02:00:00", "2022-08-31_01:00:00"));
    assert(check_next("0 0 1 LW * ?",           "2022-10-01_00:00:00", "2022-10-31_01:00:00"));
    assert(check_next("0 0 1 LW * ?",           "2022-07-31_00:00:00", "2022-08-31_01:00:00"));
    assert(check_next("0 0 1 LW * ?",           "2022-07-30_00:00:00", "2022-08-31_01:00:00"));
    assert(check_next("0 0 1 LW,L-3 * ?",       "2022-07-30_00:00:00", "2022-08-28_01:00:00"));
    assert(check_next("0 0 1 LW,L-3 * ?",       "2022-08-29_00:00:00", "2022-08-31_01:00:00"));
    cron_init_hash(7);
    assert(check_next("H 0 H LW * ?",           "2022-10-01_00:00:00", "2022-10-31_14:00:00"));
    assert(check_next("0 0 1 L * ?",            "2022-05-12_00:00:00", "2022-05-31_01:00:00"));
    assert(check_next("0 0 1 L * ?",            "2022-02-12_00:00:00", "2022-02-28_01:00:00"));
    assert(check_next("0 0 1 L * ?",            "2020-02-12_00:00:00", "2020-02-29_01:00:00"));
    assert(check_next("0 0 1 L * ?",            "2021-02-12_00:00:00", "2021-02-28_01:00:00"));
    assert(check_next("0 0 1 ? * L",            "2022-05-12_00:00:00", "2022-05-15_01:00:00"));
    assert(check_next("0 0 1 ? * 4L",           "2022-05-12_00:00:00", "2022-05-26_01:00:00"));
    assert(check_next("0 0 1 ? * 1L",           "2022-03-29_00:00:00", "2022-04-25_01:00:00"));
    assert(check_next("0 0 1 ? * 5L",           "2022-06-25_00:00:00", "2022-07-29_01:00:00"));
    assert(check_next("0 0 1 L-2 * ?",          "2022-05-12_00:00:00", "2022-05-29_01:00:00"));
    assert(check_next("0 0 1 L-3 * ?",          "2020-02-12_00:00:00", "2020-02-26_01:00:00"));
    assert(check_next("0 0 1 L-30 * ?",         "2022-03-01_00:00:00", "2022-03-01_01:00:00"));
    assert(check_next("0 0 1 L-30 * ?",         "2022-01-02_00:00:00", "2022-02-01_01:00:00"));
    assert(check_next("0 0 1 L-31 * ?",         "2022-05-12_00:00:00", "2022-06-01_01:00:00"));
    assert(check_next("0 0 1 L-32 * ?",         "2022-05-12_00:00:00", "2022-06-01_01:00:00"));
    assert(check_next("0 0 1 L-31 2 ?",         "2022-01-01_00:00:00", "2022-02-01_01:00:00"));
    assert(check_next("0 0 1 1,L 2 ?",          "2022-01-01_00:00:00", "2022-02-01_01:00:00"));
    assert(check_next("0 0 1 1,L 2 ?",          "2022-02-02_00:00:00", "2022-02-28_01:00:00"));
    assert(check_next("0 0 1 1,L * ?",          "2022-02-28_02:00:00", "2022-03-01_01:00:00"));
    assert(check_next("0 0 1 1,L * ?",          "2022-03-02_00:00:00", "2022-03-31_01:00:00"));
    assert(check_next("0 0 1 1,L,5,L-5 2 ?",    "2022-01-01_00:00:00", "2022-02-01_01:00:00"));
    assert(check_next("0 0 1 1,L,5,L-5 2 ?",    "2022-02-02_00:00:00", "2022-02-05_01:00:00"));
    assert(check_next("0 0 1 1,L,5,L-5 2 ?",    "2022-02-06_00:00:00", "2022-02-23_01:00:00"));
    assert(check_next("0 0 1 1,L,5,L-5 2 ?",    "2022-02-24_00:00:00", "2022-02-28_01:00:00"));
    assert(check_next("0 0 1 1,L,5,L-5 * ?",    "2022-02-28_02:00:00", "2022-03-01_01:00:00"));
    assert(check_next("0 0 1 1,L,5,L-5 * ?",    "2022-03-02_00:00:00", "2022-03-05_01:00:00"));
    assert(check_next("0 0 1 1,L,5,L-5 * ?",    "2022-03-06_00:00:00", "2022-03-26_01:00:00"));
    assert(check_next("0 0 1 1,L,5,L-5 * ?",    "2022-03-27_00:00:00", "2022-03-31_01:00:00"));
    // Tests for unintended month rollovers when going from 31st, see https://github.com/staticlibs/ccronexpr/issues/35
    assert(check_next("0 0 0 ? 11-12 *",        "2022-05-31_00:00:00", "2022-11-01_00:00:00"));
    assert(check_next("0 0 0 ? 11-12 *",        "2022-07-31_00:00:00", "2022-11-01_00:00:00"));
    assert(check_next("0 0 0 ? 11-12 *",        "2022-08-31_00:00:00", "2022-11-01_00:00:00"));
    assert(check_next("0 0 0 ? 11-12 *",        "2022-10-31_00:00:00", "2022-11-01_00:00:00"));
    assert(check_next("0 0 0 ? 6-7 *",          "2022-05-31_00:00:00", "2022-06-01_00:00:00"));
    assert(check_next("0 0 0 ? 8-9 *",          "2022-07-31_00:00:00", "2022-08-01_00:00:00"));
    assert(check_next("0 0 0 ? 9-10 *",         "2022-08-31_00:00:00", "2022-09-01_00:00:00"));

    assert(check_next("0 0 0 ? 2-3 *",          "2022-01-31_00:00:00", "2022-02-01_00:00:00"));
    assert(check_next("0 0 0 ? 4-5 *",          "2022-03-31_00:00:00", "2022-04-01_00:00:00"));
    // Multiple consecutive days with 'W' flag
    assert(check_next("0 0 0 24W * *",          "2022-09-22_01:02:03", "2022-09-23_00:00:00"));
    assert(check_next("0 0 0 25W * *",          "2022-09-24_01:02:03", "2022-09-26_00:00:00"));
    assert(check_next("0 0 0 30W * *",          "2023-04-24_01:02:03", "2023-04-28_00:00:00"));
    assert(check_next("0 0 0 24W,25W * *",      "2022-09-22_01:02:03", "2022-09-23_00:00:00"));
    assert(check_next("0 0 0 24W,25W * *",      "2022-09-24_01:02:03", "2022-09-26_00:00:00"));
    assert(check_next("0 0 0 29W,30W * *",      "2022-10-24_01:02:03", "2022-10-28_00:00:00"));
    assert(check_next("0 0 0 29W,30W * *",      "2022-02-24_01:02:03", "2022-03-29_00:00:00"));
    assert(check_next("0 0 0 15,29W,30W * *",   "2022-02-24_01:02:03", "2022-03-15_00:00:00"));
    assert(check_next("0 0 0 29W,30W * *",      "2022-10-28_01:02:03", "2022-10-31_00:00:00"));
    assert(check_next("0 0 0 29W,30W * *",      "2022-10-29_01:02:03", "2022-10-31_00:00:00"));
    assert(check_next("0 0 0 29W,30W * *",      "2023-04-27_01:02:03", "2023-04-28_00:00:00"));
    assert(check_next("0 0 0 29W,30W * *",      "2023-04-29_01:02:03", "2023-05-29_00:00:00"));
    assert(check_next("0 0 0 1W,2W * *",        "2023-04-01_01:02:03", "2023-04-03_00:00:00"));
    assert(check_next("0 0 0 1W,2W * *",        "2023-04-02_01:02:03", "2023-04-03_00:00:00"));
    assert(check_next("0 0 0 1W,2W * *",        "2023-04-03_01:02:03", "2023-05-01_00:00:00"));
    assert(check_next("0 0 0 1W,15W,30W * *",   "2023-02-24_01:02:03", "2023-03-01_00:00:00"));
    assert(check_next("0 0 0 1W,15W,30W * *",   "2023-04-01_01:02:03", "2023-04-03_00:00:00"));
    assert(check_next("0 0 0 1W,15W,30W * *",   "2023-04-03_01:02:03", "2023-04-14_00:00:00"));
    assert(check_next("0 0 0 1W,15,30W * *",    "2023-04-03_01:02:03", "2023-04-15_00:00:00"));
    assert(check_next("0 0 0 1W,15W,30W * *",   "2023-04-14_01:02:03", "2023-04-28_00:00:00"));
    assert(check_next("0 0 0 1W,15,30W * *",    "2023-04-14_01:02:03", "2023-04-15_00:00:00"));
    assert(check_next("0 0 0 1W,15,30W * *",    "2023-04-15_01:02:03", "2023-04-28_00:00:00"));
    assert(check_next("0 0 0 1W,15W,30W * *",   "2023-04-28_01:02:03", "2023-05-01_00:00:00"));
    assert(check_next("0 0 0 1W,8W,15W,30W * *","2023-04-01_01:02:03", "2023-04-03_00:00:00"));
    assert(check_next("0 0 0 1W,8W,15W,30W * *","2023-04-03_01:02:03", "2023-04-07_00:00:00"));
    assert(check_next("0 0 0 1W,8W,15W,30W * *","2023-04-07_01:02:03", "2023-04-14_00:00:00"));
    assert(check_next("0 0 0 1W,8W,15W,30W * *","2023-04-14_01:02:03", "2023-04-28_00:00:00"));
    assert(check_next("0 0 0 1W,8W,15W,30W * *","2023-04-28_01:02:03", "2023-05-01_00:00:00"));
    assert(check_next("0 0 0 1W,15W,LW * *",    "2023-02-16_01:02:03", "2023-02-28_00:00:00"));
    assert(check_next("0 0 0 1W,15W,LW * *",    "2016-02-16_01:02:03", "2016-02-29_00:00:00"));
    assert(check_next("0 0 0 1W,15W,LW * *",    "2023-04-01_01:02:03", "2023-04-03_00:00:00"));
    assert(check_next("0 0 0 1W,15W,LW * *",    "2023-04-03_01:02:03", "2023-04-14_00:00:00"));
    assert(check_next("0 0 0 1W,15W,LW * *",    "2023-04-14_01:02:03", "2023-04-28_00:00:00"));
    assert(check_next("0 0 0 1W,15W,LW * *",    "2023-04-28_01:02:03", "2023-05-01_00:00:00"));
    assert(check_next("0 0 0 1W,15W,LW * *",    "2023-06-15_01:02:03", "2023-06-30_00:00:00"));
    assert(check_next("0 0 0 1W,15W,LW * *",    "2023-09-15_01:02:03", "2023-09-29_00:00:00"));
    assert(check_next("0 0 0 1,1W,15W * *",     "2021-12-31_01:02:03", "2022-01-01_00:00:00"));
    assert(check_next("0 0 0 1,1W,15W * *",     "2022-01-01_01:02:03", "2022-01-03_00:00:00"));
    assert(check_next("0 0 0 1,1W,15W * *",     "2022-01-03_01:02:03", "2022-01-14_00:00:00"));
    // Testcase for new years eve bug
    assert(check_next("0 0 12 1W,2W * ?","2025-01-01_11:00:00", "2025-01-01_12:00:00"));
    assert(check_next("0 0 12 1W,2W * ?","2025-01-01_13:01:00", "2025-01-02_12:00:00"));
    assert(check_next("0 0 12 1W,2W * ?","2025-01-02_12:01:00", "2025-02-03_12:00:00"));
    assert(check_next("0 0 12 1W,3W * ?","2025-01-01_11:00:00", "2025-01-01_12:00:00"));
    assert(check_next("0 0 12 1W,3W * ?","2025-01-01_13:01:00", "2025-01-03_12:00:00"));
    assert(check_next("0 0 12 1W,3W * ?","2025-01-03_12:01:00", "2025-02-03_12:00:00"));
    assert(check_next("0 0 12 1W,15W * ?","2025-01-01_11:00:00", "2025-01-01_12:00:00"));
    assert(check_next("0 0 12 1W,15W * ?","2025-01-01_13:01:00", "2025-01-15_12:00:00"));
    assert(check_next("0 0 12 1W,15W * ?","2025-01-15_12:01:00", "2025-02-03_12:00:00"));
    // Repeat for 1st May, a thursday
    assert(check_next("0 0 12 1W,2W * ?","2025-05-01_11:00:00", "2025-05-01_12:00:00"));
    assert(check_next("0 0 12 1W,2W * ?","2025-05-01_13:01:00", "2025-05-02_12:00:00"));
    assert(check_next("0 0 12 1W,2W * ?","2025-05-02_12:01:00", "2025-06-02_12:00:00"));
    assert(check_next("0 0 12 1W,3W * ?","2025-05-01_11:00:00", "2025-05-01_12:00:00"));
    assert(check_next("0 0 12 1W,3W * ?","2025-05-01_13:01:00", "2025-05-02_12:00:00"));
    assert(check_next("0 0 12 1W,3W * ?","2025-05-02_12:01:00", "2025-06-02_12:00:00"));
    assert(check_next("0 0 12 1W,15W * ?","2025-05-01_11:00:00", "2025-05-01_12:00:00"));
    assert(check_next("0 0 12 1W,15W * ?","2025-05-01_13:01:00", "2025-05-15_12:00:00"));
    assert(check_next("0 0 12 1W,15W * ?","2025-05-15_12:01:00", "2025-06-02_12:00:00"));
    // check 1st february 2025
    assert(check_next("0 0 12 1,15 * ?", "2025-01-31_12:00:00", "2025-02-01_12:00:00"));
    assert(check_next("0 0 12 1W,15W * ?", "2025-01-31_12:00:00", "2025-02-03_12:00:00"));
}

void test_parse() {

    assert(check_same("* * * 2 * *", "* * * 2 * ?"));
    assert(check_same("57,59 * * * * *", "57/2 * * * * *"));
    assert(check_same("1,3,5 * * * * *", "1-6/2 * * * * *"));
    assert(check_same("* * 4,8,12,16,20 * * *", "* * 4/4 * * *"));
    assert(check_same("* * * * * 0-6", "* * * * * TUE,WED,THU,FRI,SAT,SUN,MON"));
    assert(check_same("* * * * * 0", "* * * * * SUN"));
    assert(check_same("* * * * * 0", "* * * * * 7"));
    assert(check_same("* * * * 1-12 *", "* * * * FEB,JAN,MAR,APR,MAY,JUN,JUL,AUG,SEP,OCT,NOV,DEC *"));
    assert(check_same("* * * * 2 *", "* * * * Feb *"));
    assert(check_same("*  *  * *  1 *", "* * * * 1 *"));
    assert(check_same("* * * * 1 L", "* * * * 1 SUN"));
    // Cannot set specific days of month AND days of week
    assert(check_same("* * * * * *",
                      "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19-59,H 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18-59,H 0,1,2,3,4,5,6,7,8,9,10,11-23,H * jan,feb,mar,apr,may,jun,jul,aug,sep,oct,nov,dec,H mon,tue,wed,thu,fri,sat,sun,H"));
    assert(check_same("0 0 15 1,16,L * *", "0 0 15 1,L,16 * *"));
    assert(check_expr_valid("0 0 15 1,16,L * *"));
    assert(check_expr_valid("0 0 15 1,L,16 * *"));
    assert(check_expr_valid("0 0 12 LW,L * *"));
    assert(check_expr_valid("0 0 12 LW,L-3,L * *"));
    assert(check_expr_valid("0 0 12 L-3,LW,L * *"));
    // check default hash func has valid output
    cron_init_custom_hash_fn(NULL);
    assert(check_expr_valid("0 0 1 * * ?"));
    assert(check_expr_valid("H H H H H ?"));
    assert(check_expr_valid("H(0-59) H H H H ?"));
    assert(check_expr_valid("H H H ? H H"));
    assert(check_expr_valid("H H H,H ? H H"));
    assert(check_expr_valid("H H H/2 ? H H"));
    assert(check_expr_valid("H H H(0-12) ? H H"));
    assert(check_expr_valid("H H H H(1-17) H ?"));
    assert(check_expr_valid("H H H H(1-3),H(2-12) H *"));
    assert(check_expr_valid("H H H(1-3),H(0-12) H H *"));
    assert(check_expr_valid("H,H(0-59) H H H H *"));
    cron_init_custom_hash_fn(testing_hash_function);
    // Check H can be used in lists
    assert(check_expr_valid("H(30-32),H(0-59) H H,H(1-2),H(3-5) H H *"));
    assert(check_expr_valid("2,6,H,H,H H(0-9),H(7-15) H(1-3),H(1-3) H H *"));
    assert(check_expr_valid("2,6,H,H,H,7 H(0-9),H(7-15) H(1-3),H(1-3) 1,H(2-27),15 H *"));
    assert(check_expr_valid("2,6,H,H,H,7 H(0-9),H(7-15) H(1-3),H(1-3) 1,H(2-27),15W H *"));
    assert(check_expr_valid("2,6,H,H,H,7 H(0-9),H(7-15) H(1-3),H(1-3) 1,H(2-27),15W,LW H *"));
    assert(check_expr_valid("H,H H 12,H,7 ? H H,1,H(3-6)"));
    assert(check_expr_valid("H,H H 12,H,7 ? H H,1,H(3-6),THU"));

    assert(check_expr_invalid("77 * * * * *"));
    assert(check_expr_invalid("44-77 * * * * *"));
    assert(check_expr_invalid("* 77 * * * *"));
    assert(check_expr_invalid("* 44-77 * * * *"));
    assert(check_expr_invalid("* * 27 * * *"));
    assert(check_expr_invalid("* * 23-28 * * *"));
    assert(check_expr_invalid("* * * 45 * *"));
    assert(check_expr_invalid("* * * L-0 * *"));
    assert(check_expr_invalid("* * * 28-45 * *"));
    assert(check_expr_invalid("0 0 0 25 13 ?"));
    assert(check_expr_invalid("0 0 0 25 0 ?"));
    assert(check_expr_invalid("0 0 0 32 12 ?"));
    assert(check_expr_invalid("* * * * 11-13 *"));
    assert(check_expr_invalid("0 0 1 1-3W * ?"));
    assert(check_expr_invalid("0 0 1 1/3W * ?"));
    assert(check_expr_invalid("0 0 1 1W/3 * ?"));
    assert(check_expr_invalid("0 0 1 16WL * ?"));
    assert(check_expr_invalid("0 0 1 16LW * ?"));
    assert(check_expr_invalid("0 0 1 W3 * ?"));
    assert(check_expr_invalid("0 0 1 WL * ?"));
    assert(check_expr_invalid("0 0 1 10L * ?"));
    assert(check_expr_invalid("0 0 1 L * 3"));
    assert(check_expr_invalid("0 0 1 LW * 3"));
    assert(check_expr_invalid("0 0 1 9W * 3"));
    assert(check_expr_invalid("0 0 1 L-10 * 3"));
    assert(check_expr_invalid("0 0 1 L/7 * ?"));
    assert(check_expr_invalid("0 0 1 HLW * ?"));
    assert(check_expr_invalid("0 0 1 HL/H * ?"));
    assert(check_expr_invalid("0 0 1 HL/HW * ?"));
    assert(check_expr_valid("0 0 1 ? * 5L,SUN")); // Now allowed: Every sunday, and on the last friday
    assert(check_expr_invalid("0 0 1 ? * H/L"));
    assert(check_expr_invalid("0 0 1 ? * 19L"));
    assert(check_expr_invalid("0 0 1 17 * 5L"));
    assert(check_expr_valid("0 0 1 ? * L-7")); // Is now allowed, will be turned into the day with an offset
    assert(check_expr_invalid("0 0 1 ? * 5L-7"));
    assert(check_expr_invalid("0 0 1 5L-7 * ?"));
    assert(check_expr_invalid("0 0 1 5L * ?"));
    assert(check_expr_invalid("0 0 1 L12 * ?"));
    assert(check_expr_invalid("0 0 1 L12- * ?"));
    assert(check_expr_invalid("0 0 1 L1-4 * ?"));
    // H can not be used in ranges
    assert(check_expr_invalid("H H-H 1 * * ?")); // H-H Must be error, H can not be used in ranges
    assert(check_expr_invalid("H H-H 1 * * ?")); // H-60 Must be error, H can not be used in ranges
    assert(check_expr_invalid("H H-60 1 * * ?")); // H-60 Must be error, H can not be used in ranges
    assert(check_expr_invalid("H 1-H 1 * * ?")); // H can not be used in ranges
    assert(check_expr_invalid("1-H 0 1 * * ?")); // H can not be used in ranges
    assert(check_expr_invalid("1-H 0 1 * * ?")); // H can not be used in ranges
    assert(check_expr_invalid("0 0 1-H * * ?")); // H can not be used in ranges
    assert(check_expr_invalid("0 0 1 1-H * ?")); // H can not be used in ranges
    assert(check_expr_invalid("0 0 1 * 1-H ?")); // H can not be used in ranges
    assert(check_expr_invalid("0 0 1 ? * 1-H")); // H can not be used in ranges
    // Invalid iterator values
    assert(check_expr_invalid("0/60 * * * * *"));
    assert(check_expr_invalid("/12 * * * * *"));
    assert(check_expr_invalid("12/ * * * * *"));
    assert(check_expr_invalid("12- * * * * *"));
    assert(check_expr_invalid("* 0/60 * * * *"));
    assert(check_expr_invalid("* * 0/24 * * *"));
    assert(check_expr_invalid("* * * 1/32 * *"));
    assert(check_expr_invalid("* * * * 1/13 *"));
    assert(check_expr_invalid("* * * * * 1/8"));
    assert(check_expr_invalid("* * * * * 1/-1"));
    assert(check_expr_invalid("H H H */H H *"));
    assert(check_expr_invalid("H H H H(0-39) H *"));
    assert(check_expr_invalid("H(0-60) H H H H *"));
    assert(check_expr_invalid("H(0-30 H H H H *"));
    assert(check_expr_invalid("H(5-69) H H H H *"));
    assert(check_expr_invalid("H(11-6) H H H H *"));
    assert(check_expr_invalid("H H(17-93) H H H *"));
    assert(check_expr_invalid("H H H(0-25) H H *"));
    assert(check_expr_invalid("H H H H(0-12) H *"));
    assert(check_expr_invalid("H H H H H(0-2) *"));
    assert(check_expr_invalid("H H H * H H(0-9)"));
    assert(check_expr_invalid("H(5-o) H H H H *"));
    assert(check_expr_invalid("H(o-10) H H H H *"));
    assert(check_expr_invalid("H H H * H(0-8) *"));
    assert(check_expr_invalid("H H H * H(-1-8) *"));
    assert(check_expr_invalid("0 0\\  0 * * *")); // no "escaping"
    assert(check_expr_invalid("0 0 \\ 0 * * *")); // no "escaping"
    // Cannot set specific days of month AND days of week
    assert(check_expr_invalid("0 0 0 1 * 1"));
    assert(check_expr_invalid("0 0 0 H * SUN"));
    assert(check_expr_invalid("0 0 0 2 * H"));
    assert(check_expr_invalid("0 0 0 2W * H"));
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

/// Test if non-empty cron_expr will still result in the desired cron_expr
void test_invalid_bits(void) {
    cron_expr expr;
    const char *err = NULL;
    time_t res;
    struct tm *errortime = NULL;
    char buffer[21];

    // Test if non-zeroed cron_expr contains "old" bits after parsing
    cron_setBit(expr.seconds, 27);
    cron_parse_expr("0 * * * * *", &expr, &err);
    if (!cron_getBit(expr.seconds, 0) || err) {
        printf("Error: Non-zeroed cron_expr wasn't parsed properly\n");
        if (err) printf("%s\n", err);
        assert(0);
    } else if (cron_getBit(expr.seconds, 27)) {
        printf("Error: Non-zeroed cron_expr still has invalid bit\n");
        if (err) printf("%s\n", err);
        assert(0);
    }

    // Init start date for cron_next tests with invalid/unused bits
    struct tm *calinit = poors_mans_strptime("2012-07-01_09:53:50");
    const time_t dateinit = timegm(calinit);
    // Test empty cron
    memset(&expr, 0, sizeof expr);
    res = cron_next(&expr, dateinit);
    if (res != -1) {
        printf("Error: Empty cron found next trigger date successfully\n");
        errortime = gmtime(&res);
        strftime(buffer, 20, DATE_FORMAT, errortime);
        printf("%s\n", buffer);
        assert(0);
    }

    // Test only unused bits in expr
    memset(&expr, 0, sizeof expr);
    expr.seconds[7] = 0xF0;
    expr.minutes[7] = 0xF0;
    // all hour bits are used
    expr.days_of_month[0] = 0x01;
    expr.months[1] = 0x80;
    expr.days_of_week[0] = 0x01;

    res = cron_next(&expr, dateinit);
    if (res != -1) {
        printf("Error: Empty cron (only unused bits) found next trigger date successfully\n");
        errortime = gmtime(&res);
        strftime(buffer, 20, DATE_FORMAT, errortime);
        printf("%s\n", buffer);
        assert(0);
    }

    // Test only L flags in expr (invalid by default, L flag can only be used in one of them at the time)
    memset(&expr, 0, sizeof expr);
    expr.months[1] = 0x60;
    res = cron_next(&expr, dateinit);
    if (res != -1) {
        printf("Error: Empty cron (only L flags) found next trigger date successfully\n");
        errortime = gmtime(&res);
        strftime(buffer, 20, DATE_FORMAT, errortime);
        printf("%s\n", buffer);
        assert(0);
    }

    // Test only seconds, minutes and hours in expr
    memset(&expr, 0, sizeof expr);

    memset(expr.seconds, 0xFF, sizeof(uint8_t) * 8);
    memset(expr.minutes, 0xFF, sizeof(uint8_t) * 8);
    memset(expr.hours, 0xFF, sizeof(uint8_t) * 3);

    res = cron_next(&expr, dateinit);
    if (res != -1) {
        printf("Error: CRON with only seconds, minutes and hours found next trigger date successfully\n");
        errortime = gmtime(&res);
        strftime(buffer, 20, DATE_FORMAT, errortime);
        printf("%s\n", buffer);
        assert(0);
    }

    free(calinit);

}

/* For this test to work you need to set "-DCRON_TEST_MALLOC=1"*/
#ifdef CRON_TEST_MALLOC

void test_memory() {
    cron_expr cron;
    const char *err;

    cron_parse_expr("* * * * * *", &cron, &err);
    if (cronAllocations != 0) {
        printf("Allocations != 0 but %d\n", cronAllocations);
        assert(cronAllocations == 0);
    }
    printf("Allocations: total: %d, max: %d\n", cronTotalAllocations, maxAlloc);
}

#endif

int main() {

    test_bits();

    test_expr();
    test_parse();
    check_calc_invalid();
    test_invalid_bits();
#ifdef CRON_TEST_MALLOC
    test_memory(); /* For this test to work you need to set "-DCRON_TEST_MALLOC=1"*/
#endif
    printf("\nAll OK!\n");
    return 0;
}

