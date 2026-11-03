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

#include <string.h>

#include <pbnjson.h>

#include "ConnmgrStatus.h"

static bool linkIsConnected(jvalue_ref status, const char *name) {
    jvalue_ref linkObj = NULL;
    jvalue_ref state = NULL;

    if (!jobject_get_exists(status, j_cstr_to_buffer(name), &linkObj) || !jis_object(linkObj))
        return false;

    if (!jobject_get_exists(linkObj, J_CSTR_TO_BUF("state"), &state) || !jis_string(state))
        return false;

    raw_buffer buf = jstring_get_fast(state);
    return buf.m_len == strlen("connected") &&
           strncmp(buf.m_str, "connected", buf.m_len) == 0;
}

bool connmgrParseDataNetwork(const char *payload, bool *connected, int *type) {
    if (!payload || !connected || !type)
        return false;

    jschema_ref schema = jschema_parse(j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);
    if (!schema)
        return false;

    JSchemaInfo schemaInfo;
    jschema_info_init(&schemaInfo, schema, NULL, NULL);
    jvalue_ref parsed = jdom_parse(j_cstr_to_buffer(payload), DOMOPT_NOOPT, &schemaInfo);
    jschema_release(&schema);

    if (!jis_valid(parsed))
        return false;

    bool ok = false;
    bool returnValue = false;
    bool internet = false;
    jvalue_ref value = NULL;

    if (jis_object(parsed) &&
        jobject_get_exists(parsed, J_CSTR_TO_BUF("returnValue"), &value) &&
        jis_boolean(value) && jboolean_get(value, &returnValue) == CONV_OK && returnValue &&
        jobject_get_exists(parsed, J_CSTR_TO_BUF("isInternetConnectionAvailable"), &value) &&
        jis_boolean(value) && jboolean_get(value, &internet) == CONV_OK) {
        *connected = internet;
        *type = (linkIsConnected(parsed, "wifi") || linkIsConnected(parsed, "wired"))
                ? CONNMGR_NETWORK_WIFI : CONNMGR_NETWORK_MOBILE;
        ok = true;
    }

    j_release(&parsed);
    return ok;
}
