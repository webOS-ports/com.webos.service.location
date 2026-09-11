// Copyright (c) 2020 LG Electronics, Inc.
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


#include <Position.h>
#include <sys/time.h>
#include "LunaLocationServiceUtil.h"

void location_util_add_pos_json(jvalue_ref serviceObject, Position *pos) {
  int64_t currentTime = 0;

  if (jis_null(serviceObject) || (pos == NULL))
    return;

  //cache time is zero wiich is invalid time we will give current reply time.
  if (pos->timestamp == 0) {
    struct timeval tv;
    gettimeofday(&tv, (struct timezone *) NULL);
    currentTime = tv.tv_sec * 1000LL + tv.tv_usec / 1000;
  } else {
    currentTime = pos->timestamp;
  }

  jobject_put(serviceObject, J_CSTR_TO_JVAL("timestamp"), jnumber_create_i64(currentTime));
  jobject_put(serviceObject, J_CSTR_TO_JVAL("latitude"), jnumber_create_f64(pos->latitude));
  jobject_put(serviceObject, J_CSTR_TO_JVAL("longitude"), jnumber_create_f64(pos->longitude));
  jobject_put(serviceObject, J_CSTR_TO_JVAL("altitude"), jnumber_create_f64(pos->altitude));
  jobject_put(serviceObject, J_CSTR_TO_JVAL("direction"), jnumber_create_f64(pos->direction));
  jobject_put(serviceObject, J_CSTR_TO_JVAL("speed"), jnumber_create_f64(pos->speed));
}

void location_util_add_errorText_json(jvalue_ref serviceObject, char *errorText) {
  if (jis_null(serviceObject))
    return;

  jobject_put(serviceObject, J_CSTR_TO_JVAL("errorText"), jstring_create(errorText));
}

void location_util_form_json_reply(jvalue_ref serviceObject, bool returnValue, int errorCode) {
  if (jis_null(serviceObject))
    return;

  jobject_put(serviceObject, J_CSTR_TO_JVAL("returnValue"), jboolean_create(returnValue));
  jobject_put(serviceObject, J_CSTR_TO_JVAL("errorCode"), jnumber_create_i32(errorCode));
}

void location_util_add_acc_json(jvalue_ref serviceObject, Accuracy *acc) {
  if (jis_null(serviceObject) || (acc == NULL))
    return;

  jobject_put(serviceObject, J_CSTR_TO_JVAL("horizAccuracy"), jnumber_create_f64(acc->horizAccuracy));
  jobject_put(serviceObject, J_CSTR_TO_JVAL("vertAccuracy"), jnumber_create_f64(acc->vertAccuracy));
}

bool location_util_parsejsonAddress(jvalue_ref serviceObject, Address *addr) {
  bool mRetVal;
  jvalue_ref m_JsonSubArgument = NULL;
  raw_buffer nameBuf;

  if (jis_null(serviceObject) || addr == NULL) {
    return false;
  }

  mRetVal = jobject_get_exists(serviceObject, J_CSTR_TO_BUF("street"), &m_JsonSubArgument);

  if (mRetVal == true) {
    nameBuf = jstring_get(m_JsonSubArgument);
    addr->street = g_strdup(nameBuf.m_str);
    jstring_free_buffer(nameBuf);
  }

  m_JsonSubArgument = NULL;

  mRetVal = jobject_get_exists(serviceObject, J_CSTR_TO_BUF("country"), &m_JsonSubArgument);


  if (mRetVal == true) {
    nameBuf = jstring_get(m_JsonSubArgument);
    addr->country = g_strdup(nameBuf.m_str);
    jstring_free_buffer(nameBuf);
  }

  m_JsonSubArgument = NULL;
  mRetVal = jobject_get_exists(serviceObject, J_CSTR_TO_BUF("postcode"), &m_JsonSubArgument);

  if (mRetVal == true) {
    nameBuf = jstring_get(m_JsonSubArgument);
    addr->postcode = g_strdup(nameBuf.m_str);
    jstring_free_buffer(nameBuf);
  }

  m_JsonSubArgument = NULL;
  mRetVal = jobject_get_exists(serviceObject, J_CSTR_TO_BUF("countrycode"), &m_JsonSubArgument);

  if (mRetVal == true) {
    nameBuf = jstring_get(m_JsonSubArgument);
    addr->countrycode = g_strdup(nameBuf.m_str);
    jstring_free_buffer(nameBuf);
  }

  m_JsonSubArgument = NULL;
  mRetVal = jobject_get_exists(serviceObject, J_CSTR_TO_BUF("area"), &m_JsonSubArgument);


  if (mRetVal == true) {
    nameBuf = jstring_get(m_JsonSubArgument);
    addr->area = g_strdup(nameBuf.m_str);
    jstring_free_buffer(nameBuf);
  }

  m_JsonSubArgument = NULL;
  mRetVal = jobject_get_exists(serviceObject, J_CSTR_TO_BUF("locality"), &m_JsonSubArgument);

  if (mRetVal == true) {
    nameBuf = jstring_get(m_JsonSubArgument);
    addr->locality = g_strdup(nameBuf.m_str);
    jstring_free_buffer(nameBuf);
  }

  m_JsonSubArgument = NULL;
  mRetVal = jobject_get_exists(serviceObject, J_CSTR_TO_BUF("region"), &m_JsonSubArgument);

  if (mRetVal == true) {
    nameBuf = jstring_get(m_JsonSubArgument);
    addr->region = g_strdup(nameBuf.m_str);
    jstring_free_buffer(nameBuf);
  }

  return true;
}

bool location_util_req_has_wakeup(LSMessage *msg) {
  bool bWakeLock = false;
  jvalue_ref parsedObj = NULL;
  jvalue_ref jsonSubObject = NULL;
  JSchemaInfo schemaInfo;

  jschema_info_init(&schemaInfo, jschema_all(), NULL, NULL);
  parsedObj = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(msg)),
                         DOMOPT_NOOPT, &schemaInfo);

  if (!jis_null(parsedObj)) {

    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("wakelock"), &jsonSubObject))
      jboolean_get(jsonSubObject, &bWakeLock);

    j_release(&parsedObj);
  }

  return bWakeLock;
}

