// Copyright (c) 2026 LuneOS project
//
// SPDX-License-Identifier: Apache-2.0
//
// Tests for fetchXtraData: every XTRA server is tried once at most, so a
// failure cannot spin, and only a complete 200 with a body is handed on to
// be injected into the GPS chip. loc_http is replaced here by a table of
// what each server does.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <XtraDownload.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond) do { \
    tests_run++; \
    if (!(cond)) { \
        tests_failed++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

struct Server {
    bool prepareFails;
    bool requestFails;
    CURLcode curlResult;
    long status;
    const char *body;
};

static const Server GOOD = {false, false, CURLE_OK, 200, "xtra"};

static const Server *gServers;
static int gPrepareCalls;
static int gRequests;

static char url0[] = "0", url1[] = "1", url2[] = "2";
static char *const URLS[3] = {url0, url1, url2};

// A loop that does not advance must fail the test, not hang it.
static void countCall(void) {
    if (gPrepareCalls + gRequests > 100) {
        fprintf(stderr, "FAIL: fetchXtraData does not stop\n");
        exit(1);
    }
}

gboolean loc_http_task_prepare_connection(HttpReqTask **task_ref, char *url) {
    gPrepareCalls++;
    countCall();
    (*task_ref)->message = (void *) &gServers[atoi(url)];
    return !gServers[atoi(url)].prepareFails;
}

gboolean loc_http_add_request(HttpReqTask *task, gboolean sync) {
    const Server *server = (const Server *) task->message;

    gRequests++;
    countCall();
    CHECK(sync);

    // As loc-utils does before each request.
    g_free(task->responseData);
    task->responseData = nullptr;
    task->responseSize = 0;
    task->curlDesc.curlResultCode = CURLE_OK;
    task->curlDesc.httpResponseCode = 0;

    if (server->requestFails)
        return FALSE;

    task->curlDesc.curlResultCode = server->curlResult;
    task->curlDesc.httpResponseCode = server->status;
    if (server->body) {
        task->responseData = g_strdup(server->body);
        task->responseSize = strlen(server->body);
    }
    return TRUE;
}

static bool run(const Server *servers, HttpReqTask *task) {
    HttpReqTask *ref = task;

    gServers = servers;
    gPrepareCalls = 0;
    gRequests = 0;
    memset(task, 0, sizeof(*task));
    return fetchXtraData(&ref, URLS, 3);
}

static void check_nothing_left(const HttpReqTask *task) {
    CHECK(task->responseData == nullptr);
    CHECK(task->responseSize == 0);
}

static void test_first_server_answers(void) {
    Server servers[3] = {GOOD, GOOD, GOOD};
    HttpReqTask task;

    CHECK(run(servers, &task));
    CHECK(gRequests == 1);
    CHECK(task.responseSize == 4);
    CHECK(task.responseData && !memcmp(task.responseData, "xtra", 4));
    g_free(task.responseData);
}

static void test_prepare_failing_moves_to_next_server(void) {
    Server servers[3] = {GOOD, GOOD, GOOD};
    HttpReqTask task;

    servers[0].prepareFails = true;
    CHECK(run(servers, &task));
    CHECK(gPrepareCalls == 2);
    CHECK(gRequests == 1);
    g_free(task.responseData);
}

static void test_prepare_always_failing_stops(void) {
    Server servers[3] = {GOOD, GOOD, GOOD};
    HttpReqTask task;

    servers[0].prepareFails = servers[1].prepareFails = servers[2].prepareFails = true;
    CHECK(!run(servers, &task));
    CHECK(gPrepareCalls == 3);
    CHECK(gRequests == 0);
    check_nothing_left(&task);
}

static void test_request_failing_moves_to_next_server(void) {
    Server servers[3] = {GOOD, GOOD, GOOD};
    HttpReqTask task;

    servers[0].requestFails = true;
    CHECK(run(servers, &task));
    CHECK(gRequests == 2);
    g_free(task.responseData);
}

static void test_error_page_is_not_handed_on(void) {
    Server servers[3] = {GOOD, GOOD, GOOD};
    HttpReqTask task;

    servers[0].status = 503;
    servers[0].body = "<html>down</html>";
    servers[1].status = 404;
    servers[1].body = "<html>gone</html>";
    servers[2].body = "good";
    CHECK(run(servers, &task));
    CHECK(gRequests == 3);
    CHECK(task.responseData && !strcmp(task.responseData, "good"));
    g_free(task.responseData);
}

static void test_only_error_pages_leave_nothing(void) {
    Server servers[3] = {GOOD, GOOD, GOOD};
    HttpReqTask task;

    servers[0].status = servers[1].status = servers[2].status = 500;
    CHECK(!run(servers, &task));
    CHECK(gRequests == 3);
    check_nothing_left(&task);
}

static void test_cut_off_transfer_is_not_handed_on(void) {
    Server servers[3] = {GOOD, GOOD, GOOD};
    HttpReqTask task;

    servers[0].curlResult = CURLE_PARTIAL_FILE;
    servers[1].curlResult = CURLE_OPERATION_TIMEDOUT;
    servers[2].curlResult = CURLE_RECV_ERROR;
    CHECK(!run(servers, &task));
    check_nothing_left(&task);
}

static void test_empty_body_is_not_handed_on(void) {
    Server servers[3] = {GOOD, GOOD, GOOD};
    HttpReqTask task;

    servers[0].body = nullptr;
    servers[1].body = "";
    CHECK(run(servers, &task));
    CHECK(gRequests == 3);
    g_free(task.responseData);
}

static void test_no_task(void) {
    HttpReqTask *task = nullptr;

    gPrepareCalls = 0;
    CHECK(!fetchXtraData(&task, URLS, 3));
    CHECK(gPrepareCalls == 0);
}

int main(void) {
    test_first_server_answers();
    test_prepare_failing_moves_to_next_server();
    test_prepare_always_failing_stops();
    test_request_failing_moves_to_next_server();
    test_error_page_is_not_handed_on();
    test_only_error_pages_leave_nothing();
    test_cut_off_transfer_is_not_handed_on();
    test_empty_body_is_not_handed_on();
    test_no_task();

    printf("%s: %d checks, %d failed\n",
           tests_failed ? "FAIL" : "PASS", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
