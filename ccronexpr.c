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

#include "ccronexpr.h"

#define CRON_MAX_SECONDS 60
#define CRON_MAX_MINUTES 60
#define CRON_MAX_HOURS 24
#define CRON_MAX_DAYS_OF_WEEK 8
#define CRON_MAX_DAYS_OF_MONTH 32
#define CRON_MAX_MONTHS 13


// Bit 0...11 = Month
// Bit 13 ... 15 are used for W and L flags
#define CRON_L_DOW_BIT 13
#define CRON_L_DOM_BIT 14
#define CRON_W_DOM_BIT 15

#define W_DOM_FLAG (1<<0)
#define L_DOM_FLAG (1<<1)
#define L_DOW_FLAG (1<<2)

typedef enum {
    CRON_CF_SECOND = 0,
    CRON_CF_MINUTE,
    CRON_CF_HOUR_OF_DAY,
    CRON_CF_DAY_OF_WEEK,
    CRON_CF_DAY_OF_MONTH,
    CRON_CF_MONTH,
    CRON_CF_YEAR
} cron_cf;

typedef enum {
    CRON_FIELD_SECOND = 0,
    CRON_FIELD_MINUTE,
    CRON_FIELD_HOUR,
    CRON_FIELD_DAY_OF_MONTH,
    CRON_FIELD_MONTH,
    CRON_FIELD_DAY_OF_WEEK
} cron_fieldpos;

#define CRON_CF_ARR_LEN 7 /* or (CRON_CF_YEAR-CRON_CF_SECOND+1) */

#define CRON_INVALID_INSTANT ((time_t) -1)

