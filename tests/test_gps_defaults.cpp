// Copyright (c) 2026 LuneOS project
//
// SPDX-License-Identifier: Apache-2.0
//
// Tests for GPSServiceConfig's compiled-in defaults: the XTRA assistance
// data must be fetched over HTTPS, so an on-path attacker cannot hand the
// GPS chip forged orbit data.

#include <stdio.h>
#include <string.h>

#include <GPSServiceConfig.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond) do { \
    tests_run++; \
    if (!(cond)) { \
        tests_failed++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static bool is_https(const char *url) {
    return strncmp(url, "https://", 8) == 0;
}

static void test_xtra_servers_are_https(void) {
    // A file that does not exist leaves the defaults in place.
    GPSServiceConfig conf("/nonexistent/gpsConfig.conf");

    CHECK(is_https(conf.mXtraServer1));
    CHECK(is_https(conf.mXtraServer2));
    CHECK(is_https(conf.mXtraServer3));
}

int main(void) {
    test_xtra_servers_are_https();

    printf("%s: %d checks, %d failed\n",
           tests_failed ? "FAIL" : "PASS", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
