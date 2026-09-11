// Copyright (c) 2026 LuneOS project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// SPDX-License-Identifier: Apache-2.0
//
// Host-runnable tests for the preference store (db_util) and the cached
// position layer built on it (Gps_stored_data).  These exercise exactly the
// failure modes fixed on herrie/fixes: missing/corrupt files, missing keys,
// repeated commits, delete-all-duplicates, and out-of-range coordinate
// formatting.

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include <db_util.h>
#include <Position.h>
#include <location_errors.h>
#include <Gps_stored_data.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond) do { \
    tests_run++; \
    if (!(cond)) { \
        tests_failed++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static const char *tmpfile_path(const char *name) {
    static char buf[512];
    const char *dir = getenv("TMPDIR");
    snprintf(buf, sizeof(buf), "%s/%s", dir ? dir : "/tmp", name);
    return buf;
}

static void test_create_put_get_roundtrip(void) {
    const char *path = tmpfile_path("dbutil-roundtrip.xml");
    DBHandle handle;
    xmlChar *result = NULL;

    memset(&handle, 0, sizeof(handle));
    remove(path);

    CHECK(createPreference(path, &handle, "Location", 0) == SUCCESS);
    CHECK(put(&handle, "latitude", "12.3456789") == SUCCESS);
    CHECK(put(&handle, "longitude", "-98.7654321") == SUCCESS);
    CHECK(commit(&handle) == SUCCESS);

    /* commit released the doc; the handle must be reusable, not dangling */
    CHECK(handle.doc == NULL);

    CHECK(get(&handle, "latitude", &result) == SUCCESS);
    CHECK(result != NULL);
    CHECK(strcmp((const char *) result, "12.3456789") == 0);
    xmlFree(result);

    result = NULL;
    CHECK(get(&handle, "longitude", &result) == SUCCESS);
    CHECK(result != NULL);
    xmlFree(result);

    remove(path);
}

static void test_get_missing_key_reports_not_found(void) {
    const char *path = tmpfile_path("dbutil-missing-key.xml");
    DBHandle handle;
    xmlChar *result = (xmlChar *) 0xdeadbeef;

    memset(&handle, 0, sizeof(handle));
    remove(path);

    CHECK(createPreference(path, &handle, "Location", 0) == SUCCESS);
    CHECK(put(&handle, "latitude", "1.0") == SUCCESS);
    CHECK(commit(&handle) == SUCCESS);

    /* the old code returned SUCCESS here with *result NULL -> atof(NULL) */
    CHECK(get(&handle, "no_such_key", &result) == KEY_NOT_FOUND);
    CHECK(result == NULL);

    remove(path);
}

static void test_get_on_absent_file_fails_cleanly(void) {
    DBHandle handle;
    xmlChar *result = NULL;

    memset(&handle, 0, sizeof(handle));
    handle.fileName = tmpfile_path("dbutil-never-created.xml");
    remove(handle.fileName);

    /* the old code dereferenced the NULL xmlParseFile result */
    CHECK(get(&handle, "latitude", &result) != SUCCESS);
    CHECK(result == NULL);
}

static void test_get_on_corrupt_file_fails_cleanly(void) {
    const char *path = tmpfile_path("dbutil-corrupt.xml");
    DBHandle handle;
    xmlChar *result = NULL;
    FILE *f = fopen(path, "w");

    CHECK(f != NULL);
    if (f) {
        fputs("<Location><latitude>1.0</lat", f);   /* truncated mid-tag */
        fclose(f);
    }

    memset(&handle, 0, sizeof(handle));
    handle.fileName = path;

    CHECK(get(&handle, "latitude", &result) != SUCCESS);
    CHECK(result == NULL);

    remove(path);
}

static void test_double_commit_is_safe(void) {
    const char *path = tmpfile_path("dbutil-double-commit.xml");
    DBHandle handle;

    memset(&handle, 0, sizeof(handle));
    remove(path);

    CHECK(createPreference(path, &handle, "Location", 0) == SUCCESS);
    CHECK(put(&handle, "k", "v") == SUCCESS);
    CHECK(commit(&handle) == SUCCESS);

    /* the old code use-after-freed handle->doc here */
    CHECK(commit(&handle) == INIT_ERROR);

    remove(path);
}

static void test_delete_key_removes_all_duplicates(void) {
    const char *path = tmpfile_path("dbutil-dup-keys.xml");
    DBHandle handle;
    xmlChar *result = NULL;

    memset(&handle, 0, sizeof(handle));
    remove(path);

    CHECK(createPreference(path, &handle, "Location", 0) == SUCCESS);
    CHECK(put(&handle, "k", "one") == SUCCESS);
    CHECK(put(&handle, "k", "two") == SUCCESS);
    CHECK(put(&handle, "other", "kept") == SUCCESS);
    CHECK(commit(&handle) == SUCCESS);

    /* the old loop read freed sibling pointers and stopped after the first */
    CHECK(deleteKey(&handle, "k") == SUCCESS);

    CHECK(get(&handle, "k", &result) == KEY_NOT_FOUND);
    result = NULL;
    CHECK(get(&handle, "other", &result) == SUCCESS);
    if (result) xmlFree(result);

    remove(path);
}

static void test_stored_position_roundtrip(void) {
    const char *path = tmpfile_path("stored-pos.xml");
    Position pos;
    Accuracy acc;

    remove(path);
    memset(&pos, 0, sizeof(pos));
    memset(&acc, 0, sizeof(acc));

    set_store_position(1757577600123LL, 52.3702157, 4.8951679, -2.5,
                       1.5, 270.0, 12.0, 8.0, path);

    CHECK(get_stored_position(&pos, &acc, path) == ERROR_NONE);
    CHECK(pos.timestamp == 1757577600123LL);
    CHECK(fabs(pos.latitude - 52.3702157) < 1e-6);
    CHECK(fabs(pos.longitude - 4.8951679) < 1e-6);
    CHECK(fabs(acc.horizAccuracy - 12.0) < 1e-6);

    remove(path);
}

static void test_stored_position_survives_huge_values(void) {
    const char *path = tmpfile_path("stored-pos-huge.xml");
    Position pos;
    Accuracy acc;

    remove(path);
    memset(&pos, 0, sizeof(pos));
    memset(&acc, 0, sizeof(acc));

    /*
     * 1e308 formatted with %.7f is ~316 characters - this is the exact input
     * that smashed the old 50-byte sprintf buffer.  The write may store a
     * truncated representation; the requirement is that it does not corrupt
     * memory and the read does not crash.
     */
    set_store_position(1LL, 1e308, -1e308, 1e308, 1e308, 1e308, 1e308, 1e308,
                       path);

    (void) get_stored_position(&pos, &acc, path);

    remove(path);
}

static void test_stored_position_rejects_partial_file(void) {
    const char *path = tmpfile_path("stored-pos-partial.xml");
    Position pos;
    Accuracy acc;
    FILE *f = fopen(path, "w");

    CHECK(f != NULL);
    if (f) {
        /* timestamp only - every other key missing */
        fputs("<?xml version=\"1.0\"?>\n<Location><timestamp>5</timestamp></Location>\n", f);
        fclose(f);
    }

    memset(&pos, 0, sizeof(pos));
    memset(&acc, 0, sizeof(acc));

    /* the old code returned each missing key as silent zeroes */
    CHECK(get_stored_position(&pos, &acc, path) == ERROR_NOT_AVAILABLE);

    remove(path);
}

static void test_null_arguments(void) {
    Position pos;
    Accuracy acc;
    DBHandle handle;

    memset(&handle, 0, sizeof(handle));

    CHECK(get_stored_position(NULL, &acc, "/nonexistent") == ERROR_NOT_AVAILABLE);
    CHECK(get_stored_position(&pos, NULL, "/nonexistent") == ERROR_NOT_AVAILABLE);
    CHECK(get_stored_position(&pos, &acc, NULL) == ERROR_NOT_AVAILABLE);
    CHECK(put(NULL, "k", "v") != SUCCESS);
    CHECK(put(&handle, NULL, "v") != SUCCESS);
    CHECK(get(NULL, "k", NULL) != SUCCESS);
    CHECK(deleteKey(NULL, "k") != SUCCESS);
    CHECK(commit(NULL) == INIT_ERROR);
    CHECK(createPreference(NULL, &handle, "t", 0) == NULL_VALUE);
    CHECK(deletePreference(NULL) == NULL_VALUE);
    CHECK(isFileExists(NULL) == 0);

    /* must not crash */
    set_store_position(0, 0, 0, 0, 0, 0, 0, 0, NULL);
}

int main(void) {
    test_create_put_get_roundtrip();
    test_get_missing_key_reports_not_found();
    test_get_on_absent_file_fails_cleanly();
    test_get_on_corrupt_file_fails_cleanly();
    test_double_commit_is_safe();
    test_delete_key_removes_all_duplicates();
    test_stored_position_roundtrip();
    test_stored_position_survives_huge_values();
    test_stored_position_rejects_partial_file();
    test_null_arguments();

    printf("%s: %d checks, %d failed\n",
           tests_failed ? "FAIL" : "PASS", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