static const char *DAYS_ARR[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
#define CRON_DAYS_ARR_LEN 7
static const char *MONTHS_ARR[] = {"FOO", "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV",
                                   "DEC"};
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

/* Defining 'cron_mktime' to use UTC (default) or local time */
#ifndef CRON_USE_LOCAL_TIME

/* http://stackoverflow.com/a/22557778 */
#ifdef _WIN32
time_t cron_mktime(struct tm* tm) {
    return _mkgmtime(tm);
}
#else /* _WIN32 */
#ifndef ANDROID

/* can be hidden in time.h */
time_t timegm(struct tm *__tp);

#endif /* ANDROID */

time_t cron_mktime(struct tm *tm) {
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
#define cronFree(x) free(x)
#define cronMalloc(x) malloc(x)
#else

void *cronMalloc(size_t n);

void cronFree(void *p);

#endif

struct tm *cron_time(time_t *date, struct tm *out) {
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

static void free_splitted(char **splitted, size_t len) {
    size_t i;
    if (!splitted) return;
    for (i = 0; i < len; i++) {
        if (splitted[i]) {
            cronFree(splitted[i]);
        }
    }
    cronFree(splitted);
}

static char *strdupl(const char *str, size_t len) {
    if (!str) return NULL;
    char *res = (char *) cronMalloc(len + 1);
    if (!res) return NULL;
    memset(res, 0, len + 1);
    memcpy(res, str, len);
    return res;
}

/** Return next set bit position of bits starting at from_index as integer, set notfound to 1 if none was found.
 *  Interval: [from_index:max[
 */
static unsigned int next_set_bit(const uint8_t *bits, unsigned int max, unsigned int from_index, int *notfound) {
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

/// Clear bit in reset byte at position *fi* (*arr* is usually initialized with -1)
/// Example: push_to_fields_arr(array, CRON_CF_MINUTE)
///     - if used on a completely "new" array, would clear bit 2 (CRON_CF_MINUTE) and return
static void push_to_fields_arr(uint8_t *arr, cron_cf fi) {
    if (!arr) {
        return;
    }
    if (fi >= CRON_CF_ARR_LEN) {
        return;
    }
    *arr &= ~(1 << fi); // Unset bit at position fi
}

static int add_to_field(struct tm *calendar, cron_cf field, int val) {
    if (!calendar) {
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
 * Reset the calendar field (at position field) to 0/1.
 */
static int reset(struct tm *calendar, cron_cf field) {
    if (!calendar) {
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
 * Reset reset_fields in calendar to 0/1 (if day of month), provided their field bit is not set.
 *
 * @param calendar Pointer to a struct tm; its reset_fields will be reset to 0/1, if the corresponding field bit is not set.
 * @param reset_fields BitArray/BitFlags of CRON_CF_ARR_LEN; each bit is either 1 (no reset) or 0, so it will reset its corresponding field in calendar.
 * @return 1 if a reset fails, 0 if successful.
 */
static int reset_all(struct tm *calendar, uint8_t *reset_fields) {
    int i;
    int res = 0;
    if (!calendar || !reset_fields) {
        return 1;
    }
    for (i = 0; i < CRON_CF_ARR_LEN; i++) {
        if (!(*reset_fields & (1 << i))) { // reset when value was already considered "right", so bit was cleared
            res = reset(calendar, i);
            if (0 != res) return res;
            *reset_fields |= 1 << i; // reset field bit here, so it will not be reset on next iteration if necessary
        }
    }
    return 0;
}

static int set_field(struct tm *calendar, cron_cf field, unsigned int val) {
    if (!calendar) {
        return 1;
    }
    switch (field) {
        case CRON_CF_SECOND:
            calendar->tm_sec = (int) val;
            break;
        case CRON_CF_MINUTE:
            calendar->tm_min = (int) val;
            break;
        case CRON_CF_HOUR_OF_DAY:
            calendar->tm_hour = (int) val;
            break;
        case CRON_CF_DAY_OF_WEEK:
            calendar->tm_wday = (int) val;
            break;
        case CRON_CF_DAY_OF_MONTH:
            calendar->tm_mday = (int) val;
            break;
        case CRON_CF_MONTH:
            calendar->tm_mon = (int) val;
            break;
        case CRON_CF_YEAR:
            calendar->tm_year = (int) val;
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
 * @param field Integer showing which field is currently searched for, used to set/reset that field in calendar.
 * @param nextField Integer showing which field is following the searched field, used to increment that field in calendar, if a roll-over happens.
 * @param reset_fields 8 bit field marking which fields in calendar should be reset by [reset_all](#reset_all). 1 bit per field.
 * @param res_out Pointer to error code output. (Will be checked by do_next().)
 * @return Either next trigger value for or 0 if field could not be set in calendar or lower calendar fields could not be reset. (If failing, *res_out will be set to 1 as well.)
 */
static unsigned int
find_next(const uint8_t *bits, unsigned int max, unsigned int value, struct tm *calendar, cron_cf field,
          cron_cf nextField, uint8_t *reset_fields, int *res_out) {
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
        // Still no set bit in range from 0 to max found? Range must be empty, return error
        if (notfound) goto return_error;
    }
    if (next_value != value) {
        err = reset_all(calendar, reset_fields);
        if (err) goto return_error;
        err = set_field(calendar, field, next_value);
        if (err) goto return_error;
    }
    return next_value;

    return_error:
    *res_out = 1;
    return 0;
}

/** Add all days with L flags to the current month bitarray,
 * to allow finding the next fitting day as usual.
 *
 * The calendar will be re-set to the day and month passed in.
 *
 * @param calendar struct tm with current month (and day of month);
 *                 will be used to determine the last dom, but reset to the original day.
 * @param cron_dom (copied) bits for the cron days of month
 * @param cron_dow bits for the cron 'L' days of week; only read for the L flag in DOW, to get which day is used.
 * @param lw_flags bitflags for set 'L' and 'W' flags types: Checked to see if 'L' flag in DOW is present.
 * @param l_offset bits for cron days of month 'L' flags: One bit for each offset (0-30), only read
 * @param res_out output for possible problems finding the 'L' days. Is '0' if no error occurred
 */
static void find_l_days(struct tm *calendar, uint8_t *cron_dom, const uint8_t *cron_dow, const uint8_t *l_offset,
                        const uint8_t lw_flags, int *res_out) {
    int startday = calendar->tm_mday;
    int startmonth = calendar->tm_mon;
    int startyear = calendar->tm_year;

    int no_lflag = 0;
    // Set calendar to last day of current month by going to "0th" of next month; get max offset from it
    calendar->tm_mday  = 0;
    calendar->tm_mon  += 1;
    time_t check = cron_mktime(calendar);
    if (CRON_INVALID_INSTANT == check) {
        // TODO: Return more detailed error code?
        goto return_err;
    }
    int lastday = calendar->tm_mday;
    // Can either use Last DOM or Last DOW, only one of the day fields can be set to specific values.
    // Case 1:
    // Find first offset for L DOM flags, from 0 to 30
    if (lw_flags & L_DOM_FLAG) {
        unsigned int offset = next_set_bit(l_offset, CRON_MAX_DAYS_OF_MONTH, 0, &no_lflag);
        while (no_lflag == 0) {
            if (offset >= lastday) {
                // Allow at least one execution this month: Set the 1st bit
                cron_setBit(cron_dom, 1);
            } else {
                // Set the corresponding bit in cron_dom, removing offset from lastday
                cron_setBit(cron_dom, lastday - offset);
            }
            offset = next_set_bit(l_offset, lastday, offset+1, &no_lflag);
        }
        goto return_success;
    }
    // Case 2:
    // Find day for L DOW flag, if set
    if (lw_flags & L_DOW_FLAG) {
        // Clear cur_dom, as otherwise all bits are already set
        memset(cron_dom, 0, 4);
        // Calendar is already set to the last day of the current month. Go back until the desired weekday is found
        int cur_wday = calendar->tm_wday;
        unsigned int offset = next_set_bit(cron_dow, CRON_MAX_DAYS_OF_WEEK, 0, &no_lflag);
        if (no_lflag) {
            // No DOW set?!
            goto return_err;
        }
        unsigned int diff = 0;
        if (offset <= cur_wday) {
            diff = cur_wday - offset;
        } else {
            // cur_wday < offset; MON, but offset is FRI
            // Add 7 days to signify cur_wday is in the week before the target day
            diff = (cur_wday + 7) - offset;
        }
        // Go back diff days in month; will always stay in month as no month is only 6 days long
        cron_setBit(cron_dom, lastday-diff);
    }
    // Successfully added cron bits; reset calendar and return
    return_success:
    calendar->tm_mday = startday;
    calendar->tm_mon = startmonth;
    calendar->tm_year = startyear;
    check = cron_mktime(calendar);
    if (CRON_INVALID_INSTANT == check) {
        goto return_err;
    }
    return;

    return_err:
    calendar->tm_mday = startday;
    calendar->tm_mon = startmonth;
    calendar->tm_year = startyear;
    cron_mktime(calendar);
    *res_out = 1;
}

/** Add all days with W flags to the current month bitarray,
 * to allow finding the next fitting day as usual.
 *
 * The calendar will be re-set to the day and month passed in.
 *
 * @param calendar struct tm with current month (and day of month);
 *                 will be used to determine the last dom, but reset to the original day.
 * @param cron_dom (copied) bits for the cron days of month, days will be set here
 * @param w_flags bits for the 'W' flags for days of month; only read for calculations
 * @param res_out output for possible problems finding the 'W' days. Is '0' if no error occurred
 */
static void find_w_days(struct tm *calendar, uint8_t *cron_dom, const uint8_t *w_flags, int *res_out) {
    int startday = calendar->tm_mday;
    int startmonth = calendar->tm_mon;
    int startyear = calendar->tm_year;

    // Get last day of month, boundary for searched W flags, may be needed for 'LW' flag
    calendar->tm_mday = 0;
    calendar->tm_mon += 1;
    time_t check = cron_mktime(calendar);
    if (CRON_INVALID_INSTANT == check) {
        goto return_err;
    }
    int lastday = calendar->tm_mday;
    // Reset to startday
    calendar->tm_mday = startday;
    calendar->tm_mon = startmonth;
    calendar->tm_year = startyear;
    check = cron_mktime(calendar);
    if (CRON_INVALID_INSTANT == check) {
        goto return_err;
    }

    int no_wflag = 0;
    unsigned int next_w = next_set_bit(w_flags, lastday+1, 0, &no_wflag);
    while (no_wflag == 0) {
        if (next_w == 0) {
            // Last weekday of the month (can only happen on first loop)
            calendar->tm_mday = lastday;
            check = cron_mktime(calendar);
            if (CRON_INVALID_INSTANT == check) {
                goto return_err;
            }
            switch (calendar->tm_wday) {
                case 0: // SUN, 2 days back
                    calendar->tm_mday -= 2;
                    break;
                case 6: // SAT, 1 day back
                    calendar->tm_mday -=1;
                    break;
                default: // already a valid weekday
                    break;
            }
            // Set the bit in cron_dom here, and search next W flag for a day
            cron_setBit(cron_dom, calendar->tm_mday);
            next_w = next_set_bit(w_flags, lastday+1, 1, &no_wflag);
            continue;
        }
        // Go to next W day
        calendar->tm_mday = (int)next_w; // next_w will never be bigger than 31, is safe
        check = cron_mktime(calendar);
        if (CRON_INVALID_INSTANT == check) {
            // TODO: Return more detailed error code?
            goto return_err;
        }
        // Check if it is a valid weekday
        if (calendar->tm_wday > 0 && calendar->tm_wday < 6) {
            goto finish_loop;
        }
        // if not, go to the closest weekday in current month
        // First of the month must only further in month (only +)
        if (calendar->tm_mday == 1) {
            if (calendar->tm_wday == 6) {
                // Saturday: 2 days further
                calendar->tm_mday += 2;
            } else {
                // Sunday: 1 day further
                calendar->tm_mday++;
            }
            goto finish_loop;
        }
        // Other days: Go one day back (SAT), or one day further (SUN), and check that month hasn't rolled over (then back to FRI)
        if (calendar->tm_wday == 6) {
            calendar->tm_mday--;
            goto finish_loop;
        }
        calendar->tm_mday++;
        check = cron_mktime(calendar);
        if (CRON_INVALID_INSTANT == check) {
            // TODO: Return more detailed error code?
            goto return_err;
        }
        if (calendar->tm_mon != startmonth) {
            // go 3 days back
            calendar->tm_mday -= 3;
            check = cron_mktime(calendar);
            if (CRON_INVALID_INSTANT == check) {
                // TODO: Return more detailed error code?
                goto return_err;
            }
        }

        finish_loop:
        cron_setBit(cron_dom, calendar->tm_mday);
        // Find next flag
        next_w = next_set_bit(w_flags, lastday+1, next_w+1, &no_wflag);
    }
    calendar->tm_mday = startday;
    calendar->tm_mon = startmonth;
    calendar->tm_year = startyear;
    check = cron_mktime(calendar);
    if (CRON_INVALID_INSTANT == check) {
        goto return_err;
    }
    return;

    return_err:
    calendar->tm_mday = startday;
    calendar->tm_mon = startmonth;
    calendar->tm_year = startyear;
    cron_mktime(calendar);
    *res_out = 1;
}

static unsigned int
find_next_day(struct tm *calendar, const uint8_t *cron_dom, unsigned int day_of_month, const uint8_t *cron_dow,
              unsigned int day_of_week, const uint8_t lw_flags, const uint8_t *l_dom_offset, const uint8_t *l_dow_flags,
              const uint8_t *w_flags, uint8_t *reset_fields, int *res_out) {
    int err;
    unsigned int count = 0;
    int max_days = 366; // without flags, advance 1 year may. days until months end with lw_flags; then the L- and W-Flags need to be recalculated
    // Copy cron_dom to add days determined by L- and W- flags
    uint8_t cur_doms[4];
    memcpy(cur_doms, cron_dom, 4);
    // Copy calendar to move day independently searching for L/W-Days; timezone MUST not be touched
    // find_{L,W}_days() will not alter the original tm struct
    struct tm searcher;
    if (lw_flags) {
        memcpy(&searcher, calendar, sizeof searcher);
        // Add L days and W days of current month to cur_doms
        find_l_days(&searcher, cur_doms, l_dow_flags, l_dom_offset, lw_flags, res_out);
        if (*res_out) {
            // something went wrong; keep res_out value, return 0
            return 0;
        }
        find_w_days(&searcher, cur_doms, w_flags, res_out);
        if (*res_out) {
            // something went wrong; keep res_out value, return 0
            return 0;
        }
        // Get last day of current month: 0th day of next month
        searcher.tm_mday = 0;
        searcher.tm_mon += 1;
        time_t check = cron_mktime(&searcher);
        if (CRON_INVALID_INSTANT == check) {
            *res_out = 1;
            return 0;
        }
        max_days = (searcher.tm_mday - calendar->tm_mday) + 1; // 1 day more, to allow rolling over into next month
    }
    // Find next fitting day in month, or reset lower fields and roll over into next month
    while ( (!cron_getBit(cur_doms, day_of_month) || !cron_getBit(cron_dow, day_of_week)) &&
           count++ < max_days) {
        err = add_to_field(calendar, CRON_CF_DAY_OF_MONTH, 1);

        if (err) goto return_error;
        day_of_month = calendar->tm_mday;
        day_of_week = calendar->tm_wday;
        reset_all(calendar, reset_fields);
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
 * 2. Once matching seconds are found, check if minute is valid for CRON. If so, continue to find matching hours, if not, raise hours by one, reset minutes (and lower fields) to 0, and re-start.
 * (Same for hours, day and month until a time fitting a CRON was found, or the next execution date is further than 4 years away.
 *
 * @param expr The parsed cron expression.
 * @param calendar The time after which the next cron trigger should be found. If successful (see return), will be replaced with the next trigger time.
 * @param dot Year of the original time. If no trigger is found even 4 years in the future, an error code (-1) is returned.
 * @return Error code: 0 on success, other values (e. g. -1) mean failure.
 */
static int do_next(const cron_expr *expr, struct tm *calendar, unsigned int dot) {
    int res = 0;
    uint8_t reset_fields = 0xFE; // First bit (seconds) should always be unset, because if minutes roll over (and seconds didn't), seconds need to be reset as well
    uint8_t second_reset_fields = 0xFF; // Only used for seconds; they shouldn't reset themselves after finding a match
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
    // L flags for DOM and DOW, or W flag for DOM
    uint8_t l_flags = 0; // Bit 0: W (day of month), Bit 1: L (day of month), Bit 2: L (day of week)
    if (cron_getBit(expr->months, CRON_L_DOM_BIT)) {
        l_flags |= L_DOM_FLAG;
    }
    if (cron_getBit(expr->months, CRON_L_DOW_BIT)) {
        l_flags |= L_DOW_FLAG;
    }
    if (cron_getBit(expr->months, CRON_W_DOM_BIT)) {
        l_flags |= W_DOM_FLAG;
    }

    while (reset_fields) {
        if (calendar->tm_year - dot > 5) {
            res = -1;
            goto return_result;
        }
        second = calendar->tm_sec;
        update_second = find_next(expr->seconds, CRON_MAX_SECONDS, second, calendar, CRON_CF_SECOND, CRON_CF_MINUTE,
                                  &second_reset_fields,
                                  &res); // Value should not be changed since all bits are already set, so discarding const explicitly
        if (0 != res) goto return_result;
        if (second == update_second) {
            push_to_fields_arr(&reset_fields, CRON_CF_SECOND);
        }

        minute = calendar->tm_min;
        update_minute = find_next(expr->minutes, CRON_MAX_MINUTES, minute, calendar, CRON_CF_MINUTE,
                                  CRON_CF_HOUR_OF_DAY, &reset_fields, &res);
        if (0 != res) goto return_result;
        if (minute == update_minute) { //
            push_to_fields_arr(&reset_fields, CRON_CF_MINUTE);
        } else {
            continue;
        }

        hour = calendar->tm_hour;
        update_hour = find_next(expr->hours, CRON_MAX_HOURS, hour, calendar, CRON_CF_HOUR_OF_DAY, CRON_CF_DAY_OF_WEEK,
                                &reset_fields, &res);
        if (0 != res) goto return_result;
        if (hour == update_hour) {
            push_to_fields_arr(&reset_fields, CRON_CF_HOUR_OF_DAY);
        } else {
            continue;
        }

        day_of_week = calendar->tm_wday;
        day_of_month = calendar->tm_mday;
        month = calendar->tm_mon;

        update_day_of_month = find_next_day(calendar, expr->days_of_month, day_of_month, expr->days_of_week,
                                            day_of_week, l_flags, expr->l_dom_offset, expr->l_dow_flags, expr->w_flags,
                                            &reset_fields,&res);
        if (0 != res) goto return_result;
        if (day_of_month == update_day_of_month && month == (unsigned int) calendar->tm_mon) {
            push_to_fields_arr(&reset_fields, CRON_CF_DAY_OF_MONTH);
        } else {
            continue;
        }

        month = calendar->tm_mon; /*day already adds one if no day in same month is found*/
        update_month = find_next(expr->months, CRON_MAX_MONTHS - 1, month, calendar, CRON_CF_MONTH, CRON_CF_YEAR,
                                 &reset_fields,
                                 &res); // max-1 because month bits are only set from 0 to 11
        if (0 != res) goto return_result;
        if (month != update_month) {
            continue;
        }
        goto return_result;
    }

    return_result:
    return res;
}

static int to_upper(char *str) {
    if (!str) return 1;
    int i;
    for (i = 0; '\0' != str[i]; i++) {
        str[i] = (char) toupper(str[i]);
    }
    return 0;
}

static char *to_string(int num) {
    if (abs(num) >= CRON_MAX_NUM_TO_SRING) return NULL;
    char *str = (char *) cronMalloc(CRON_NUM_OF_DIGITS(num) + 1);
    if (!str) return NULL;
    int res = sprintf(str, "%d", num);
    if (res < 0) return NULL;
    return str;
}

static char *str_replace(char *orig, const char *rep, const char *with) {
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
    tmp = result = (char *) cronMalloc(strlen(orig) + (len_with - len_rep) * count + 1);
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

static unsigned int parse_uint(const char *str, int *errcode) {
    char *endptr;
    errno = 0;
    long int l = strtol(str, &endptr, 10);
    if (errno == ERANGE || *endptr != '\0' || l < 0 || l > INT_MAX) {
        *errcode = 1;
        return 0;
    } else {
        *errcode = 0;
        return (unsigned int) l;
    }
}

static char **split_str(const char *str, char del, size_t *len_out) {
    size_t i;
    size_t stlen = 0;
    size_t len = 0;
    int accum = 0;
    char *buf = NULL;
    char **res = NULL;
    size_t bi = 0;
    size_t ri = 0;
    char *tmp;

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

    buf = (char *) cronMalloc(stlen + 1);
    if (!buf) goto return_error;
    memset(buf, 0, stlen + 1);
    res = (char **) cronMalloc(len * sizeof(char *));
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

static char *replace_ordinals(char *value, const char **arr, size_t arr_len) {
    size_t i;
    char *cur = value;
    char *res = NULL;
    int first = 1;
    for (i = 0; i < arr_len; i++) {
        char *strnum = to_string((int) i);
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

static int has_char(char *str, char ch) {
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
static cron_custom_hash_fn fn = NULL;

void cron_init_hash(int seed) {
    hash_seed = seed;
}

void cron_init_custom_hash_fn(cron_custom_hash_fn func) {
    fn = func;
}

/**
 * Replace H parameter with integer in proper range. If using an iterator field, min/max have to be set to proper values before!
 * The input field will always be freed, the returned char* should be used instead.
 *
 * @param field CRON field which needs a value for its 'H' (in string form)
 * @param n Position of the field in the CRON string, from 0 - 5
 * @param min Minimum value allowed in field/for replacement
 * @param max Maximum value allowed in field/for replacement
 * @param hashFn Custom random output function, if provided, will be used instead of rand(). Needs to return an integer and needs to accept the seed and index for deterministic behaviour for each field. Can be NULL.
 * @param error Error string in which error descriptions will be stored, if they happen. Just needs to be a const char** pointer. (See usage of get_range)
 * @return New char* with replaced H, to be used instead of field.
 */
static char *replace_hashed(char *field, unsigned int n, unsigned int min, unsigned int max, cron_custom_hash_fn hashFn,
                            const char **error) {
    unsigned int i = 0;
    unsigned int value;
    char *newField = NULL;
    // needed when a custom range is detected and removed
    char customRemover[8];
    char innerString[6];
    char *oldField = field;

    if (!has_char(field, 'H')) {
        *error = "No H to replace in field";
        return field;
    }

    // Generate random value
    if (hashFn) {
        value = hashFn(hash_seed, n);
    } else {
        int newSeed = rand();
        srand(hash_seed);
        while (n >= i++) {
            value = rand();
        }
        srand(newSeed);
    }
    // ensure value is below max...
    value %= max - min;
    // and above min
    value += min;

    // Check if a custom range is present, and get rid of it
    if (has_char(field, '(')) {
        sscanf(field, "H(%5[-0123456789])", innerString);
        sprintf(customRemover, "(%s)", innerString);
        field = str_replace(field, customRemover, NULL);
        cronFree(oldField);
    }

    // Create string
    char value_str[3];
    sprintf(value_str, "%u", value);
    newField = str_replace(field, "H", value_str);

    if (!newField) {
        *error = "Error allocating newField";
        return field;
    }

    cronFree(field);
    return newField;
}

static unsigned int *get_range(char *field, unsigned int min, unsigned int max, const char **error) {
    char **parts = NULL;
    size_t len = 0;
    unsigned int *res = (unsigned int *) cronMalloc(2 * sizeof(unsigned int));
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
        if (len != 2) {
            *error = "Specified range doesn't have two fields";
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

void cron_setBit(uint8_t *rbyte, unsigned int idx) {
    uint8_t j = idx / 8;
    uint8_t k = idx % 8;

    rbyte[j] |= (1 << k);
}

void cron_delBit(uint8_t *rbyte, unsigned int idx) {
    uint8_t j = idx / 8;
    uint8_t k = idx % 8;

    rbyte[j] &= ~(1 << k);
}

uint8_t cron_getBit(const uint8_t *rbyte, unsigned int idx) {
    uint8_t j = idx / 8;
    uint8_t k = idx % 8;

    if (rbyte[j] & (1 << k)) {
        return 1;
    } else {
        return 0;
    }
}

/**
 * Set bits in cron_expr field target, depending on value, and only in the range of [min:max[
 *
 * @param value String of field which needs to be parsed into set bits in cron_expr
 * @param target cron_expr field looking to be filled
 * @param min Min possible value for current field
 * @param max Max possible value for current field, not included in interval
 * @param error String output of error, if one occurred
 */
void set_number_hits(const char *value, uint8_t *target, unsigned int min, unsigned int max, const char **error) {
    size_t i;
    unsigned int i1;
    size_t len = 0;

    char **fields = split_str(value, ',', &len);
    if (!fields) {
        *error = "Comma split error";
        goto return_result;
    }

    for (i = 0; i < len; i++) {
        if (!has_char(fields[i], '/')) {
            /* Not an incrementer so it must be a range (possibly empty) */

            unsigned int *range = get_range(fields[i], min, max, error);

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
            char **split = split_str(fields[i], '/', &len2);
            if (len2 != 2) {
                *error = "Incrementer doesn't have two fields";
                free_splitted(split, len2);
                goto return_result;
            }
            unsigned int *range = get_range(split[0], min, max, error);
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
            if (delta >= max) {
                *error = "Incrementer too big";
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

static char *replace_h_entry(char *field, unsigned int pos, unsigned int min, const char **error) {
    char *has_h = strchr(field, 'H');
    if (has_h == NULL) {
        return field;
    }

    unsigned int fieldMax = 0, customMax = 0;
    // minBuf is 0xFF to see if it has been altered/read successfully, since 0 is a valid value for it
    unsigned int minBuf = 0xFF, maxBuf = 0;

    if (*(has_h + 1) == '/') { /* H before an iterator */
        sscanf(has_h, "H/%2u",
               &customMax); // get value of iterator, so it will be used as maximum instead of standard maximum for field
        if (!customMax) { /* iterator might have been specified as an ordinal instead... */
            *error = "Hashed: Iterator error";
            return field;
        }
    }
    if ((has_h != field) && (*(has_h - 1) == '/')) { /* H not allowed as iterator */
        *error = "Hashed: 'H' not allowed as iterator";
        return field;
    }
    if (*(has_h + 1) == '-' || \
        (has_h != field && *(has_h - 1) == '-')) { // 'H' not starting field, so may be the end of a range
        *error = "'H' is not allowed for use in ranges";
        return field;
    }
    // Test if custom Range is specified
    if (*(has_h + 1) == '(') {
        sscanf(has_h, "H(%2u-%2u)", &minBuf, &maxBuf);
        if (!maxBuf || \
                (minBuf == 0xFF) || \
                (minBuf > maxBuf) || \
                (minBuf < min) || \
                // if a customMax is present: Is read maximum bigger than it? (which it shouldn't be)
            (customMax ? maxBuf > customMax : 0)
                ) {
            *error = "'H' custom range error";
            return field;
        }
        min = minBuf;
        // maxBuf needs to be incremented by 1 to include it
        customMax = maxBuf + 1;
    }
    switch (pos) {
        case CRON_FIELD_SECOND:
            fieldMax = CRON_MAX_SECONDS;
            break;
        case CRON_FIELD_MINUTE:
            fieldMax = CRON_MAX_MINUTES;
            break;
        case CRON_FIELD_HOUR:
            fieldMax = CRON_MAX_HOURS;
            break;
        case CRON_FIELD_DAY_OF_MONTH:
            // limited to 28th so the hashed cron will be executed every month
            fieldMax = 28;
            break;
        case CRON_FIELD_MONTH:
            fieldMax = CRON_MAX_MONTHS;
            break;
        case CRON_FIELD_DAY_OF_WEEK:
            fieldMax = CRON_MAX_DAYS_OF_WEEK;
            break;
        default:
            *error = "Unknown field!";
            return field;
    }
    if (!customMax) {
        customMax = fieldMax;
    } else if (customMax > fieldMax) {
        *error = "'H' range maximum error";
        return field;
    }
    field = replace_hashed(field, pos, min, customMax, fn, error);

    return field;
}

static char *check_and_replace_h(char *field, unsigned int pos, unsigned int min, const char **error) {
    char *has_h = strchr(field, 'H');

    if (!has_h) {
        return field;
    }

    char *accum_field = NULL;
    char **subfields = NULL;
    size_t subfields_len = 0;
    // Check if Field contains ',', if so, split into multiple subfields, and replace in each (with same position no)
    char *has_comma = strchr(field, ',');
    if (has_comma) {
        // Iterate over split sub-fields, check for 'H' and replace if present
        subfields = split_str(field, ',', &subfields_len);
        if (subfields == NULL) {
            *error = "Failed to split 'H' string in list";
            goto return_error;
        }
        size_t res_len = 0;
        size_t res_lens[subfields_len];
        for (size_t i = 0; i < subfields_len; i++) {
            has_h = strchr(subfields[i], 'H');
            if (has_h) {
                subfields[i] = replace_h_entry(subfields[i], pos, min, error);
            }
            if (*error != NULL) {
                goto return_error;
            }
            res_lens[i] = strnlen(subfields[i], CRON_MAX_STR_LEN_TO_SPLIT);
            res_len += res_lens[i];
        }
        // Allocate space for the full string: Result lengths + (result count - 1) for the commas + 1 for '\0'
        accum_field = (char *) cronMalloc(res_len + subfields_len);
        if (accum_field == NULL) {
            *error = "Failed to merge 'H' in list";
            goto return_error;
        }
        memset(accum_field, 0, res_len + subfields_len);
        char *tracking = accum_field;
        for (size_t i = 0; i < subfields_len; i++) {
            // Sanity check: Is "tracking" still in the allocated memory boundaries?
            if ((tracking - accum_field) > (res_len + subfields_len)) {
                *error = "Failed to insert subfields to merged fields: String went oob";
                goto return_error;
            }
            strncpy(tracking, subfields[i], res_lens[i]);
            tracking += res_lens[i];
            // Don't append comma to last list entry
            if (i < subfields_len - 1) {
                strncpy(tracking, ",",
                        2); // using 2 to ensure the string ends in '\0', tracking will be set to that char
                tracking += 1;
            }
        }
        free_splitted(subfields, subfields_len);
        cronFree(field);
        return accum_field;
    }
    // only one H to find and replace, then return
    field = replace_h_entry(field, pos, min, error);
    return field;

    return_error:
    if (subfields) free_splitted(subfields, subfields_len);
    if (accum_field) cronFree(accum_field);
    return field;
}

static void set_months(char *value, uint8_t *targ, const char **error) {
    int err;
    unsigned int i;

    char *replaced = NULL;

    err = to_upper(value);
    if (err) return;
    replaced = replace_ordinals(value, MONTHS_ARR, CRON_MONTHS_ARR_LEN);
    if (!replaced) return;
    replaced = check_and_replace_h(replaced, 4, 1, error);
    if (*error) {
        cronFree(replaced);
        return;
    }

    set_number_hits(replaced, targ, 1, CRON_MAX_MONTHS, error);
    cronFree(replaced);

    /* ... and then rotate it to the front of the months */
    for (i = 1; i < CRON_MAX_MONTHS; i++) {
        if (cron_getBit(targ, i)) {
            cron_setBit(targ, i - 1);
            cron_delBit(targ, i);
        }
    }
}

static void set_days(char *field, uint8_t *targ, int max, const char **error) {
    if (1 == strlen(field) && '?' == field[0]) {
        field[0] = '*';
    }
    set_number_hits(field, targ, 0, max, error);
}

static void set_days_of_month(char *field, uint8_t *targ, const char **error) {
    /* Days of month start with 1 (in Cron and Calendar) so add one */
    set_days(field, targ, CRON_MAX_DAYS_OF_MONTH, error);
    /* ... and remove it from the front */
    if (targ) {
        cron_delBit(targ, 0);
    }

}

static char *replace_l_entry(char *field, unsigned int pos, cron_expr *target, const char **error) {
    char *has_l = strchr(field, 'L');
    if (!has_l) {
        return field;
    }
    int err;
    unsigned int offset;
    char day_char[2];

    switch (pos) {
        case CRON_FIELD_DAY_OF_MONTH: {
            // Possible usage: With offset, L-x days before last day of month
            // Days of Week and Days of Month cannot be set to specific values in the same cron anymore.
            // (Sub-)Field needs to start with L!
            if (has_l != field) {
                *error = "Element in Day of Month with 'L' doesn't begin with it";
                return field;
            }

            // Ensure W day is not the last in a range or iterator of days
            // Also, char following 'L' has to be either '-', 'W', ',' or '\0'; ',' shouldn't be possible as replace_l_entry is only fed split fields
            if ( has_char(has_l, '/') || \
                !((*(has_l + 1) == '-') || (*(has_l + 1) == 'W') || (*(has_l + 1) == ',') || (*(has_l + 1) == '\0'))) {
                *error = "L only allowed in combination before an offset or before W in 'day of month' field";
                return field;
            }

            cron_setBit(target->months, CRON_L_DOM_BIT);
            if (*(has_l + 1) == '-') {
                // offset is specified, L is starting dom
                offset = parse_uint(has_l + 2, &err);
                if (err) {
                    *error = "Error parsing L offset in 'day of month'";
                    return field;
                }
                if (offset == 0) {
                    *error = "Invalid offset: Needs to be > 0";
                    return field;
                } else if (offset > 30) {
                    // used to break, now it will simply set offset to 30
                    offset = 30;
                }
                cron_setBit(target->l_dom_offset, offset);
            } else {
                // No offset, set first bit in l_dom_offset
                cron_setBit(target->l_dom_offset, 0);
            }
            *has_l = '\0';
            // Should result in a 0-length string, which is ok. The offset is stored separately
            return field;
        }
        case CRON_FIELD_DAY_OF_WEEK: {
            if ( has_char(field, '/') ) {
                *error = "L can't be used with iterators in 'day of week' field";
                // Commas shouldn't be present, as sub-fields are input here, '-' for ranges is ok
                return field;
            }
            // 'L' with offset (or none)
            if (has_l == field) {
                if (strlen(field) == 1) {
                    *has_l = '0'; // Only L, so replace with sunday
                    return field;
                }
                if (*(has_l+1) == '-') {
                    // Convert offset to proper day
                    offset = parse_uint(has_l+2, &err);
                    if (err) {
                        *error = "Error parsing L offset in 'day of month'";
                        return field;
                    }
                    if (offset == 0) {
                        *error = "Invalid offset: Needs to be > 0";
                        return field;
                    } else if (offset > 6) {
                        // used to break, now it will simply set offset to 6
                        offset = 6;
                    }
                    // print offset instead of l; sprintf will append '\0' automatically
                    sprintf(has_l, "%1u", 7-offset);
                }
            } else {
                // Weekday L flag: Last x-day of month
                // Check if char after needs to end (sub-)field
                if (*(has_l+1) != '\0') {
                    *error = "'L' in weekday doesn't end field";
                    return field;
                }
                // check if char before 'L' is a decimal for a weekday
                if (strnlen(field, CRON_MAX_STR_LEN_TO_SPLIT) > 2) {
                    *error = "'L' in weekday is prefixed by non-weekday characters";
                    return field;
                }
                if (!sscanf(field, "%[01234567]L", &day_char[0])) {
                    *error = "'L' in weekday is preceded by non-weekday characters";
                    return field;
                }
                cron_setBit(target->months, CRON_L_DOW_BIT);
                day_char[1] = '\0';
                offset = parse_uint(day_char, &err);
                if (err) {
                    *error = "Error parsing 'L' Weekday in DOW";
                    return field;
                }
                if (offset == 7) {
                    // SUN is 0 bit, but can be '7' in field
                    offset = 0;
                }
                cron_setBit(target->l_dow_flags, offset);
                memset(field, 0, sizeof(char)*(has_l-field+1)); // Day is marked in l_dow_flags, so an empty string can be returned
            }
            return field;
        }
        default:
            *error = "Trying to find 'L' in unsupported field";
            return field;
    }
}

static char *l_check(char *field, unsigned int pos, cron_expr *target, const char **error) {
    char *has_l = strchr(field, 'L');

    if (!has_l) {
        return field;
    }

    char *has_comma = strchr(field, ',');
    char *new_field = field;
    char **subfields = NULL;
    size_t subfields_len = 0;
    if (has_comma) {
        // split list into subfields
        subfields = split_str(field, ',', &subfields_len);
        if (!subfields) {
            *error = "Failed to split 'L' in list";
            goto return_res;
        }
        size_t orig_len = strnlen(field, CRON_MAX_STR_LEN_TO_SPLIT);
        // allocate new field, with same length as current field; should be supported, as at least one letter is dropped
        new_field = (char *) cronMalloc(sizeof(char) * orig_len);
        if (new_field == NULL) {
            *error = "Failed to allocate string for 'L' replacement";
            goto return_res;
        }
        memset(new_field, 0, orig_len);
        char *tracking = new_field;
        // replace_l_entry for each, return field with replacements
        for (size_t i = 0; i < subfields_len; i++) {
            if ((tracking - new_field) > orig_len) {
                *error = "Failed to merge strings during 'L' replacement: String went oob";
                goto return_res;
            }
            subfields[i] = replace_l_entry(subfields[i], pos, target, error);
            if (*error != NULL) {
                goto return_res;
            }
            if (strnlen(subfields[i], CRON_MAX_STR_LEN_TO_SPLIT) > 0) {
                // No comma for first field separation, or an empty field
                if (i > 0) {
                    strncpy(tracking, ",", 1);
                    tracking += 1;
                }
                strncpy(tracking, subfields[i], strnlen(subfields[i], CRON_MAX_STR_LEN_TO_SPLIT));
                tracking += strnlen(subfields[i], CRON_MAX_STR_LEN_TO_SPLIT);
            }
        }
        goto return_res;
    }
    // Replace the L in the current field
    new_field = replace_l_entry(field, pos, target, error);
    return_res:
    if (subfields) free_splitted(subfields, subfields_len);
    if (new_field != field) cronFree(field); // field was replaced successfully
    return new_field;
}

static char *w_check(char *field, cron_expr *target, const char **error) {
    char *has_w = strchr(field, 'W');

    if (!has_w) {
        return field;
    }

    char *newField = NULL;
    char **splitField = NULL;
    size_t len_out = 0;

    unsigned int w_day = 0;
    int err;

    // Only available for dom, so no pos checking needed
    newField = (char *) cronMalloc(sizeof(char) * strlen(field));
    if (!newField) {
        *error = "w_check: newField malloc error";
        goto return_error;
    }
    memset(newField, 0, sizeof(char) * strlen(field));
    char *tracking = newField;
    // Ensure no specific days are set in day of week
    // Already checked in cron_parse_expr
    splitField = split_str(field, ',', &len_out);
    if (!splitField) {
        *error = "Error splitting 'day of month' field for W detection";
        goto return_error;
    }
    for (size_t i = 0; i < len_out; i++) {
        if ((has_w = strchr(splitField[i], 'W'))) {
            // Ensure W day is not the end or beginning of a range or iterator
            if (has_char(splitField[i], '/') || has_char(splitField[i], '-')) {
                *error = "W not allowed in iterators or ranges in 'day of month' field";
                goto return_error;
            }
            // Ensure nothing follows 'W'
            if (*(has_w + 1) != '\0') {
                *error = "If W is used, 'day of month' element needs to end with it";
                goto return_error;
            }
            if (!(strcmp(splitField[i], "LW"))) {
                cron_setBit(target->w_flags, 0);
            } else {
                *has_w = '\0';
                w_day = parse_uint(splitField[i], &err);
                if (err) {
                    *error = "Error reading uint in w-check";
                    goto return_error;
                }
                cron_setBit(target->w_flags, w_day);
            }
        } else {
            if (tracking != newField) {
                // A field was already added. Add a comma first
                strncpy(tracking, ",", 2); // ensure string ends in '\0', tracking will be set to it
                tracking += 1;
            }
            size_t field_len = strnlen(splitField[i], CRON_MAX_STR_LEN_TO_SPLIT);
            strncpy(tracking, splitField[i], field_len);
            tracking += field_len;
        }
    }
    free_splitted(splitField, len_out);
    cronFree(field);
    cron_setBit(target->months, CRON_W_DOM_BIT);
    return newField;

    return_error:
    if (splitField) free_splitted(splitField, len_out);
    if (newField) cronFree(newField);
    return field;
}

void cron_parse_expr(const char *expression, cron_expr *target, const char **error) {
    const char *err_local;
    size_t len = 0;
    char **fields = NULL;
    char *days_replaced = NULL;
    int notfound = 0;
    if (!error) {
        error = &err_local;
    }
    *error = NULL;
    if (!expression) {
        *error = "Invalid NULL expression";
        goto return_res;
    }

    if (!target) {
        *error = "Invalid target";
        goto return_res;
    }
    memset(target, 0, sizeof(*target));

    fields = split_str(expression, ' ', &len);
    if (len != 6) {
        *error = "Invalid number of fields, expression must consist of 6 fields";
        goto return_res;
    }

    fields[0] = check_and_replace_h(fields[0], 0, 0, error);
    if (*error) goto return_res;
    set_number_hits(fields[0], target->seconds, 0, CRON_MAX_SECONDS, error);
    if (*error) goto return_res;

    fields[1] = check_and_replace_h(fields[1], 1, 0, error);
    if (*error) goto return_res;
    set_number_hits(fields[1], target->minutes, 0, CRON_MAX_MINUTES, error);
    if (*error) goto return_res;

    fields[2] = check_and_replace_h(fields[2], 2, 0, error);
    if (*error) goto return_res;
    set_number_hits(fields[2], target->hours, 0, CRON_MAX_HOURS, error);
    if (*error) goto return_res;

    // Don't allow specific values for DOM and DOW at the same time
    if ( ((strcmp(fields[3], "*") != 0) && (strcmp(fields[3], "?") != 0)) &&
            ((strcmp(fields[5], "*") != 0) && (strcmp(fields[5], "?") != 0)) ) {
        *error = "Cannot set specific values for day of month AND day of week";
        goto return_res;
    }

    to_upper(fields[5]);
    days_replaced = replace_ordinals(fields[5], DAYS_ARR, CRON_DAYS_ARR_LEN);
    days_replaced = check_and_replace_h(days_replaced, 5, 1, error);
    if (*error) {
        cronFree(days_replaced);
        goto return_res;
    }
    days_replaced = l_check(days_replaced, 5, target, error);
    if (*error) {
        cronFree(days_replaced);
        goto return_res;
    }
    if (strnlen(days_replaced, CRON_MAX_STR_LEN_TO_SPLIT) == 0) {
        days_replaced[0] = '*'; // Ensure all weekdays are available if the field is empty (L flags)
    }
    set_days(days_replaced, target->days_of_week, CRON_MAX_DAYS_OF_WEEK, error);
    cronFree(days_replaced);
    if (*error) goto return_res;
    if (cron_getBit(target->days_of_week, 7)) {
        /* Sunday can be represented as 0 or 7*/
        cron_setBit(target->days_of_week, 0);
        cron_delBit(target->days_of_week, 7);
    }

    // Days of month: Ensure L-flag for dow is unset, unless the field is '*'
    if ((strcmp(fields[3], "*") != 0) && (strcmp(fields[3], "?") != 0)) {
        if (cron_getBit(target->months, CRON_L_DOW_BIT)) {
            *error = "Cannot specify specific days of month when using 'L' in days of week.";
            goto return_res;
        }
    }
    fields[3] = check_and_replace_h(fields[3], 3, 1, error);
    if (*error) goto return_res;
    // Days of month: Test for W, if there, set appropriate w_flags in target
    fields[3] = w_check(fields[3], target, error);
    if (*error) goto return_res;
    // Days of month: Test for L, if there, set 15th bit in months
    fields[3] = l_check(fields[3], 3, target, error);
    if (*error) goto return_res;
    // If w flags are set, days of month can be empty (e.g. "LW" or "9W" or "L")
    // So parsing has to happen if the field str len > 0, but can be skipped if a W flag or L (DOM) flag was found
    // Check W flags
    next_set_bit(target->w_flags, CRON_MAX_DAYS_OF_MONTH, 0, &notfound);
    if (notfound) {
        notfound = 0;
        // Check L (DOM) flags as well; if they don't exist as well, DOM needs to be checked
        next_set_bit(target->l_dom_offset, CRON_MAX_DAYS_OF_MONTH, 0, &notfound);
    }
    if (strlen(fields[3]) || notfound) set_days_of_month(fields[3], target->days_of_month, error);
    if (*error) goto return_res;

    set_months(fields[4], target->months, error); // check_and_replace_h incorporated into set_months
    if (*error) goto return_res;

    goto return_res;

    return_res:
    free_splitted(fields, len);
}

time_t cron_next(const cron_expr *expr, time_t date) {
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
    struct tm *calendar = cron_time(&date, &calval);
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
        res = do_next(expr, calendar, calendar->tm_year);
        if (0 != res) return CRON_INVALID_INSTANT;
    }

    return cron_mktime(calendar);
}
