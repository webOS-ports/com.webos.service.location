// Copyright (c) 2026 LuneOS project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include <glib.h>

#include "ConnmgrStatus.h"

/* What tissot's webos-connman-adapter sent, shortened to the fields read. */
static const char *WIFI_ONLINE =
    "{\"returnValue\":true,\"isInternetConnectionAvailable\":true,"
    "\"wifi\":{\"state\":\"connected\",\"onInternet\":\"yes\",\"interfaceName\":\"wlan0\"},"
    "\"wan\":{\"connectedContexts\":[],\"connected\":false},"
    "\"wired\":{\"plugged\":false,\"state\":\"disconnected\"},\"subscribed\":true}";

/* The same link before connman's online check passed. */
static const char *WIFI_NOT_YET_ONLINE =
    "{\"returnValue\":true,\"isInternetConnectionAvailable\":false,"
    "\"wifi\":{\"state\":\"connected\",\"onInternet\":\"no\"},"
    "\"wired\":{\"plugged\":false,\"state\":\"disconnected\"}}";

static void test_wifi(void) {
    bool connected = false;
    int type = -1;

    g_assert_true(connmgrParseDataNetwork(WIFI_ONLINE, &connected, &type));
    g_assert_true(connected);
    g_assert_cmpint(type, ==, CONNMGR_NETWORK_WIFI);

    g_assert_true(connmgrParseDataNetwork(WIFI_NOT_YET_ONLINE, &connected, &type));
    g_assert_false(connected);
    g_assert_cmpint(type, ==, CONNMGR_NETWORK_WIFI);
}

static void test_mobile_and_wired(void) {
    bool connected = false;
    int type = -1;

    g_assert_true(connmgrParseDataNetwork(
        "{\"returnValue\":true,\"isInternetConnectionAvailable\":true,"
        "\"wifi\":{\"state\":\"disconnected\"},"
        "\"wan\":{\"connected\":true,\"connectedContexts\":[\"internet\"]}}",
        &connected, &type));
    g_assert_true(connected);
    g_assert_cmpint(type, ==, CONNMGR_NETWORK_MOBILE);

    g_assert_true(connmgrParseDataNetwork(
        "{\"returnValue\":true,\"isInternetConnectionAvailable\":true,"
        "\"wifi\":{\"state\":\"disconnected\"},"
        "\"wired\":{\"plugged\":true,\"state\":\"connected\"}}",
        &connected, &type));
    g_assert_true(connected);
    g_assert_cmpint(type, ==, CONNMGR_NETWORK_WIFI);

    /* no link at all */
    g_assert_true(connmgrParseDataNetwork(
        "{\"returnValue\":true,\"isInternetConnectionAvailable\":false}",
        &connected, &type));
    g_assert_false(connected);
    g_assert_cmpint(type, ==, CONNMGR_NETWORK_MOBILE);
}

static void test_not_a_status(void) {
    bool connected = true;
    int type = 42;

    /* the hub's reply when connectionmanager is not running */
    g_assert_false(connmgrParseDataNetwork(
        "{\"returnValue\":false,\"errorCode\":-1,\"errorText\":\"Service does not exist\"}",
        &connected, &type));
    /* a failed reply is not a status, whatever else it carries */
    g_assert_false(connmgrParseDataNetwork(
        "{\"returnValue\":false,\"isInternetConnectionAvailable\":true}", &connected, &type));
    g_assert_false(connmgrParseDataNetwork("{\"returnValue\":true}", &connected, &type));
    g_assert_false(connmgrParseDataNetwork(
        "{\"returnValue\":true,\"isInternetConnectionAvailable\":\"yes\"}", &connected, &type));
    g_assert_false(connmgrParseDataNetwork(
        "{\"isInternetConnectionAvailable\":true}", &connected, &type));
    g_assert_false(connmgrParseDataNetwork("[]", &connected, &type));
    g_assert_false(connmgrParseDataNetwork("not json", &connected, &type));
    g_assert_false(connmgrParseDataNetwork("", &connected, &type));
    g_assert_false(connmgrParseDataNetwork(NULL, &connected, &type));
    g_assert_false(connmgrParseDataNetwork(WIFI_ONLINE, NULL, &type));
    g_assert_false(connmgrParseDataNetwork(WIFI_ONLINE, &connected, NULL));

    /* left as they were */
    g_assert_true(connected);
    g_assert_cmpint(type, ==, 42);
}

static void test_link_state_is_matched_exactly(void) {
    bool connected = false;
    int type = -1;

    /* "connected" must be the whole state, not a prefix or a different type */
    g_assert_true(connmgrParseDataNetwork(
        "{\"returnValue\":true,\"isInternetConnectionAvailable\":true,"
        "\"wifi\":{\"state\":\"connectedish\"}}", &connected, &type));
    g_assert_cmpint(type, ==, CONNMGR_NETWORK_MOBILE);

    g_assert_true(connmgrParseDataNetwork(
        "{\"returnValue\":true,\"isInternetConnectionAvailable\":true,"
        "\"wifi\":{\"state\":\"connect\"}}", &connected, &type));
    g_assert_cmpint(type, ==, CONNMGR_NETWORK_MOBILE);

    g_assert_true(connmgrParseDataNetwork(
        "{\"returnValue\":true,\"isInternetConnectionAvailable\":true,"
        "\"wifi\":{\"state\":true},\"wired\":\"connected\"}", &connected, &type));
    g_assert_cmpint(type, ==, CONNMGR_NETWORK_MOBILE);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/connmgr/wifi", test_wifi);
    g_test_add_func("/connmgr/mobile-and-wired", test_mobile_and_wired);
    g_test_add_func("/connmgr/not-a-status", test_not_a_status);
    g_test_add_func("/connmgr/link-state-exact", test_link_state_is_matched_exactly);

    return g_test_run();
}
