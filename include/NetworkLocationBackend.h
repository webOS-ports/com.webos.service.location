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

#ifndef NETWORK_LOCATION_BACKEND_H_
#define NETWORK_LOCATION_BACKEND_H_

#include <cstddef>

/*
 * Which server turns the WiFi access points and cells around the device into
 * a position. beaconDB (beacondb.net) is open, needs no key and is the
 * default; Google's Geolocation API needs a key in /etc/location/wsp.conf.
 * Both speak the same request and reply (Mozilla Location Service / Ichnaea
 * is the common ancestor), so only the address differs.
 */
enum class NetworkLocationBackend {
    BeaconDB,
    Google,
};

#define BEACONDB_GEOLOCATE_URL "https://api.beacondb.net/v1/geolocate"
#define GOOGLE_GEOLOCATE_URL   "https://www.googleapis.com/geolocation/v1/geolocate?key=%s"

/* Where the choice is kept: luna-prefs, under the service's own name
 * (LOCATION_SERVICE_NAME), next to the handler states setState writes. */
#define NETWORK_BACKEND_PREF_APP "com.webos.service.location"
#define NETWORK_BACKEND_PREF     "networkProvider"

/* "google" is Google; anything else, NULL included, is beaconDB. */
NetworkLocationBackend parseNetworkLocationBackend(const char *name);

/* The name getNetworkProvider reports and setNetworkProvider takes. */
const char *networkLocationBackendName(NetworkLocationBackend backend);

/* The address template for a backend, before any mock substitution. */
const char *networkLocationBackendUrl(NetworkLocationBackend backend);

/*
 * The URL to post to: urlTemplate with its "%s", if it has one, replaced by
 * apiKey. Surrounding whitespace is trimmed off the key (the shipped
 * wsp.conf's key ends in a newline, which made curl refuse the URL), and a key
 * that is empty or holds anything but letters, digits, '-' and '_' is refused,
 * as is a template with "%s" when there is no key. A template without "%s"
 * (beaconDB, or the mock server's address) is taken as it is. False, and out
 * left empty, when refused or when the URL does not fit in outSize.
 */
bool buildGeolocateUrl(const char *urlTemplate, const char *apiKey, char *out, size_t outSize);

/*
 * The request headers: what loc-utils sends by default (it uses these only
 * when given none) plus a User-Agent, which beaconDB asks clients to set to
 * identify themselves.
 */
extern const char *const kGeolocateHeaders[];
extern const int kGeolocateHeaderCount;

#endif /* NETWORK_LOCATION_BACKEND_H_ */
