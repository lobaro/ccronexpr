Cron expression parsing in ANSI C
=================================

Given a cron expression and a date, you can get the next date which satisfies the cron expression.

Supports cron expressions with `seconds` field. Based on implementation of [CronSequenceGenerator](https://github.com/spring-projects/spring-framework/blob/babbf6e8710ab937cd05ece20270f51490299270/spring-context/src/main/java/org/springframework/scheduling/support/CronSequenceGenerator.java) from Spring Framework.

Compiles and should work on Linux (GCC/Clang), Mac OS (Clang), Windows (MSVC), Android NDK, iOS, Raspberry Pi and possibly on other platforms with `time.h` support.

Supports compilation in C (89) and in C++ modes.

Usage example
-------------

    #include "ccronexpr.h"

    const char* err = NULL;
    cron_expr* expr = cron_parse_expr("0 */2 1-4 * * *", &err);
    if (err) ... /* invalid expression */
    time_t cur = time(NULL);
    time_t next = cron_next(expr, cur);
    ...
    cron_expr_free(expr);


Compilation and tests run examples
----------------------------------

    gcc ccronexpr.c ccronexpr_test.c -I. -Wall -Wextra -std=c89 -DCRON_TEST_MALLOC -o a.out && ./a.out
    g++ ccronexpr.c ccronexpr_test.c -I. -Wall -Wextra -std=c++11 -DCRON_TEST_MALLOC -o a.out && ./a.out
    g++ ccronexpr.c ccronexpr_test.c -I. -Wall -Wextra -std=c++11 -DCRON_TEST_MALLOC -DCRON_COMPILE_AS_CXX -o a.out && ./a.out

    clang ccronexpr.c ccronexpr_test.c -I. -Wall -Wextra -std=c89 -DCRON_TEST_MALLOC -o a.out && ./a.out
    clang++ ccronexpr.c ccronexpr_test.c -I. -Wall -Wextra -std=c++11 -DCRON_TEST_MALLOC -o a.out && ./a.out
    clang++ ccronexpr.c ccronexpr_test.c -I. -Wall -Wextra -std=c++11 -DCRON_TEST_MALLOC -DCRON_COMPILE_AS_CXX -o a.out && ./a.out

    cl ccronexpr.c ccronexpr_test.c /W4 /D_CRT_SECURE_NO_WARNINGS && ccronexpr.exe

Examples of supported expressions
---------------------------------

Expression, input date, next date:

    "*/15 * 1-4 * * *",  "2012-07-01_09:53:50", "2012-07-02_01:00:00"
    "0 */2 1-4 * * *",   "2012-07-01_09:00:00", "2012-07-02_01:00:00"
    "0 0 7 ? * MON-FRI", "2009-09-26_00:42:55", "2009-09-28_07:00:00"
    "0 30 23 30 1/3 ?",  "2011-04-30_23:30:00", "2011-07-30_23:30:00"

See more examples in tests.

Timezones
---------

This implementation does not support explicit timezones handling. By default all dates are
processed as UTC (GMT) dates without timezone infomation. 

To use local dates (current system timezone) instead of GMT compile with `-DCRON_USE_LOCAL_TIME`.

License information
-------------------

This project is released under the [Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0)

Changelog
---------
**2022-08-01**

 * `do_next` finding next trigger date via iteration instead of recursion

**2022-07-19**

 * Support for `H`, `L` and `W` in expressions
   * `H` (all fields): Is replaced with a pseudo-randomly generated number to allow triggering a task on a unique (per job, device...) time, but also consistent time or offset.
   * `L` (Day of month (DOM) or Day of week (DOW)):
     * By itself, means either the last DOM or DOW (=Sunday)
     * In DOM: Can be followed by an offset (e.g. `-2`) to set the cron for the day x days before the last DOM (here, 2 days before last DOM)
       * Can also be followed by `W` to set the cron for the last weekday (MON-FRI) of the month
     * In DOW: If preceded with a weekday (1-7, MON-SUN), sets the cron to the last set day of that month. E.g. `1L` sets the cron for the last monday of the month.
   * `W` (DOM):
     * In DOM: Has to be preceded by a day, to set the month for the closest weekday (MON-FRI) to that day. The trigger will not "jump" across months, 
       e.g. `1W` will trigger on the 2nd if the 1st is a Sunday, and on the 3rd if the 1st is a Saturday.

Explanation in more detail: [http://www.quartz-scheduler.org/documentation/quartz-2.3.0/tutorials/crontrigger.html](http://www.quartz-scheduler.org/documentation/quartz-2.3.0/tutorials/crontrigger.html)

**2016-06-17**

 * use thread-safe versions of `gmtime` and `localtime`

**2015-02-28**

 * initial public version
