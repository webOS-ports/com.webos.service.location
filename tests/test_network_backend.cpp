// Copyright (c) 2026 LG Electronics, Inc.
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

// Choosing the network location server and building its address: beaconDB
// by default and without a key, Google with one, the mock server as it is.

#include <glib.h>

#include <cstring>
#include <string>

#include "NetworkLocationBackend.h"

static void test_parse_names(void) {
    g_assert_true(parseNetworkLocationBackend("google") == NetworkLocationBackend::Google);
    g_assert_true(parseNetworkLocationBackend("beacondb") == NetworkLocationBackend::BeaconDB);
    // Unset or unknown is the default, beaconDB.
    g_assert_true(parseNetworkLocationBackend(NULL) == NetworkLocationBackend::BeaconDB);
    g_assert_true(parseNetworkLocationBackend("") == NetworkLocationBackend::BeaconDB);
    g_assert_true(parseNetworkLocationBackend("Google") == NetworkLocationBackend::BeaconDB);
    g_assert_cmpstr(networkLocationBackendName(NetworkLocationBackend::Google), ==, "google");
    g_assert_cmpstr(networkLocationBackendName(NetworkLocationBackend::BeaconDB), ==, "beacondb");
}

static void test_beacondb_needs_no_key(void) {
    char url[256];
    const char *t = networkLocationBackendUrl(NetworkLocationBackend::BeaconDB);
    g_assert_cmpstr(t, ==, "https://api.beacondb.net/v1/geolocate");
    g_assert_true(buildGeolocateUrl(t, NULL, url, sizeof(url)));
    g_assert_cmpstr(url, ==, "https://api.beacondb.net/v1/geolocate");
    g_assert_true(buildGeolocateUrl(t, "", url, sizeof(url)));
    g_assert_cmpstr(url, ==, "https://api.beacondb.net/v1/geolocate");
}

static void test_google_takes_the_key(void) {
    char url[256];
    const char *t = networkLocationBackendUrl(NetworkLocationBackend::Google);
    g_assert_true(buildGeolocateUrl(t, "AIzaSy-abc_123", url, sizeof(url)));
    g_assert_cmpstr(url, ==, "https://www.googleapis.com/geolocation/v1/geolocate?key=AIzaSy-abc_123");
}

// The key wsp.conf ships decodes with a trailing newline; curl refused the
// URL that made ("URL using bad/illegal format").
static void test_google_key_is_trimmed(void) {
    char url[256];
    const char *t = networkLocationBackendUrl(NetworkLocationBackend::Google);
    g_assert_true(buildGeolocateUrl(t, " key123\n", url, sizeof(url)));
    g_assert_cmpstr(url, ==, "https://www.googleapis.com/geolocation/v1/geolocate?key=key123");
}

static void test_google_refuses_without_a_usable_key(void) {
    char url[256];
    const char *t = networkLocationBackendUrl(NetworkLocationBackend::Google);
    g_assert_false(buildGeolocateUrl(t, NULL, url, sizeof(url)));
    g_assert_cmpstr(url, ==, "");
    g_assert_false(buildGeolocateUrl(t, "", url, sizeof(url)));
    g_assert_false(buildGeolocateUrl(t, " \n", url, sizeof(url)));
    g_assert_false(buildGeolocateUrl(t, "abc&x=1", url, sizeof(url)));
    g_assert_false(buildGeolocateUrl(t, "ab cd", url, sizeof(url)));
}

static void test_too_long_is_refused(void) {
    char url[30];  // the beaconDB address is 37 characters
    const char *t = networkLocationBackendUrl(NetworkLocationBackend::BeaconDB);
    g_assert_false(buildGeolocateUrl(t, NULL, url, sizeof(url)));
    g_assert_cmpstr(url, ==, "");
    // Exactly fitting: the URL plus its terminator.
    size_t need = strlen(t) + 1;
    std::string buffer(need, 'x');
    g_assert_true(buildGeolocateUrl(t, NULL, &buffer[0], need));
    g_assert_false(buildGeolocateUrl(t, NULL, &buffer[0], need - 1));
    // The same with a key spliced in.
    const char *g = networkLocationBackendUrl(NetworkLocationBackend::Google);
    size_t withKey = strlen(g) - 2 + strlen("key123") + 1;
    std::string keyed(withKey, 'x');
    g_assert_true(buildGeolocateUrl(g, "key123", &keyed[0], withKey));
    g_assert_false(buildGeolocateUrl(g, "key123", &keyed[0], withKey - 1));
    std::string longKey(300, 'k');
    char big[256];
    g_assert_false(buildGeolocateUrl(networkLocationBackendUrl(NetworkLocationBackend::Google),
                                     longKey.c_str(), big, sizeof(big)));
}

// While a network mock location is on, the template is the mock server's
// address, which has no key slot.
static void test_mock_address_taken_as_is(void) {
    char url[256];
    g_assert_true(buildGeolocateUrl("http://127.0.0.1:5005/nlp?", "", url, sizeof(url)));
    g_assert_cmpstr(url, ==, "http://127.0.0.1:5005/nlp?");
}

static void test_headers_name_the_client(void) {
    bool userAgent = false;
    bool contentType = false;
    for (int i = 0; i < kGeolocateHeaderCount; ++i) {
        if (strncmp(kGeolocateHeaders[i], "User-Agent: ", 12) == 0 && strlen(kGeolocateHeaders[i]) > 12)
            userAgent = true;
        if (strcmp(kGeolocateHeaders[i], "Content-Type: application/json") == 0)
            contentType = true;
    }
    g_assert_true(userAgent);
    g_assert_true(contentType);
    g_assert_cmpint(kGeolocateHeaderCount, ==, 4);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/backend/parse_names", test_parse_names);
    g_test_add_func("/backend/beacondb_needs_no_key", test_beacondb_needs_no_key);
    g_test_add_func("/backend/google_takes_the_key", test_google_takes_the_key);
    g_test_add_func("/backend/google_key_is_trimmed", test_google_key_is_trimmed);
    g_test_add_func("/backend/google_refuses_without_a_usable_key", test_google_refuses_without_a_usable_key);
    g_test_add_func("/backend/too_long_is_refused", test_too_long_is_refused);
    g_test_add_func("/backend/mock_address_taken_as_is", test_mock_address_taken_as_is);
    g_test_add_func("/backend/headers_name_the_client", test_headers_name_the_client);
    return g_test_run();
}
