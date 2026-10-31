// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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
//
// The BSSIDs around the device say where it is, and they go to the persistent
// journal if they are logged. They are for the geolocation query only: this
// runs them through the real NetworkData and NetworkPositionProvider, keeps
// everything those log, and looks for an address in it.

#include <glib.h>
#include <PmLogLib.h>
#include <luna-service2/lunaservice.h>

#include <cstdarg>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>

#include "NetworkData.h"
#include "NetworkPositionProvider.h"
#include "NetworkRequestManager.h"

static GString *gLog;
static const char *gPayload;

// In this binary these stand in for the ones in PmLogLib and luna-service2.
extern "C" {

PmLogErr _PmLogMsgKV(PmLogContext, PmLogLevel, unsigned int, const char *, size_t,
                     const char *, const char *, const char *fmt, ...) {
    // NetworkRequestManager is a static, and logs after main() has returned.
    if (!gLog)
        return kPmLogErr_None;

    va_list args;
    va_start(args, fmt);
    g_string_append_vprintf(gLog, fmt, args);
    va_end(args);
    g_string_append_c(gLog, '\n');
    return kPmLogErr_None;
}

const char *LSMessageGetPayload(LSMessage *) {
    return gPayload;
}

}  // extern "C"

static const char kFirst[] = "aa:bb:cc:00:00:01";
static const char kSecond[] = "aa:bb:cc:00:00:02";
static const char kThird[] = "aa:bb:cc:00:00:03";

static bool logHasAddress() {
    static GRegex *address = g_regex_new("([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}",
                                         (GRegexCompileFlags) 0, (GRegexMatchFlags) 0, NULL);
    return g_regex_match(address, gLog->str, (GRegexMatchFlags) 0, NULL);
}

class NetworkDataTest {
public:
    static void wifiReply(NetworkData& data, const char *payload) {
        gPayload = payload;
        data.wifiAccessPointsCb(NULL, reinterpret_cast<LSMessage *>(&data));
    }
};

class NetworkPositionProviderTest {
public:
    static void wifiUpdate(NetworkPositionProvider& provider, GHashTable *accessPoints) {
        provider.onUpdateWifiData(accessPoints);
    }

    static char *wifiQuery(NetworkPositionProvider& provider) {
        return provider.createWifiQuery();
    }
};

namespace {

class FakeClient : public NetworkDataClient {
public:
    guint accessPoints = 0;
    bool sawFirst = false;

    void onUpdateCellData(const char *) override {}
    void onUpdateWifiData(GHashTable *wifiAccessPoints) override {
        accessPoints = g_hash_table_size(wifiAccessPoints);
        sawFirst = g_hash_table_contains(wifiAccessPoints, kFirst);
    }
};

}  // namespace

static GHashTable *accessPoints(std::initializer_list<std::pair<const char *, int>> list) {
    GHashTable *table = g_hash_table_new_full(g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);
    for (const auto& ap : list)
        g_hash_table_insert(table, g_strdup(ap.first), GINT_TO_POINTER(ap.second));
    return table;
}

static void test_scan_reply_logs_no_bssid(void) {
    g_string_truncate(gLog, 0);
    const std::string payload = std::string("{\"foundNetworks\":[{\"networkInfo\":{\"available\":true,"
            "\"bssInfo\":[{\"bssid\":\"") + kFirst + "\",\"signal\":-55},{\"bssid\":\"" + kSecond
            + "\",\"signal\":-70}]}}]}";

    NetworkData data(NULL);
    FakeClient client;
    data.setNetworkDataClient(&client);
    NetworkDataTest::wifiReply(data, payload.c_str());

    // It was read, and logged, and the addresses still reach the query.
    g_assert_cmpuint(client.accessPoints, ==, 2);
    g_assert_true(client.sawFirst);
    g_assert_nonnull(strstr(gLog->str, "signalLevel -55"));
    g_assert_false(logHasAddress());
}

static void test_query_logs_no_bssid(void) {
    g_string_truncate(gLog, 0);
    NetworkPositionProvider provider(NULL);

    // No Wi-Fi status and no telephony: stored, and nothing is posted.
    GHashTable *first = accessPoints({{kFirst, -40}, {kSecond, -50}});
    NetworkPositionProviderTest::wifiUpdate(provider, first);
    g_hash_table_unref(first);

    // The first query takes the whole table.
    char *query = NetworkPositionProviderTest::wifiQuery(provider);
    g_assert_nonnull(query);
    g_assert_nonnull(strstr(query, kFirst));
    g_free(query);

    // A later one compares it with the last: one kept, one gone, one new.
    GHashTable *second = accessPoints({{kFirst, -90}, {kThird, -80}});
    NetworkPositionProviderTest::wifiUpdate(provider, second);
    g_hash_table_unref(second);
    query = NetworkPositionProviderTest::wifiQuery(provider);
    g_assert_nonnull(query);
    g_assert_nonnull(strstr(query, kThird));
    g_free(query);

    g_assert_nonnull(strstr(gLog->str, "inserting value: "));
    g_assert_nonnull(strstr(gLog->str, "Existing key is vanished"));
    g_assert_nonnull(strstr(gLog->str, "signal change = "));
    g_assert_false(logHasAddress());
}

// The check above has to be able to fail.
static void test_address_is_recognised(void) {
    g_string_assign(gLog, "inserting key/value: aa:bb:cc:00:00:01, -40");
    g_assert_true(logHasAddress());
    g_string_truncate(gLog, 0);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    gLog = g_string_new(NULL);
    NetworkRequestManager::getInstance()->init();
    g_test_add_func("/network/address_is_recognised", test_address_is_recognised);
    g_test_add_func("/network/scan_reply_logs_no_bssid", test_scan_reply_logs_no_bssid);
    g_test_add_func("/network/query_logs_no_bssid", test_query_logs_no_bssid);
    int result = g_test_run();
    NetworkRequestManager::getInstance()->deInit();
    g_string_free(gLog, TRUE);
    gLog = NULL;
    return result;
}
