cmake_minimum_required(VERSION 3.28)
project(ccronexpr C)

set(CMAKE_C_STANDARD 11)
add_compile_definitions(CRON_TEST_MALLOC=1)

include_directories(.)

add_executable(ccronexpr
        ccronexpr.c
        ccronexpr.h
        ccronexpr_test.c)
