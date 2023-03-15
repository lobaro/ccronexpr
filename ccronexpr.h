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
 * File:   ccronexpr.h
 * Author: alex
 *
 * Created on February 24, 2015, 9:35 AM
 */

#ifndef CCRONEXPR_H
#define	CCRONEXPR_H

#if defined(__cplusplus) && !defined(CRON_COMPILE_AS_CXX)
extern "C" {
#endif

#ifndef ANDROID
#include <time.h>
#else /* ANDROID */
#include <time64.h>
#endif /* ANDROID */

#include <stdint.h> /*added for use if uint*_t data types*/

#ifndef ARRAY_LEN
#define ARRAY_LEN(x) sizeof(x)/sizeof(x[0])
#endif

#ifdef __MINGW32__
/* To avoid warning when building with mingw */
time_t _mkgmtime(struct tm* tm);
#endif /* __MINGW32__ */

/**
 * Parsed cron expression
 */
typedef struct {
    uint8_t seconds[8];
    uint8_t minutes[8];
    uint8_t hours[3];
    uint8_t days_of_week[1];
    uint8_t days_of_month[4];
    uint8_t w_flags[4]; // Bits 0-30 for days 1-31, bit 31 for 'L'
    uint8_t months[2];
} cron_expr;

/**
 * Parses specified cron expression.
 * 
 * @param expression cron expression as nul-terminated string,
 *        should be no longer that 256 bytes
 * @param error output error message, will be set to string literal
 *        error message in case of error. Will be set to NULL on success.
 *        The error message should NOT be freed by client.
 * @return parsed cron expression in case of success. Returned expression
 *        must be freed by client using 'cron_expr_free' function.
 *        NULL is returned on error.
 */
void cron_parse_expr(const char* expression, cron_expr* target, const char** error);

/**
 * Uses the specified expression to calculate the next 'fire' date after
 * the specified date. All dates are processed as UTC (GMT) dates 
 * without timezones information. To use local dates (current system timezone) 
 * instead of GMT compile with '-DCRON_USE_LOCAL_TIME'
 * 
 * @param expr parsed cron expression to use in next date calculation
 * @param date start date to start calculation from
 * @return next 'fire' date in case of success, '((time_t) -1)' in case of error.
 */
time_t cron_next(const cron_expr* expr, time_t date);

/**
 * uint8_t* replace char* for storing hit dates, set_bit and get_bit are used as handlers
 */
uint8_t cron_getBit(const uint8_t* rbyte, unsigned int idx);
void cron_setBit(uint8_t* rbyte, unsigned int idx);
void cron_delBit(uint8_t* rbyte, unsigned int idx);

/**
 * Function for deterministic replacing of 'H' in expression (similar to Jenkins feature)
 * Seed: seed for random function
 * idx: Index in cron, same idx must return the same value
 *
 * returns a hash that is always the same for the same seed and idx
 */
typedef int (*cron_custom_hash_fn)(int seed, uint8_t idx);
/**
 * Set seed for 'H' replacement number generation, to keep it deterministic.
 * With default hash func, it will only be set when a number is generated and reset to a (previously generated) random number after;
 * if using a custom function, you need to take care of that
 * @param seed The seed to be used
 */
void cron_init_hash(int seed);
/**
 * Set a custom hash function to be used for 'H' replacement number generation
 * @param func A function which can generate pseudo-random numbers based on a seed.
 *             Needs to accept 2 parameters:
 *             - A seed (int)
 *             - An index (uint8_t), so the same position in a cron will always have the same number
 */
void cron_init_custom_hash_fn(cron_custom_hash_fn func);

/**
 * Frees the memory allocated by the specified cron expression
 * 
 * @param expr parsed cron expression to free
 */
void cron_expr_free(cron_expr* expr);

#if defined(__cplusplus) && !defined(CRON_COMPILE_AS_CXX)
} /* extern "C"*/
#endif

#endif	/* CCRONEXPR_H */

