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
 * File:   CronExprParser.cpp
 * Author: alex
 * 
 * Created on February 24, 2015, 9:35 AM
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <math.h>

#include "ccronexpr.h"

#define CRON_MAX_SECONDS 60
#define CRON_MAX_MINUTES 60
#define CRON_MAX_HOURS 24
#define CRON_MAX_DAYS_OF_WEEK 8
#define CRON_MAX_DAYS_OF_MONTH 32
#define CRON_MAX_MONTHS 12

#define CRON_CF_SECOND 0
#define CRON_CF_MINUTE 1
#define CRON_CF_HOUR_OF_DAY 2
#define CRON_CF_DAY_OF_WEEK 3
#define CRON_CF_DAY_OF_MONTH 4
#define CRON_CF_MONTH 5
#define CRON_CF_YEAR 6

#define CRON_CF_ARR_LEN 7

#define CRON_INVALID_INSTANT ((time_t) -1)

static const char* DAYS_ARR[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
#define CRON_DAYS_ARR_LEN 7
static const char* MONTHS_ARR[] = { "FOO", "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
#define CRON_MONTHS_ARR_LEN 13

#define CRON_MAX_STR_LEN_TO_SPLIT 256
#define CRON_MAX_NUM_TO_SRING 1000000000
/* computes number of digits in decimal number */
#define CRON_NUM_OF_DIGITS(num) (abs(num) < 10 ? 1 : \
                                (abs(num) < 100 ? 2 : \
                                (abs(num) < 1000 ? 3 : \
                                (abs(num) < 10000 ? 4 : \
                                (abs(num) < 100000 ? 5 : \
                                (abs(num) < 1000000 ? 6 : \
                                (abs(num) < 10000000 ? 7 : \
                                (abs(num) < 100000000 ? 8 : \
                                (abs(num) < 1000000000 ? 9 : 10)))))))))

#ifndef _WIN32
struct tm *gmtime_r(const time_t *timep, struct tm *result);
struct tm *localtime_r(const time_t *timep, struct tm *result);
#endif

/* Defining 'cron_mktime' to use use UTC (default) or local time */
#ifndef CRON_USE_LOCAL_TIME

/* http://stackoverflow.com/a/22557778 */
#ifdef _WIN32
time_t cron_mktime(struct tm* tm) {
    return _mkgmtime(tm);
}
#else /* _WIN32 */
#ifndef ANDROID
/* can be hidden in time.h */
time_t timegm(struct tm* __tp);
#endif /* ANDROID */
time_t cron_mktime(struct tm* tm) {
#ifndef ANDROID
    return timegm(tm);
#else /* ANDROID */
    /* https://github.com/adobe/chromium/blob/cfe5bf0b51b1f6b9fe239c2a3c2f2364da9967d7/base/os_compat_android.cc#L20 */
    static const time_t kTimeMax = ~(1L << (sizeof (time_t) * CHAR_BIT - 1));
    static const time_t kTimeMin = (1L << (sizeof (time_t) * CHAR_BIT - 1));
    time64_t result = timegm64(tm);
    if (result < kTimeMin || result > kTimeMax) return -1;
    return result;
#endif /* ANDROID */
}
#endif /* _WIN32 */


#ifndef CRON_TEST_MALLOC
#define cronFree(x) free(x);
#define cronMalloc(x) malloc(x);
#else
void* cronMalloc(size_t n);
void cronFree(void* p);
#endif

struct tm* cron_time(time_t* date, struct tm* out) {
#ifdef __MINGW32__
    (void)(out); /* To avoid unused warning */
    return gmtime(date);
#else /* __MINGW32__ */
#ifdef _WIN32
    return gmtime_s(date, out);
#else /* _WIN32 */
    return gmtime_r(date, out);
#endif /* _WIN32 */
#endif /* __MINGW32__ */
}

#else /* CRON_USE_LOCAL_TIME */

time_t cron_mktime(struct tm* tm) {
    return mktime(tm);
}

struct tm* cron_time(time_t* date, struct tm* out) {
#ifdef _WIN32
    errno_t err = localtime_s(out, date);
    return 0 == err ? out : NULL;
#else /* _WIN32 */    
    return localtime_r(date, out);
#endif /* _WIN32 */    
}

#endif /* CRON_USE_LOCAL_TIME */

static void free_splitted(char** splitted, size_t len) {
    size_t i;
    if (!splitted) return;
    for (i = 0; i < len; i++) {
        if (splitted[i]) {
            cronFree(splitted[i]);
        }
    }
    cronFree(splitted);
}

static char* strdupl(const char* str, size_t len) {
    if (!str) return NULL;
    char* res = (char*) cronMalloc(len + 1);
    if (!res) return NULL;
    memset(res, 0, len + 1);
    memcpy(res, str, len);
    return res;
}

/** Return next set bit position of bits starting at from_index as integer, set notfound to 1 if none was found.
 *  Interval: [from_index:max[
 */
static unsigned int next_set_bit(uint8_t* bits, unsigned int max, unsigned int from_index, int* notfound) {
    unsigned int i;
    if (!bits) {
        *notfound = 1;
        return 0;
    }
    for (i = from_index; i < max; i++) {
        if (cron_getBit(bits, i)) return i;
    }
    *notfound = 1;
    return 0;
}

/// Set last unset entry in reset array to *fi* (*arr* is usually initialized with -1 as every entry)
/// Example: push_to_fields_arr(array, CRON_CF_MINUTE)
///     - if used on a completely "new" array, would set seconds to 2 (CRON_CF_MINUTE) and return
///     - if used on an array with seconds set, would set minutes to 2 and return (usual)
///     - [...]
static void push_to_fields_arr(int* arr, int fi) {
    int i;
    if (!arr || -1 == fi) {
        return;
    }
    for (i = 0; i < CRON_CF_ARR_LEN; i++) {
        if (arr[i] == fi) return;
    }
    for (i = 0; i < CRON_CF_ARR_LEN; i++) {
        if (-1 == arr[i]) {
            arr[i] = fi;
            return;
        }
    }
}

static int add_to_field(struct tm* calendar, int field, int val) {
    if (!calendar || -1 == field) {
        return 1;
    }
    switch (field) {
    case CRON_CF_SECOND:
        calendar->tm_sec = calendar->tm_sec + val;
        break;
    case CRON_CF_MINUTE:
        calendar->tm_min = calendar->tm_min + val;
        break;
    case CRON_CF_HOUR_OF_DAY:
        calendar->tm_hour = calendar->tm_hour + val;
        break;
    case CRON_CF_DAY_OF_WEEK: /* mkgmtime ignores this field */
    case CRON_CF_DAY_OF_MONTH:
        calendar->tm_mday = calendar->tm_mday + val;
        break;
    case CRON_CF_MONTH:
        calendar->tm_mon = calendar->tm_mon + val;
        break;
    case CRON_CF_YEAR:
        calendar->tm_year = calendar->tm_year + val;
        break;
    default:
        return 1; /* unknown field */
    }
    time_t res = cron_mktime(calendar);
    if (CRON_INVALID_INSTANT == res) {
        return 1;
    }
    return 0;
}

/**
 * Reset the calendar setting all the fields provided to zero.
 */
static int reset(struct tm* calendar, int field) {
    if (!calendar || -1 == field) {
        return 1;
    }
    switch (field) {
    case CRON_CF_SECOND:
        calendar->tm_sec = 0;
        break;
    case CRON_CF_MINUTE:
        calendar->tm_min = 0;
        break;
    case CRON_CF_HOUR_OF_DAY:
        calendar->tm_hour = 0;
        break;
    case CRON_CF_DAY_OF_WEEK:
        calendar->tm_wday = 0;
        break;
    case CRON_CF_DAY_OF_MONTH:
        calendar->tm_mday = 1;
        break;
    case CRON_CF_MONTH:
        calendar->tm_mon = 0;
        break;
    case CRON_CF_YEAR:
        calendar->tm_year = 0;
        break;
    default:
        return 1; /* unknown field */
    }
    time_t res = cron_mktime(calendar);
    if (CRON_INVALID_INSTANT == res) {
        return 1;
    }
    return 0;
}

/**
 * Reset fields in calendar to 0/1 (if day of month), provided their field entry is not -1.
 *
 * @param calendar Pointer to a struct tm; it's fields will be reset to 0/1, if the corresponding field entry is not -1, but it's CRON_CF_value.
 * @param fields Array of CRON_CF_ARRAY_LEN; each entry is either -1 (no reset) or its position in the field, so it will reset its corresponding field in calendar.
 * @return 1 if a reset fails, 0 if successful.
 */
static int reset_all(struct tm* calendar, int* fields) {
    int i;
    int res = 0;
    if (!calendar || !fields) {
        return 1;
    }
    for (i = 0; i < CRON_CF_ARR_LEN; i++) {
        if (-1 != fields[i]) {
            res = reset(calendar, fields[i]);
            if (0 != res) return res;
        }
    }
    return 0;
}

static int set_field(struct tm* calendar, int field, int val) {
    if (!calendar || -1 == field) {
        return 1;
    }
    switch (field) {
    case CRON_CF_SECOND:
        calendar->tm_sec = val;
        break;
    case CRON_CF_MINUTE:
        calendar->tm_min = val;
        break;
    case CRON_CF_HOUR_OF_DAY:
        calendar->tm_hour = val;
        break;
    case CRON_CF_DAY_OF_WEEK:
        calendar->tm_wday = val;
        break;
    case CRON_CF_DAY_OF_MONTH:
        calendar->tm_mday = val;
        break;
    case CRON_CF_MONTH:
        calendar->tm_mon = val;
        break;
    case CRON_CF_YEAR:
        calendar->tm_year = val;
        break;
    default:
        return 1; /* unknown field */
    }
    time_t res = cron_mktime(calendar);
    if (CRON_INVALID_INSTANT == res) {
        return 1;
    }
    return 0;
}

/**
 * Search the bits provided for the next set bit after the value provided,
 * and reset the calendar.
 *
 * @param bits The starting address of the searched field in the parsed cron expression.
 * @param max Maximum value for field (for expression and calendar). (Bits in expression will only be checked up to this one)
 * @param value Original value of field in calendar.
 * @param calendar Pointer to calendar structure. Starting with the original date, its fields will be replaced with the next trigger time from seconds to year.
 * @param field Integer showing which field is currently searched for, used to set/reset that field in calendar. TODO: Replace macros with enum
 * @param nextField Integer showing which field is following the searched field, used to increment that field in calendar, if a roll-over happens. TODO: Replace macros with enum
 * @param lower_orders Array marking which fields in calendar should be reset by [reset_all](#reset_all). Usually all lower calendar fields which are equal to
 * @param res_out Pointer to error code output. (Will be checked by do_next().)
 * @return Either next trigger value for or 0 if field could not be set in calendar or lower calendar fields could not be reset. (If failing, *res_out will be set to 1 as well.)
 */
static unsigned int find_next(uint8_t* bits, unsigned int max, unsigned int value, struct tm* calendar, unsigned int field, unsigned int nextField, int* lower_orders, int* res_out) {
    int notfound = 0;
    int err = 0;
    unsigned int next_value = next_set_bit(bits, max, value, &notfound);
    /* roll over if needed */
    if (notfound) {
        err = add_to_field(calendar, nextField, 1);
        if (err) goto return_error;
        err = reset(calendar, field);
        if (err) goto return_error;
        notfound = 0;
        next_value = next_set_bit(bits, max, 0, &notfound);
    }
    if (notfound || next_value != value) {
        err = set_field(calendar, field, next_value);
        if (err) goto return_error;
        err = reset_all(calendar, lower_orders);
        if (err) goto return_error;
    }
    return next_value;

    return_error:
    *res_out = 1;
    return 0;
}

/**
 * Set the next appropriate day of month on which the cron could trigger.
 * @param calendar Pointer to a struct tm, which holds the
 * @param days_of_month Pointer to parsed day of month field from the cron expression
 * @param day_of_month Day of month from which the do_next function started. (Should be equal to calendar->tm_mday on function call and exit.)
 * @param day_of_week Weekday from which the do_next function started. (Should be equal to calendar->tm_wday on function call and exit.)
 * @param lw_flags Set LW flags: Is L, W or are both present
 * @param resets Array which specifies which fields need to be reset, if the appropriate day of month is different from the start date.
 * @param res_out Integer pointer for passing out error values
 * @return 0 if an error happened (res_out is also set to 1), next day of month as an unsigned int when successful.
 */
static unsigned int handle_lw_flags(struct tm* calendar, uint8_t* days_of_month, unsigned int day_of_month, unsigned int day_of_week, uint8_t lw_flags, int* resets, int* res_out)
{
    int err;
    unsigned int count = 0;
    const unsigned int max = 366;

    unsigned int startday = calendar->tm_mday;
    unsigned int startmonth = calendar->tm_mon;

    switch (lw_flags) {
        case 3: {
            // LW in DOM
            // Special case: If already past the last weekday of the month, roll over into the next month
            // This is why finding the last weekday is in a loop which is broken only when the assumed trigger day is not behind the start one

            // Find last day in current month
            while (1)
            {
                unsigned int currentmonth = calendar->tm_mon;
                while (currentmonth == calendar->tm_mon) {
                    err = add_to_field(calendar, CRON_CF_DAY_OF_MONTH, 1);

                    if (err) {
                        *res_out = 1;
                        return 0;
                    }
                    day_of_month = calendar->tm_mday;
                }
                // Finally, go back to the end of starting month
                err = add_to_field(calendar, CRON_CF_DAY_OF_MONTH, -1);

                if (err) {
                    *res_out = 1;
                    return 0;
                }
                day_of_month = calendar->tm_mday;

                // Since the next weekday is found from the last day, it has to be behind the current day_of_month
                if (calendar->tm_wday == 6) {
                    err = add_to_field(calendar, CRON_CF_DAY_OF_MONTH, -1);

                    if (err) {
                        *res_out = 1;
                        return 0;
                    }
                    day_of_month = calendar->tm_mday;
                } else if (calendar->tm_wday == 0) {
                    err = add_to_field(calendar, CRON_CF_DAY_OF_MONTH, -2);

                    if (err) {
                        *res_out = 1;
                        return 0;
                    }
                    day_of_month = calendar->tm_mday;
                }
                // Verify assumed trigger day is not behind startday
                if ( (startmonth == calendar->tm_mon) && (startday > day_of_month) ) {
                    // Startmonth hasn't changed, but trigger day is before initial day
                    reset_all(calendar, resets);
                    while (calendar->tm_mon - startmonth == 0) {
                        // Roll over into next month
                        err = add_to_field(calendar, CRON_CF_DAY_OF_MONTH, 1);

                        if (err) {
                            *res_out = 1;
                            return 0;
                        }
                    }
                } else break;
            }
        }
        break;
        case 1: {
            // W in DOM

            // Check first which day is the "desired" day (xxW):
            // - Is current day a Monday?
            //   - If yes, is desired day only one day before current day, or is it the 1st of month and current day the 3rd?
            unsigned int desired_day = next_set_bit(days_of_month, CRON_MAX_DAYS_OF_MONTH, 0, res_out);
            if (*res_out == 1) return 0;
            if  ( calendar->tm_wday == 1 && \
                ( (calendar->tm_mday-desired_day == 1) ||  ( (desired_day==1) && (calendar->tm_mday==3) ) ) ) {
                // Great! The current day is the next trigger day and can be returned.
                ;
            }
            // else...
            // Step forward until "desired" day
            else {
                while ((!cron_getBit(days_of_month, day_of_month)) &&
                       count++ < max) {
                    err = add_to_field(calendar, CRON_CF_DAY_OF_MONTH, 1);

                    if (err) {
                        *res_out = 1;
                        return 0;
                    }
                    day_of_month = calendar->tm_mday;
                    day_of_week = calendar->tm_wday;
                }
                // Is it a weekday? If so, great! It can be returned directly, and the following condition will be irrelevant.

                // Otherwise...
                if (calendar->tm_wday == 6 || !calendar->tm_wday) { // SAT or SUN
                    int sign = ( calendar->tm_wday ? 1 : -1);
                    // If SAT: Try to go 1 day back, if accidentally in previous month, go 3 days forward (to MON)
                    // If SUN: Inverted (1 day forward, if in next month, 3 days back)
                    unsigned int oldmonth = calendar->tm_mon;
                    err = add_to_field(calendar, CRON_CF_DAY_OF_MONTH, -1*sign); // go to next friday
                    if (err) {
                        *res_out = 1;
                        return 0;
                    }

                    if (oldmonth != calendar->tm_mon) { // Jumped into the previous month by accident...
                        err = add_to_field(calendar, CRON_CF_DAY_OF_MONTH, 3*sign); // ...so we have to go 3 days forward to get to the closest monday.
                        if (err) {
                            *res_out = 1;
                            return 0;
                        }
                        if (oldmonth != calendar->tm_mon) {
                            *res_out = 1;
                            return 0;
                        } // just in case

                    }
                    day_of_month = calendar->tm_mday;
                }
            }
        }
        break;
        case 2: {
            // L in DOM

            while ( startmonth == calendar->tm_mon ) {
                err = add_to_field(calendar, CRON_CF_DAY_OF_MONTH, 1);

                if (err) {
                    *res_out = 1;
                    return 0;
                }
                day_of_month = calendar->tm_mday;
            }
            // Finally, go back to the end of starting month
            err = add_to_field(calendar, CRON_CF_DAY_OF_MONTH, -1);

            if (err) {
                *res_out = 1;
                return 0;
            }
            day_of_month = calendar->tm_mday;
        }
        break;
        // no default case, if different bits are set this shouldn't deal with it
    }
    // Finally, check if the planned date has moved in comparison to the start. If so, reset appropriate calendar fields for recalculation
    if ( (startday != day_of_month) || (startmonth != calendar->tm_mon) ) {
        reset_all(calendar, resets);
    }
    return day_of_month;
}

static unsigned int find_next_day(struct tm* calendar, uint8_t* days_of_month, unsigned int day_of_month, uint8_t* days_of_week, unsigned int day_of_week, uint8_t lw_flags, int* resets, int* res_out) {
    int err;
    unsigned int count = 0;
    const unsigned int max = 366;
    if (lw_flags) { // Adapt finding of next day: W should be nearest weekday to set dom, with L last weekday of month; in dow last x-day (Friday, Saturday, ...) of month
        day_of_month = handle_lw_flags(calendar, days_of_month, day_of_month, day_of_week, lw_flags, resets, res_out);
        if (*res_out) goto return_error;
    }
    else {
        while ((!cron_getBit(days_of_month, day_of_month) || !cron_getBit(days_of_week, day_of_week)) &&
               count++ < max) {
            err = add_to_field(calendar, CRON_CF_DAY_OF_MONTH, 1);

            if (err) goto return_error;
            day_of_month = calendar->tm_mday;
            day_of_week = calendar->tm_wday;
            reset_all(calendar, resets);
        }
    }
    return day_of_month;

    return_error:
    *res_out = 1;
    return 0;
}

/**
 * Find the next time at which the cron will be triggered.
 * If successful, it will replace the start time in *calendar*.
 *
 * Principle from [cron_next](#cron_next):
 * 1. Try to find matching seconds after start, if not found (in find_next()), raise minutes by one, reset seconds to 0 and start again.
 * 2. Once matching seconds are found,
 *
 * @param expr The parsed cron expression.
 * @param calendar The time after which the next cron trigger should be found. If successful (see return), will be replaced with the next trigger time.
 * @param dot Year of the original time. If no trigger is found even 4 years in the future, an error code (-1) is returned.
 * @return Error code: 0 on success, other values (e. g. -1) mean failure.
 */
static int do_next(cron_expr* expr, struct tm* calendar, unsigned int dot) {
    int i;
    int res = 0;
    int* resets = NULL;
    int* empty_list = NULL;
    unsigned int second = 0;
    unsigned int update_second = 0;
    unsigned int minute = 0;
    unsigned int update_minute = 0;
    unsigned int hour = 0;
    unsigned int update_hour = 0;
    unsigned int day_of_week = 0;
    unsigned int day_of_month = 0;
    unsigned int update_day_of_month = 0;
    unsigned int month = 0;
    unsigned int update_month = 0;

    resets = (int*) cronMalloc(CRON_CF_ARR_LEN * sizeof(int));
    if (!resets) goto return_result;
    empty_list = (int*) cronMalloc(CRON_CF_ARR_LEN * sizeof(int));
    if (!empty_list) goto return_result;
    for (i = 0; i < CRON_CF_ARR_LEN; i++) {
        resets[i] = -1;
        empty_list[i] = -1;
    }

    second = calendar->tm_sec;
    minute = calendar->tm_min;
    update_second = find_next(expr->seconds, CRON_MAX_SECONDS, second, calendar, CRON_CF_SECOND, CRON_CF_MINUTE, empty_list, &res);
    if (0 != res) goto return_result;
    if (second == update_second) {
        push_to_fields_arr(resets, CRON_CF_SECOND);
    }

    minute = calendar->tm_min;
    update_minute = find_next(expr->minutes, CRON_MAX_MINUTES, minute, calendar, CRON_CF_MINUTE, CRON_CF_HOUR_OF_DAY, resets, &res);
    if (0 != res) goto return_result;
    if (minute == update_minute) { //
        push_to_fields_arr(resets, CRON_CF_MINUTE);
    } else {
        res = do_next(expr, calendar, dot);
        if (0 != res) goto return_result;
    }

    hour = calendar->tm_hour;
    update_hour = find_next(expr->hours, CRON_MAX_HOURS, hour, calendar, CRON_CF_HOUR_OF_DAY, CRON_CF_DAY_OF_WEEK, resets, &res);
    if (0 != res) goto return_result;
    if (hour == update_hour) {
        push_to_fields_arr(resets, CRON_CF_HOUR_OF_DAY);
    } else {
        res = do_next(expr, calendar, dot);
        if (0 != res) goto return_result;
    }

    day_of_week = calendar->tm_wday;
    day_of_month = calendar->tm_mday;
    month = calendar->tm_mon;
    // L & W parameters
    uint8_t lw_flags = 0; // Bit 0: W (day of month), Bit 1: L (day of month), Bit 2: L (day of week)
    if (cron_getBit(expr->months, 15)) { // W DOM bit
        lw_flags |= (1 << 0);
    }
    if (cron_getBit(expr->months, 14)) { // L DOM bit
        lw_flags |= (1 << 1);
    }

    update_day_of_month = find_next_day(calendar, expr->days_of_month, day_of_month, expr->days_of_week, day_of_week, lw_flags, resets, &res);
    if (0 != res) goto return_result;
    if (day_of_month == update_day_of_month && month == calendar->tm_mon) {
        push_to_fields_arr(resets, CRON_CF_DAY_OF_MONTH);
    } else {
        res = do_next(expr, calendar, dot);
        if (0 != res) goto return_result;
    }

    month = calendar->tm_mon; /*day already adds one if no day in same month is found*/
    update_month = find_next(expr->months, CRON_MAX_MONTHS, month, calendar, CRON_CF_MONTH, CRON_CF_YEAR, resets, &res);
    if (0 != res) goto return_result;
    if (month != update_month) {
        if (calendar->tm_year - dot > 4) {
            res = -1;
            goto return_result;
        }
        res = do_next(expr, calendar, dot);
        if (0 != res) goto return_result;
    }
    goto return_result;

    return_result:
    if (!resets || !empty_list) {
        res = -1;
    }
    if (resets) {
        cronFree(resets);
    }
    if (empty_list) {
        cronFree(empty_list);
    }
    return res;
}

static int to_upper(char* str) {
    if (!str) return 1;
    int i;
    for (i = 0; '\0' != str[i]; i++) {
        str[i] = (char) toupper(str[i]);
    }
    return 0;
}

static char* to_string(int num) {
    if (abs(num) >= CRON_MAX_NUM_TO_SRING) return NULL;
    char* str = (char*) cronMalloc(CRON_NUM_OF_DIGITS(num) + 1);
    if (!str) return NULL;
    int res = sprintf(str, "%d", num);
    if (res < 0) return NULL;
    return str;
}

static char* str_replace(char *orig, const char *rep, const char *with) {
    char *result; /* the return string */
    char *ins; /* the next insert point */
    char *tmp; /* varies */
    size_t len_rep; /* length of rep */
    size_t len_with; /* length of with */
    size_t len_front; /* distance between rep and end of last rep */
    int count; /* number of replacements */
    if (!orig) return NULL;
    if (!rep) rep = "";
    if (!with) with = "";
    len_rep = strlen(rep);
    len_with = strlen(with);

    ins = orig;
    for (count = 0; NULL != (tmp = strstr(ins, rep)); ++count) {
        ins = tmp + len_rep;
    }

    /* first time through the loop, all the variable are set correctly
     from here on,
     tmp points to the end of the result string
     ins points to the next occurrence of rep in orig
     orig points to the remainder of orig after "end of rep"
     */
    tmp = result = (char*) cronMalloc(strlen(orig) + (len_with - len_rep) * count + 1);
    if (!result) return NULL;

    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; /* move to next "end of rep" */
    }
    strcpy(tmp, orig);
    return result;
}

