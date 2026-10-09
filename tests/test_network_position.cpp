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
// NetworkPositionProvider's last-fix handling, driven through the real
// processRequest() and handleResponse(). A request that arrives while another
// is running is answered from the last fix; before the first fix that fix is
// all zeroes and went out as a successful position at 0, 0, and the fix's time
// was never stored, so an old position was stamped as new.

#include <glib.h>

#include <cstring>

#include "NetworkPositionProvider.h"
#include "NetworkRequestManager.h"

namespace {

class FakeCallbacks : public ILocationCallbacks {
public:
    int calls = 0;
    ErrorCodes lastError = ERROR_NONE;
    GeoLocation last;

    void getLocationUpdateCb(GeoLocation& location, ErrorCodes errCode,
            HandlerTypes type) override {
        ++calls;
        lastError = errCode;
        last = location;
    }
    void getNmeaDataCb(long long, char *, int) override {}
    void getGpsStatusCb(int) override {}
    void getGpsSatelliteDataCb(Satellite *) override {}
    void nfwNotifyCb(nyx_gps_nfw_notification_t *) override {}
    void geofenceAddCb(int32_t, int32_t, gpointer) override {}
    void geofenceRemoveCb(int32_t, int32_t, gpointer) override {}
    void geofencePauseCb(int32_t, int32_t, gpointer) override {}
    void geofenceResumeCb(int32_t, int32_t, gpointer) override {}
    void geofenceBreachCb(int32_t, int32_t, int64_t, double, double, gpointer) override {}
    void geofenceStatusCb(int32_t, Position *, Accuracy *, gpointer) override {}
};

}  // namespace

// Reaches the provider's private state and handlers; see the friend
// declaration in NetworkPositionProvider.h.
class NetworkPositionProviderTest {
public:
    // Enabled, with a request already running - what processRequest() sees
    // for a second client while a Wi-Fi scan or query is outstanding.
    static void startRequest(NetworkPositionProvider& provider) {
        provider.mEnabled = true;
        provider.mProcessRequestInProgress = true;
    }

    static void setLastFix(NetworkPositionProvider& provider, double latitude,
                           double longitude, double accuracy, int64_t timestamp) {
        provider.mPositionData.lastLatitude = latitude;
        provider.mPositionData.lastLongitude = longitude;
        provider.mPositionData.lastAccuracy = accuracy;
        provider.mPositionData.lastTimeStamp = timestamp;
    }

    static int64_t lastTimeStamp(const NetworkPositionProvider& provider) {
        return provider.mPositionData.lastTimeStamp;
    }

    static void setLastTimeStamp(NetworkPositionProvider& provider, int64_t timestamp) {
        provider.mPositionData.lastTimeStamp = timestamp;
    }

    // An HTTP 200 answer from the geolocation server with |body|, as the
    // request manager hands it over. handleResponse() frees the task.
    static void respond(NetworkPositionProvider& provider, const char *body) {
        HttpReqTask *task = loc_create_http_task(NULL, 0, NULL, &provider);
        g_assert_nonnull(task);
        task->curlDesc.httpResponseCode = 200;
        task->responseData = g_strdup(body);
        task->responseSize = strlen(body);
        provider.handleResponse(task);
    }
};

static PositionRequest positionRequest() {
    return PositionRequest("network", POSITION_CMD);
}

static const char kFix[] =
        "{\"location\":{\"lat\":52.3731,\"lng\":4.8922},\"accuracy\":30.0}";

// The empty fix: a second client while the first request runs, with nothing
// fixed yet, must get nothing now - the running request answers it later.
static void test_no_fix_yet_reports_nothing(void) {
    NetworkPositionProvider provider(NULL);
    FakeCallbacks callbacks;
    provider.setCallback(&callbacks);
    NetworkPositionProviderTest::startRequest(provider);

    provider.processRequest(positionRequest());

    g_assert_cmpint(callbacks.calls, ==, 0);
}

