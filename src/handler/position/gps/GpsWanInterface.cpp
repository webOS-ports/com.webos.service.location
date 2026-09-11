// Copyright (c) 2024 LG Electronics, Inc.
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


#include <pbnjson.h>
#include <pbnjson.hpp>
#include <GPSPositionProvider.h>

#define WAN_SERVICE "com.webos.service.wan"
#define LUNA_WAN_SERVICE "luna://" WAN_SERVICE
#define STR(x) #x

#define WAN_FUNC(SERVICE_API) LUNA_WAN_SERVICE "/" STR(SERVICE_API)

std::string GpsWanInterface::mApnName;

void GpsWanInterface::initialize(LSHandle *sh)
{
    _mLSHandle = sh;
    mServiceConnected = false;
    wanServiceStatus();
    settingsServiceLunaCall();
}

void GpsWanInterface::registerLunaMethods()
{
    jvalue_ref getContexts = jobject_create();

    LSError lserror;

    LSErrorInit(&lserror);

    jobject_put(getContexts, J_CSTR_TO_JVAL("subscribe"), jboolean_create(true));

    jschema_ref getContext_schema = jschema_parse(j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);

    const char *payload = jvalue_tostring(getContexts, getContext_schema);

    LSCall(_mLSHandle, WAN_FUNC(getContexts), payload, getContextCb, this, &mWanGetContext, &lserror);

    if (LSErrorIsSet(&lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    j_release(&getContexts);
    jschema_release(&getContext_schema);
}

void GpsWanInterface::serviceStarted()
{
    registerLunaMethods();

    mServiceConnected = true;
}

void GpsWanInterface::serviceStopped()
{
    LSError lserror;
    LSErrorInit(&lserror);
    mServiceConnected = false;

    if(mWanGetContext){
        LSCallCancel(_mLSHandle, mWanGetContext, &lserror);
        mWanGetContext = 0;
    }
}

bool GpsWanInterface::settingServiceCb(LSHandle *sh, LSMessage *message, void * context)
{
    pbnjson::JValue jsonRoot;
    std::string payload;
    pbnjson::JDomParser parser;
    payload = LSMessageGetPayload(message);
    string type;
    bool ReqResult = false;
    string apn;

    GpsWanInterface *wanInterface = ((GpsWanInterface*)context);

    if (!payload.empty() && parser.parse(payload, pbnjson::JSchema::NullSchema())) {
        jsonRoot = parser.getDom();
    }

    if (!jsonRoot.isNull()) {

        if(!jsonRoot.isNull() && jsonRoot["returnValue"].isBoolean()){
            ReqResult = jsonRoot["returnValue"].asBool();
        }
        if(!ReqResult){
            return false;
        }
        else{
            pbnjson::JValue settingsObj;
            if(!jsonRoot["settings"].isNull()){
                settingsObj = jsonRoot["settings"];
                pbnjson::JValue apnListObj;
                if(!settingsObj["apnList"].isNull()){
                    apnListObj = settingsObj["apnList"];
                    pbnjson::JValue::ObjectIterator iter;
                    for( iter = apnListObj.children().begin(); iter != apnListObj.children().end(); ++iter) {
                        pbnjson::JValue::KeyValue keyVal = *iter;
                        pbnjson::JValue valObj = keyVal.second;
                        if(!valObj["type"].isNull()) {
                            type = valObj["type"].asString();
                            if(type.length()!=0){
                                if(type.find("supl",0) != string::npos) {
                                  wanInterface->mApnName = valObj["apn"].asString();
                                  break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return true;
}

bool GpsWanInterface::settingsServiceLunaCall()
{
    jvalue_ref connect_obj = jobject_create();

    LSError lserror;

    LSErrorInit(&lserror);

    jobject_put(connect_obj, J_CSTR_TO_JVAL("category"), jstring_create("TelephonyApnList"));

    jschema_ref connect_schema = jschema_parse(j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);

    const char *payload = jvalue_tostring(connect_obj, connect_schema);

    LSCall(_mLSHandle, "luna://com.webos.settingsservice/getSystemSettings", payload, settingServiceCb, this, NULL, &lserror);

    if (LSErrorIsSet(&lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    j_release(&connect_obj);
    jschema_release(&connect_schema);
    return true;
}

bool GpsWanInterface::serviceUpCb(LSHandle* handle, LSMessage* message, void* ctxt)
{
    jvalue_ref parsed_obj = {0};
    jvalue_ref service_value_obj = {0};

    JSchemaInfo schema_info;
    GpsWanInterface *wanInterface = ((GpsWanInterface*)ctxt);

    bool connected_value = false;

    jschema_ref input_schema = jschema_parse(j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);
    if (!input_schema)
        return false;

    jschema_info_init(&schema_info, input_schema, NULL, NULL);

    parsed_obj = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schema_info);
    jschema_release(&input_schema);

    if (jis_null(parsed_obj))
        return false;

    if (!jobject_get_exists(parsed_obj, J_CSTR_TO_BUF("connected"), &service_value_obj))
        goto cleanup;

    jboolean_get(service_value_obj, &connected_value);

    if (connected_value)
        wanInterface->serviceStarted();
    else
        wanInterface->serviceStopped();

cleanup:
    j_release(&parsed_obj);

    return true;
}

void GpsWanInterface::wanServiceStatus()
{
    LSError lserror;
    LSErrorInit(&lserror);

    jvalue_ref service_status = jobject_create();

    jobject_put(service_status, J_CSTR_TO_JVAL("serviceName"), jstring_create(WAN_SERVICE));

    jschema_ref service_status_schema = jschema_parse(j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);

    const char *payload = jvalue_tostring(service_status, service_status_schema);

    LSCall(_mLSHandle, "palm://com.palm.lunabus/signal/registerServerStatus", payload,serviceUpCb, this, NULL, &lserror);

    if (LSErrorIsSet(&lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    j_release(&service_status);
    jschema_release(&service_status_schema);
}

bool GpsWanInterface::getContextCb(LSHandle *sh, LSMessage *message, void * context)
{
    jvalue_ref parsed_obj = {0};
    GpsWanInterface *wanInterface = ((GpsWanInterface*)context);

    jschema_ref input_schema = jschema_parse(j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);
    if (!input_schema)
        return false;

    JSchemaInfo schema_info;
    jschema_info_init(&schema_info, input_schema, NULL, NULL);
    parsed_obj = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schema_info);
    jschema_release(&input_schema);

    if (jis_null(parsed_obj))
        return false;

    jvalue_ref return_value_obj = {0};
    jvalue_ref getContext_obj = {0};
    long int context_count = 0;
    long int counter = 0;
    char *context_string = NULL;
    bool return_value = false;

    if (!jobject_get_exists(parsed_obj, J_CSTR_TO_BUF("returnValue"), &return_value_obj))
        goto cleanup;

    jboolean_get(return_value_obj, &return_value);

    if (!return_value)
        goto cleanup;

    printf_debug("enter GpsWanInterface::getContextCb\n");
    if (jobject_get_exists(parsed_obj, J_CSTR_TO_BUF("contexts"), &getContext_obj))
    {
        context_count =  jarray_size(getContext_obj);
        printf_debug("enter GpsWanInterface::getContextCb context_count %ld\n",context_count);

        for (counter = 0; counter < context_count; counter++)
        {
            jvalue_ref context_obj = jarray_get(getContext_obj, counter);
            jvalue_ref context_name_obj = {0};

            if (jobject_get_exists(context_obj, J_CSTR_TO_BUF("name"), &context_name_obj))
            {
                if (!jis_string(context_name_obj))
                     continue;

                raw_buffer method_buf = jstring_get(context_name_obj);

                if (context_string) {
                    g_free(context_string);
                    context_string = NULL;
                }

                context_string = g_strdup(method_buf.m_str);
                jstring_free_buffer(method_buf);
                printf_debug("enter GpsWanInterface::getContextCb context_string %s \n",context_string);

                if (context_string != NULL)
                {
                    if(!strcmp(context_string, "supl"))
                    {
                        bool connected_value = false;
                        bool ipv4exist = false;
                        bool ipv6exist = false;

    printf_debug("enter GpsWanInterface::getContextCb supl \n");
                        jvalue_ref connected_value_obj = {0};
                        jvalue_ref IPV4_value_obj = {0};
                        jvalue_ref IPV6_value_obj = {0};

                        if (!jobject_get_exists(context_obj, J_CSTR_TO_BUF("connected"), &connected_value_obj))
                            goto cleanup;

                        jboolean_get(connected_value_obj, &connected_value);
                        printf_debug("enter GpsWanInterface::getContextCb connected_value %d \n",connected_value);

                        if (jobject_get_exists(context_obj, J_CSTR_TO_BUF("ipv4"), &IPV4_value_obj))
                            ipv4exist = true;

                        if (jobject_get_exists(context_obj, J_CSTR_TO_BUF("ipv6"), &IPV6_value_obj))
                            ipv6exist = true;

                        NetworkInfo networkInfo;

                        networkInfo.apn = wanInterface->mApnName;
                        networkInfo.available = connected_value;
                        if (ipv4exist && ipv6exist)
                            networkInfo.bearerType = NYX_AGPS_APN_BEARER_IPV4V6;
                        else if (ipv4exist)
                            networkInfo.bearerType = NYX_AGPS_APN_BEARER_IPV4;
                        else if (ipv6exist)
                            networkInfo.bearerType = NYX_AGPS_APN_BEARER_IPV6;

                        GPSPositionProvider *mgpsProvider = GPSPositionProvider::getInstance();
                        mgpsProvider->updateNetworkState(&networkInfo);
                        break;
                    }
                }
            }
        }
    }

cleanup:
    if(context_string)
        g_free(context_string);

    j_release(&parsed_obj);

    return true;
}

bool GpsWanInterface::connectCb(LSHandle *sh, LSMessage *message, void * context)
{
    jvalue_ref parsed_obj = {0};

    printf_debug("enter GpsWanInterface::connectCb\n");
    jschema_ref input_schema = jschema_parse(j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);

    if (!input_schema)
        return false;

    JSchemaInfo schema_info;
    jschema_info_init(&schema_info, input_schema, NULL, NULL);
    parsed_obj = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schema_info);
    jschema_release(&input_schema);

    if (jis_null(parsed_obj))
    {
        return false;
    }

    jvalue_ref return_value_obj = {0};

    bool return_value = false;

    if (!jobject_get_exists(parsed_obj, J_CSTR_TO_BUF("returnValue"), &return_value_obj))
        goto cleanup;

    jboolean_get(return_value_obj, &return_value);

    if (!return_value)
        goto cleanup;

cleanup:
    j_release(&parsed_obj);

    return true;
}

bool GpsWanInterface::disconnectCb(LSHandle *sh, LSMessage *message, void * context)
{
    printf_debug("enter GpsWanInterface::disconnectCb\n");
    jvalue_ref parsed_obj = {0};

    jschema_ref input_schema = jschema_parse(j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);

    if (!input_schema)
        return false;

    JSchemaInfo schema_info;
    jschema_info_init(&schema_info, input_schema, NULL, NULL);
    parsed_obj = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schema_info);
    jschema_release(&input_schema);

    if (jis_null(parsed_obj))
    {
        return false;
    }

    jvalue_ref return_value_obj = {0};

    bool return_value = false;

    if (!jobject_get_exists(parsed_obj, J_CSTR_TO_BUF("returnValue"), &return_value_obj))
        goto cleanup;

    jboolean_get(return_value_obj, &return_value);

    if (!return_value)
        goto cleanup;

cleanup:
    j_release(&parsed_obj);

    return true;
}

void GpsWanInterface::connect()
{
    printf_debug("enter GpsWanInterface::connect\n");
    jvalue_ref connect_obj = jobject_create();

    LSError lserror;

    LSErrorInit(&lserror);

    jobject_put(connect_obj, J_CSTR_TO_JVAL("name"), jstring_create("supl"));

    jschema_ref connect_schema = jschema_parse(j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);

    const char *payload = jvalue_tostring(connect_obj, connect_schema);

    LSCall(_mLSHandle, WAN_FUNC(connect), payload, connectCb, NULL, NULL, &lserror);

    if (LSErrorIsSet(&lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    j_release(&connect_obj);
    jschema_release(&connect_schema);
}

void GpsWanInterface::disconnect()
{
    printf_debug("enter GpsWanInterface::disconnect\n");
    jvalue_ref disconnect_obj = jobject_create();

    LSError lserror;

    LSErrorInit(&lserror);

    jobject_put(disconnect_obj, J_CSTR_TO_JVAL("name"), jstring_create("supl"));

    jschema_ref disconnect_schema = jschema_parse(j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);

    const char *payload = jvalue_tostring(disconnect_obj, disconnect_schema);

    LSCall(_mLSHandle, WAN_FUNC(disconnect), payload, disconnectCb, NULL, NULL , &lserror);

    if (LSErrorIsSet(&lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    j_release(&disconnect_obj);
    jschema_release(&disconnect_schema);
}