static unsigned int parse_uint(const char* str, int* errcode) {
    char* endptr;
    errno = 0;
    long int l = strtol(str, &endptr, 0);
    if (errno == ERANGE || *endptr != '\0' || l < 0 || l > INT_MAX) {
        *errcode = 1;
        return 0;
    } else {
        *errcode = 0;
        return (unsigned int) l;
    }
}

static char** split_str(const char* str, char del, size_t* len_out) {
    size_t i;
    size_t stlen = 0;
    size_t len = 0;
    int accum = 0;
    char* buf = NULL;
    char** res = NULL;
    size_t bi = 0;
    size_t ri = 0;
    char* tmp;

    if (!str) goto return_error;
    for (i = 0; '\0' != str[i]; i++) {
        stlen += 1;
        if (stlen >= CRON_MAX_STR_LEN_TO_SPLIT) goto return_error;
    }

    for (i = 0; i < stlen; i++) {
        if (del == str[i]) {
            if (accum > 0) {
                len += 1;
                accum = 0;
            }
        } else if (!isspace(str[i])) {
            accum += 1;
        }
    }
    /* tail */
    if (accum > 0) {
        len += 1;
    }
    if (0 == len) return NULL;

    buf = (char*) cronMalloc(stlen + 1);
    if (!buf) goto return_error;
    memset(buf, 0, stlen + 1);
    res = (char**) cronMalloc(len * sizeof(char*));
    if (!res) goto return_error;

    for (i = 0; i < stlen; i++) {
        if (del == str[i]) {
            if (bi > 0) {
                tmp = strdupl(buf, bi);
                if (!tmp) goto return_error;
                res[ri++] = tmp;
                memset(buf, 0, stlen + 1);
                bi = 0;
            }
        } else if (!isspace(str[i])) {
            buf[bi++] = str[i];
        }
    }
    /* tail */
    if (bi > 0) {
        tmp = strdupl(buf, bi);
        if (!tmp) goto return_error;
        res[ri++] = tmp;
    }
    cronFree(buf);
    *len_out = len;
    return res;

    return_error:
    if (buf) {
        cronFree(buf);
    }
    free_splitted(res, len);
    *len_out = 0;
    return NULL;
}