// With a fix in hand the second client gets it at once, stamped with the time
// of that fix, not 0 (which the reply turns into "now").
static void test_last_fix_reported_with_its_time(void) {
    NetworkPositionProvider provider(NULL);
    FakeCallbacks callbacks;
    provider.setCallback(&callbacks);
    NetworkPositionProviderTest::startRequest(provider);
    NetworkPositionProviderTest::setLastFix(provider, 48.8584, 2.2945, 25.0,
                                            1700000000123LL);

    provider.processRequest(positionRequest());

    g_assert_cmpint(callbacks.calls, ==, 1);
    g_assert_cmpint(callbacks.lastError, ==, ERROR_NONE);
    g_assert_cmpfloat(callbacks.last.getLatitude(), ==, 48.8584);
    g_assert_cmpfloat(callbacks.last.getLongitude(), ==, 2.2945);
    g_assert_cmpfloat(callbacks.last.getHorizontalAccuracy(), ==, 25.0);
    g_assert_cmpfloat(callbacks.last.getTimeStamp(), ==, 1700000000123.0);
}

// A fix from the server is kept with its time, so the next second client is
// handed that fix and that time.
static void test_server_fix_keeps_its_time(void) {
    NetworkPositionProvider provider(NULL);
    FakeCallbacks callbacks;
    provider.setCallback(&callbacks);

    gint64 before = g_get_real_time() / 1000;
    NetworkPositionProviderTest::respond(provider, kFix);

    g_assert_cmpint(callbacks.calls, ==, 1);
    g_assert_cmpint(callbacks.lastError, ==, ERROR_NONE);
    int64_t stored = NetworkPositionProviderTest::lastTimeStamp(provider);
    g_assert_cmpint(stored, >=, before);
    g_assert_cmpfloat(callbacks.last.getTimeStamp(), ==, (double) stored);

    NetworkPositionProviderTest::startRequest(provider);
    provider.processRequest(positionRequest());
    g_assert_cmpint(callbacks.calls, ==, 2);
    // Parsed from text, so not exact: under valgrind it differs from the
    // literal in the last bit.
    g_assert_cmpfloat_with_epsilon(callbacks.last.getLatitude(), 52.3731, 1e-9);
    g_assert_cmpfloat(callbacks.last.getTimeStamp(), ==, (double) stored);
}

// The same fix again is not reported again, but it is as recent as the answer
// that confirmed it.
static void test_confirmed_fix_is_refreshed(void) {
    NetworkPositionProvider provider(NULL);
    FakeCallbacks callbacks;
    provider.setCallback(&callbacks);
    NetworkPositionProviderTest::respond(provider, kFix);
    g_assert_cmpint(callbacks.calls, ==, 1);

    // As if the fix were an hour old. Only the time changes: the position
    // stays exactly as parsed.
    int64_t old = NetworkPositionProviderTest::lastTimeStamp(provider) - 3600000;
    NetworkPositionProviderTest::setLastTimeStamp(provider, old);

    NetworkPositionProviderTest::respond(provider, kFix);

    g_assert_cmpint(callbacks.calls, ==, 1);
    g_assert_cmpint(NetworkPositionProviderTest::lastTimeStamp(provider), >, old);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    // As LocationService::init() does: responses are handed back to the
    // request manager, which needs its HTTP machinery running. Nothing is sent.
    NetworkRequestManager::getInstance()->init();
    g_test_add_func("/network/no_fix_yet_reports_nothing", test_no_fix_yet_reports_nothing);
    g_test_add_func("/network/last_fix_reported_with_its_time", test_last_fix_reported_with_its_time);
    g_test_add_func("/network/server_fix_keeps_its_time", test_server_fix_keeps_its_time);
    g_test_add_func("/network/confirmed_fix_is_refreshed", test_confirmed_fix_is_refreshed);
    int result = g_test_run();
    NetworkRequestManager::getInstance()->deInit();
    return result;
}
