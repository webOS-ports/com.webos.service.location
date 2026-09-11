// Copyright (c) 2026 LuneOS project
//
// SPDX-License-Identifier: Apache-2.0
//
// Tests for the gpsConfig.conf parser: field-size enforcement (the
// GPS_MAX_PARAM_STRING+1 off-by-one fixed on herrie/fixes), whitespace
// trimming, numeric/hex parsing and hostile input lines.

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include <gps_cfg.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond) do { \
    tests_run++; \
    if (!(cond)) { \
        tests_failed++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

/* Mirror of the GPSServiceConfig layout that made the off-by-one observable:
 * a string field followed by a value that must not be clobbered. */
struct probe {
    char host[GPS_MAX_PARAM_STRING];
    uint8_t sentinel;
};

static const char *write_conf(const char *content) {
    static char path[512];
    const char *dir = getenv("TMPDIR");
    snprintf(path, sizeof(path), "%s/gps-cfg-test.conf", dir ? dir : "/tmp");
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
    return path;
}

static void test_string_value_is_bounded(void) {
    struct probe p;
    char longline[512];
    unsigned long dummy_ul = 0;

    memset(&p, 0, sizeof(p));
    p.sentinel = 0xa5;

    /* a value comfortably longer than the 80-byte field */
    memset(longline, 'A', sizeof(longline));
    memcpy(longline, "SUPL_HOST=", 10);
    longline[300] = '\n';
    longline[301] = '\0';

    gps_param_s_type table[] = {
        {"SUPL_HOST", &p.host, NULL, 's'},
        {"SUPL_VER", &dummy_ul, NULL, 'n'},
    };

    gps_read_conf(write_conf(longline), table, 2);

    /* the copy must terminate inside the field and never touch the sentinel */
    CHECK(p.sentinel == 0xa5);
    CHECK(strlen(p.host) < GPS_MAX_PARAM_STRING);
    CHECK(p.host[0] == 'A');
}

static void test_values_parse_and_trim(void) {
    struct probe p;
    unsigned long ver = 0;
    unsigned long port = 0;

    memset(&p, 0, sizeof(p));

    gps_param_s_type table[] = {
        {"SUPL_HOST", &p.host, NULL, 's'},
        {"SUPL_VER", &ver, NULL, 'n'},
        {"SUPL_PORT", &port, NULL, 'n'},
    };

    gps_read_conf(write_conf(
        "  SUPL_HOST =   supl.example.org  \n"
        "SUPL_VER=0x20000\n"
        "SUPL_PORT=7276\n"
        "line without equals sign\n"
        "=\n"
        "TRAILING_KEY=\n"), table, 3);

    CHECK(strcmp(p.host, "supl.example.org") == 0);
    CHECK(ver == 0x20000);
    CHECK(port == 7276);
}

static void test_null_literal_clears_string(void) {
    struct probe p;

    memset(&p, 0, sizeof(p));
    strcpy(p.host, "preset");

    gps_param_s_type table[] = {
        {"SUPL_HOST", &p.host, NULL, 's'},
    };

    gps_read_conf(write_conf("SUPL_HOST=NULL\n"), table, 1);

    CHECK(p.host[0] == '\0');
}

static void test_missing_file_is_noop(void) {
    unsigned long ver = 42;

    gps_param_s_type table[] = {
        {"SUPL_VER", &ver, NULL, 'n'},
    };

    gps_read_conf("/nonexistent/gps.conf", table, 1);
    CHECK(ver == 42);
}

int main(void) {
    test_string_value_is_bounded();
    test_values_parse_and_trim();
    test_null_literal_clears_string();
    test_missing_file_is_noop();

    printf("%s: %d checks, %d failed\n",
           tests_failed ? "FAIL" : "PASS", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