static char* replace_ordinals(char* value, const char** arr, size_t arr_len) {
    size_t i;
    char* cur = value;
    char* res = NULL;
    int first = 1;
    for (i = 0; i < arr_len; i++) {
        char* strnum = to_string((int) i);
        if (!strnum) {
            if (!first) {
                cronFree(cur);
            }
            return NULL;
        }
        res = str_replace(cur, arr[i], strnum);
        cronFree(strnum);
        if (!first) {
            cronFree(cur);
        }
        if (!res) {
            return NULL;
        }
        cur = res;
        if (first) {
            first = 0;
        }
    }
    return res;
}

static int has_char(char* str, char ch) {
    size_t i;
    size_t len = 0;
    if (!str) return 0;
    len = strlen(str);
    for (i = 0; i < len; i++) {
        if (str[i] == ch) return 1;
    }
    return 0;
}

static int hash_seed = 0;
static custom_hash_fn fn = NULL;

void init_hash(int seed)
{
    hash_seed = seed;
}
void init_custom_hash_fn(custom_hash_fn func)
{
    fn = func;
}

/**
 * Replace H parameter with integer in proper range. If using an iterator fielo, min/max have to be set to proper values before!
 * @param field CRON field which needs a value for its 'H' (in string form)
 * @param n Position of the field in the CRON string, from 0 - 5
 * @param min Minimum value allowed in field/for replacement
 * @param max Maximum value allowed in field/for replacement
 * @param hashFn Custom random output function, if provided, will be used instead of rand(). Needs to return an integer and needs to accept the seed and index for deterministic behaviour for each field. Can be NULL.
 * @param error Error string in which error descriptions will be stored, if they happen. Just needs to be a const char** pointer. (See usage of get_range)
 * @return New char* with replaced H, to be used instead of field.
 */
