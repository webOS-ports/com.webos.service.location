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

#include <cstdlib>
#include <cstring>

#include <glib.h>
#include <string>

#include <loc-utils/loc_security.h>
#include <pbnjson.h>

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

/* Standard base64 to a NUL-terminated string, NULL when empty. */
static char *decodeStandardBase64(const std::string &encoded) {
    gsize size = 0;
    guchar *raw = g_base64_decode(encoded.c_str(), &size);
    if (!raw || size == 0) {
        g_free(raw);
        return NULL;
    }
    char *key = static_cast<char *>(malloc(size + 1));
    if (key) {
        memcpy(key, raw, size);
        key[size] = '\0';
    }
    g_free(raw);
    return key;
}

char *geolocationKeyFromConfig(const char *content) {
    if (!content || !*content)
        return NULL;

    jschema_ref schema = jschema_parse(j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);
    if (schema) {
        JSchemaInfo schemaInfo;
        jschema_info_init(&schemaInfo, schema, NULL, NULL);
        jvalue_ref parsed = jdom_parse(j_cstr_to_buffer(content), DOMOPT_NOOPT, &schemaInfo);
        jschema_release(&schema);
        if (!jis_null(parsed) && jis_object(parsed)) {
            /*
             * wsp.conf, as the geocoding code reads it: "apiKey" in standard
             * base64 (MapServicesImpl uses g_base64_decode too). loc-utils'
             * decoder has its own alphabet, with "!@#$%" for "RSTUV", and
             * turned this key into garbage.
             */
            std::string encoded;
            jvalue_ref keyObj = NULL;
            if (jobject_get_exists(parsed, J_CSTR_TO_BUF("apiKey"), &keyObj) && jis_string(keyObj)) {
                raw_buffer buf = jstring_get(keyObj);
                encoded.assign(buf.m_str, buf.m_len);
                jstring_free_buffer(buf);
            }
            j_release(&parsed);
            return encoded.empty() ? NULL : decodeStandardBase64(encoded);
        }
        if (!jis_null(parsed))
            j_release(&parsed);
    }

    /*
     * Not JSON: LG's older format, the key alone in loc-utils' own base64.
     * Its decoder works in a 1024-byte buffer on its stack and writes a
     * terminator at the length of its input; keep well inside it.
     */
    if (strlen(content) >= 1000)
        return NULL;

    unsigned char *decoded = NULL;
    if (locSecurityBase64DecodeData(content, &decoded) != LOC_SECURITY_ERROR_SUCCESS || !decoded)
        return NULL;
    if (!decoded[0]) {
        free(decoded);
        return NULL;
    }
    return (char *) decoded;
}
