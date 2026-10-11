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

#include <cstring>
#include <string>

#include "NetworkLocationBackend.h"

const char *const kGeolocateHeaders[] = {
    "Accept: application/json",
    "Content-Type: application/json",
    "charsets: utf-8",
    "User-Agent: LuneOS com.webos.service.location",
};
const int kGeolocateHeaderCount = sizeof(kGeolocateHeaders) / sizeof(kGeolocateHeaders[0]);

NetworkLocationBackend parseNetworkLocationBackend(const char *name) {
    if (name && strcmp(name, "google") == 0)
        return NetworkLocationBackend::Google;
    return NetworkLocationBackend::BeaconDB;
}

const char *networkLocationBackendName(NetworkLocationBackend backend) {
    return backend == NetworkLocationBackend::Google ? "google" : "beacondb";
}

const char *networkLocationBackendUrl(NetworkLocationBackend backend) {
    return backend == NetworkLocationBackend::Google ? GOOGLE_GEOLOCATE_URL : BEACONDB_GEOLOCATE_URL;
}

static bool isKeyChar(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
           || c == '-' || c == '_';
}

static bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool buildGeolocateUrl(const char *urlTemplate, const char *apiKey, char *out, size_t outSize) {
    if (!out || outSize == 0)
        return false;
    out[0] = '\0';
    if (!urlTemplate)
        return false;

    const char *slot = strstr(urlTemplate, "%s");

    if (!slot) {
        size_t templateLength = strlen(urlTemplate);
        if (templateLength >= outSize)
            return false;
        memcpy(out, urlTemplate, templateLength + 1);
        return true;
    }

    if (!apiKey)
        return false;

    const char *keyStart = apiKey;
    const char *keyEnd = apiKey + strlen(apiKey);
    while (keyStart < keyEnd && isSpace(*keyStart))
        ++keyStart;
    while (keyEnd > keyStart && isSpace(keyEnd[-1]))
        --keyEnd;
    if (keyStart == keyEnd)
        return false;
    for (const char *c = keyStart; c < keyEnd; ++c) {
        if (!isKeyChar(*c))
            return false;
    }

    std::string url(urlTemplate, (size_t)(slot - urlTemplate));
    url.append(keyStart, (size_t)(keyEnd - keyStart));
    url.append(slot + 2);
    if (url.size() >= outSize)
        return false;

    memcpy(out, url.c_str(), url.size() + 1);
    return true;
}