static char* replace_hashed(char* field, unsigned int n, unsigned int min, unsigned int max, custom_hash_fn hashFn, const char** error)
{
    int i = 0;
    int value;
    char *newField = NULL;

    if (!has_char(field, 'H')) {
        *error = "No H to replace in field";
        return field;
    }
    // Multiple Hs per field are ok, but they will all be replaced with the same value
    // Not changing that, since it would either eat up further field values or require a seed per field (original seed + field offset?)?
    if (!hash_seed) {
        *error = "Hash Seed not initialized";
        return NULL;
    }

    // Generate random value
    if (hashFn) {
        value = hashFn(hash_seed, n);
    }
    else {
        int newSeed = rand();
        srand(hash_seed);
        while (n >= i++) {
            value = rand();
        }
        srand(newSeed);
    }
    // ensure value is below max...
    value %= max-min;
    // and above min
    value += min;

    // Create string
    char value_str[3];
    sprintf(value_str, "%d", value);
    newField = str_replace(field, "H", value_str);

    cronFree(field);
    return newField;

}

static unsigned int* get_range(char* field, unsigned int min, unsigned int max, const char** error) {

    char** parts = NULL;
    size_t len = 0;
    unsigned int* res = (unsigned int*) cronMalloc(2 * sizeof(unsigned int));
    if (!res) goto return_error;

    res[0] = 0;
    res[1] = 0;
    if (1 == strlen(field) && '*' == field[0]) {
        res[0] = min;
        res[1] = max - 1;
    } else if (!has_char(field, '-')) {
        int err = 0;
        unsigned int val = parse_uint(field, &err);
        if (err) {
            *error = "Unsigned integer parse error 1";
            goto return_error;
        }

        res[0] = val;
        res[1] = val;
    } else {
        parts = split_str(field, '-', &len);
        if (0 == len || len > 2) {
            *error = "Specified range has more than two fields";
            goto return_error;
        }
        int err = 0;
        res[0] = parse_uint(parts[0], &err);
        if (err) {
            *error = "Unsigned integer parse error 2";
            goto return_error;
        }
        res[1] = parse_uint(parts[1], &err);
        if (err) {
            *error = "Unsigned integer parse error 3";
            goto return_error;
        }
    }
    if (res[0] >= max || res[1] >= max) {
        *error = "Specified range exceeds maximum";
        goto return_error;
    }
    if (res[0] < min || res[1] < min) {
        *error = "Specified range is less than minimum";
        goto return_error;
    }

    free_splitted(parts, len);
    *error = NULL;
    return res;

    return_error:
    free_splitted(parts, len);
    if (res) {
        cronFree(res);
    }

    return NULL;
}

void cron_setBit(uint8_t* rbyte, int idx) {

    uint8_t j = idx / 8;
    uint8_t k = idx % 8;

    rbyte[j] |= (1 << k);

}

void cron_delBit(uint8_t* rbyte, int idx) {

    uint8_t j = idx / 8;
    uint8_t k = idx % 8;

    rbyte[j] &= ~(1 << k);

}

uint8_t cron_getBit(uint8_t* rbyte, int idx) {

    uint8_t j = idx / 8;
    uint8_t k = idx % 8;

    if (rbyte[j] & (1 << k)) {
        return 1;
    } else {
        return 0;
    }

}

void set_number_hits(const char* value, uint8_t* target, unsigned int min, unsigned int max, const char** error) {
    size_t i;
    unsigned int i1;
    size_t len = 0;

    char** fields = split_str(value, ',', &len);
    if (!fields) {
        *error = "Comma split error";
        goto return_result;
    }

    for (i = 0; i < len; i++) {
        if (!has_char(fields[i], '/')) {
            /* Not an incrementer so it must be a range (possibly empty) */

            unsigned int* range = get_range(fields[i], min, max, error);

            if (*error) {
                if (range) {
                    cronFree(range);
                }
                goto return_result;

            }

            for (i1 = range[0]; i1 <= range[1]; i1++) {
                cron_setBit(target, i1);

            }
            cronFree(range);

        } else {
            size_t len2 = 0;
            char** split = split_str(fields[i], '/', &len2);
            if (0 == len2 || len2 > 2) {
                *error = "Incrementer has more than two fields";
                free_splitted(split, len2);
                goto return_result;
            }
            unsigned int* range = get_range(split[0], min, max, error);
            if (*error) {
                if (range) {
                    cronFree(range);
                }
                free_splitted(split, len2);
                goto return_result;
            }
            if (!has_char(split[0], '-')) {
                range[1] = max - 1;
            }
            int err = 0;
            unsigned int delta = parse_uint(split[1], &err);
            if (err) {
                *error = "Unsigned integer parse error 4";
                cronFree(range);
                free_splitted(split, len2);
                goto return_result;
            }
            for (i1 = range[0]; i1 <= range[1]; i1 += delta) {
                cron_setBit(target, i1);
            }
            free_splitted(split, len2);
            cronFree(range);

        }
    }
    goto return_result;

    return_result:
    free_splitted(fields, len);

}

static void set_months(char* value, uint8_t* targ, const char** error) {
    int err;
    unsigned int i;
    unsigned int max = 12;

    char* replaced = NULL;

    err = to_upper(value);
    if (err) return;
    replaced = replace_ordinals(value, MONTHS_ARR, CRON_MONTHS_ARR_LEN);
    if (!replaced) return;

    set_number_hits(replaced, targ, 1, max + 1, error);
    cronFree(replaced);

    /* ... and then rotate it to the front of the months */
    for (i = 1; i <= max; i++) {
        if (cron_getBit(targ, i)) {
            cron_setBit(targ, i - 1);
            cron_delBit(targ, i);
        }
    }
}

static void set_days(char* field, uint8_t* targ, int max, const char** error) {
    /* WEEK: if field ends in 'L':
     * only 'L'? replace with Saturday/Sunday
     * day before 'L'? Last xday (mon,tue,...) of month
     *
     * if field is 'L-x': x days before last day of the week (better for month, maybe skip here?)
     * RANGES DISCOURAGED
     */
    /* MONTH: if field ends in 'L':
     * only 'L'? last day of the month
     *
     * if field is 'L-x': x days before last day of the month
     * RANGES DISCOURAGED
     *
     * if field ends in 'W':
     * 'W' after day? next weekday to that day in month
     * 'LW'? Last weekday of the month
     * NO RANGES ALLOWED
     */
    if (1 == strlen(field) && '?' == field[0]) {
        field[0] = '*';
    }
    set_number_hits(field, targ, 0, max, error);
}

static void set_days_of_month(char* field, uint8_t* targ, const char** error) {
    /* Days of month start with 1 (in Cron and Calendar) so add one */
    set_days(field, targ, CRON_MAX_DAYS_OF_MONTH, error);
    /* ... and remove it from the front */
    if (targ) {
        cron_delBit(targ, 0);
    }

}

void cron_parse_expr(const char* expression, cron_expr* target, const char** error) {
    const char* err_local;
    size_t len = 0;
    char** fields = NULL;
    char* days_replaced = NULL;
    char* w_check = NULL;
    char* s_check = NULL;
    if (!error) {
        error = &err_local;
    }
    *error = NULL;
    if (!expression) {
        *error = "Invalid NULL expression";
        goto return_res;
    }

    fields = split_str(expression, ' ', &len);
    if (len != 6) {
        *error = "Invalid number of fields, expression must consist of 6 fields";
        goto return_res;
    }

    // Find Hs, init seed and replace them
    for (size_t i = 0; i < len; i++)
    {
        int local_max = 0;
        char* has_h = strchr(fields[i], 'H');
        if (has_h) {
            if (i > 3) {
                char* oldfield = fields[i];
                fields[i] = replace_ordinals(fields[i], i == 4 ? MONTHS_ARR : DAYS_ARR, i == 4 ? CRON_MONTHS_ARR_LEN : CRON_DAYS_ARR_LEN);
                cronFree(oldfield);
                has_h = strchr(fields[i], 'H'); // Search for H again, since it might be gone if it was only an 'H' in 'THU'
                if (!has_h) {
                    continue; // no more 'H' in field? Skip replacement step then
                }
            }
            if ( *(has_h+1) == '/') { /* H before an iterator */
                sscanf(has_h, "H/%2d", &local_max); // get value of iterator, so it will be used as maximum instead of standard maximum for field
                if (!local_max) { /* iterator might have been specified as an ordinal instead... */
                    *error = "Hashed: Iterator error";
                    goto return_res;
                }
            }
            switch (i) {
                case 0: // seconds; technically same case as minutes
                    fields[i] = replace_hashed(fields[i], i, 0, local_max ? local_max : CRON_MAX_SECONDS, fn, error);
                    break;
                case 1: // minutes
                    fields[i] = replace_hashed(fields[i], i, 0, local_max ? local_max : CRON_MAX_MINUTES, fn, error);
                    break;
                case 2: // hours
                    fields[i] = replace_hashed(fields[i], i, 0, local_max ? local_max : CRON_MAX_HOURS, fn, error);
                    break;
                case 3: // day of month
                    fields[i] = replace_hashed(fields[i], i, 1, local_max ? local_max : 28, fn, error); // limited to 28th so the hashed cron will be executed every month
                    break;
                case 4: // month
                    fields[i] = replace_hashed(fields[i], i, 1, local_max ? local_max : CRON_MAX_MONTHS, fn, error);
                    break;
                case 5: // day of week
                    fields[i] = replace_hashed(fields[i], i, 1, local_max ? local_max : CRON_MAX_DAYS_OF_WEEK, fn, error);
                    break;
                default:
                    *error = "Too many fields! (Should have failed before loop already)";
                    goto return_res;
            }
        }
        if (*error) goto return_res;
    }

    set_number_hits(fields[0], target->seconds, 0, 60, error);
    if (*error) goto return_res;
    set_number_hits(fields[1], target->minutes, 0, 60, error);
    if (*error) goto return_res;
    set_number_hits(fields[2], target->hours, 0, 24, error);
    if (*error) goto return_res;
    to_upper(fields[5]);
    days_replaced = replace_ordinals(fields[5], DAYS_ARR, CRON_DAYS_ARR_LEN);
    set_days(days_replaced, target->days_of_week, 8, error);
    cronFree(days_replaced);
    if (*error) goto return_res;
    if (cron_getBit(target->days_of_week, 7)) {
        /* Sunday can be represented as 0 or 7*/
        cron_setBit(target->days_of_week, 0);
        cron_delBit(target->days_of_week, 7);
    }
    // Days of month: Test for W, if there, set 16th bit in months
    if ( (w_check = strchr(fields[3], 'W')) ) {
        // Ensure only 1 day is specified, and W day is not the last in a range or list or iterator of days
        if ( has_char(fields[3], ',') || has_char(fields[3], '/') || has_char(fields[3], '-')) {
            *error = "W only allowed in combination with one day or after L in \"day of month\" field";
            goto return_res;
        }
        // Ensure nothing follows 'W'
        if (*(w_check+1) != '\0') {
            *error = "If W is used, \"day of month\" field needs to end with it";
            goto return_res;
        }
        cron_setBit(target->months, 15);
        *w_check = '\0'; // Since W is only allowed with a single day in day of month, get rid of the W (and a possible rest of the string)
                         // so only the day in front of W will be set
    }
    // Days of month: Test for L, if there, set 15th bit in months
    if ( (s_check = strchr(fields[3], 'L')) ) {
        // Ensure nothing is in field before L
        if (s_check != fields[3]) {
            *error = "L only allowed as first and only option (with an offset) in \"day of month\" field";
            goto return_res;
        }
        // Ensure only 1 day is specified, and W day is not the last in a range or list or iterator of days
        if ( has_char(fields[3], ',') || has_char(fields[3], '/') ) {
            *error = "L only allowed in combination with an offset or before W in \"day of month\" field";
            goto return_res;
        }
        cron_setBit(target->months, 14);
        *s_check = '\0';
        // avoid an empty dom field when string is 'LW'
        if (strlen(fields[3]) == 0) {
            *fields[3] = '*';
        }
    }
    set_days_of_month(fields[3], target->days_of_month, error);
    if (*error) goto return_res;
    set_months(fields[4], target->months, error);
    if (*error) goto return_res;

    goto return_res;

    return_res: 
    free_splitted(fields, len);
}

time_t cron_next(cron_expr* expr, time_t date) {
    /*
     The plan:

     1 Round up to the next whole second

     2 If seconds match move on, otherwise find the next match:
     2.1 If next match is in the next minute then roll forwards

     3 If minute matches move on, otherwise find the next match
     3.1 If next match is in the next hour then roll forwards
     3.2 Reset the seconds and go to 2

     4 If hour matches move on, otherwise find the next match
     4.1 If next match is in the next day then roll forwards,
     4.2 Reset the minutes and seconds and go to 2

     ...
     */
    if (!expr) return CRON_INVALID_INSTANT;
    struct tm calval;
    memset(&calval, 0, sizeof(struct tm));
    struct tm* calendar = cron_time(&date, &calval);
    if (!calendar) return CRON_INVALID_INSTANT;
    time_t original = cron_mktime(calendar);
    if (CRON_INVALID_INSTANT == original) return CRON_INVALID_INSTANT;

    int res = do_next(expr, calendar, calendar->tm_year);
    if (0 != res) return CRON_INVALID_INSTANT;

    time_t calculated = cron_mktime(calendar);
    if (CRON_INVALID_INSTANT == calculated) return CRON_INVALID_INSTANT;
    if (calculated == original) {
        /* We arrived at the original timestamp - round up to the next whole second and try again... */
        res = add_to_field(calendar, CRON_CF_SECOND, 1);
        if (0 != res) return CRON_INVALID_INSTANT;
        int res = do_next(expr, calendar, calendar->tm_year);
        if (0 != res) return CRON_INVALID_INSTANT;
    }

    return cron_mktime(calendar);
}

