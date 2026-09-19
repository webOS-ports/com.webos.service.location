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


#include "ConnectionStateObserver.h"
#include "SleepdSignals.h"
#include <loc_log.h>
#include <JsonUtility.h>
#include <algorithm>


void ConnectionStateObserver::RegisterListener(IConnectivityListener *l) {
    if (l == NULL)
        return;

    m_listeners.insert(l);
}

void ConnectionStateObserver::UnregisterListener(IConnectivityListener *l) {
    if (l == NULL)
        return;

    if (!m_listeners.erase(l))
         LS_LOG_DEBUG("Key not present listeners");

}

void ConnectionStateObserver::init(LSHandle *ConnHandle) {
    LSError lserror;
    LSErrorInit(&lserror);
    bool result;

    LS_LOG_DEBUG("init");

    if (ConnHandle == NULL)
        return;

    //Register for status of wifi,wan,telephony service
    result = LSRegisterServerStatusEx(ConnHandle,
                                      WIFI_SERVICE,
                                      ConnectionStateObserver::wifi_service_status_cb,
                                      this,
                                      &m_wifi_cookie,
                                      &lserror);
    if (!result) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }
    result = LSRegisterServerStatusEx(ConnHandle,
                                      CONNMGR_SERVICE,
                                      ConnectionStateObserver::wan_service_status_cb,
                                      this,
                                      &m_wan_cookie,
                                      &lserror);
    if (!result) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }
    result = LSRegisterServerStatusEx(ConnHandle,
                                      TELEPHONY_SERVICE,
                                      ConnectionStateObserver::tel_service_status_cb,
                                      this,
                                      &m_telephony_cookie,
                                      &lserror);
    if (!result) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    /*
     * Only sleepd's "resume" is watched. Its "suspended" is broadcast before
     * every attempt and says nothing about whether the device slept (see
     * SleepdSignals.h); acting on it stopped the GPS engine on each attempt
     * and restarted it on the abort that followed. A real sleep is known
     * from the kernel-typed "resume" alone.
     */
    result = LSCall(ConnHandle,
                    "luna://com.webos.service.bus/signal/addmatch",
                    "{\"category\":\"/com/palm/power\", \"method\":\"resume\"}",
                    ConnectionStateObserver::resume_cb,
                    this,
                    NULL,
                    &lserror);
    if (!result) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }
}

bool ConnectionStateObserver::_resume_cb(LSHandle *sh, LSMessage *msg) {
    const char *payload = LSMessageGetPayload(msg);
    int resumetype = sleepdParseResumeType(payload);

    if (resumetype < 0) {
        /* the addmatch reply itself, or a malformed broadcast */
        LS_LOG_DEBUG("ignoring /com/palm/power/resume without resumetype: %s",
                     payload ? payload : "(null)");
        return true;
    }

    if (!sleepdResumeIsKernelWake(resumetype)) {
        /*
         * abort_suspend (2) or pwrevent_activity (1): the attempt did not
         * happen, nothing changed for us. Debug only - on battery with the
         * screen off these arrive every few seconds.
         */
        LS_LOG_DEBUG("sleepd resume type %d: suspend did not happen, nothing to do", resumetype);
        return true;
    }

    LS_LOG_INFO("sleepd resume type %d: back from a kernel suspend\n", resumetype);
    Notify_KernelResume();

    return true;
}

void ConnectionStateObserver::finalize(LSHandle *ConnHandle) {
    LSError lserror;
    LSErrorInit(&lserror);
    bool result;

    result = LSCancelServerStatus(ConnHandle, m_telephony_cookie, &lserror);

    if (!result) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    result = LSCancelServerStatus(ConnHandle, m_wifi_cookie, &lserror);

    if (!result) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    result = LSCancelServerStatus(ConnHandle, m_wan_cookie, &lserror);

    if (!result) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }
}

void ConnectionStateObserver::Notify_WifiStateChange(bool WifiState) {
    std::for_each(m_listeners.begin(), m_listeners.end(),
                  [ & ] (IConnectivityListener *l )  {
                     l->Handle_WifiNotification(WifiState);
                        });
    LS_LOG_DEBUG("Notify_WifiStateChange\n");
}

void ConnectionStateObserver::Notify_WifiInternetStateChange(bool WifiInternetState) {
    std::for_each(m_listeners.begin(), m_listeners.end(),
           [ & ] (IConnectivityListener *l )  {
                    l->Handle_WifiInternetNotification(WifiInternetState);
                });
    LS_LOG_DEBUG("Notify_WifiInternetStateChange\n");
}

void ConnectionStateObserver::Notify_ConnectivityStateChange(bool ConnState) {
    std::for_each(m_listeners.begin(), m_listeners.end(),
           [ & ] (IConnectivityListener *l )  {
                    l->Handle_ConnectivityNotification(ConnState);
                });
    LS_LOG_DEBUG("Notify_ConnectivityStateChange\n");
}

void ConnectionStateObserver::Notify_TelephonyStateChange(bool TeleState) {
    std::for_each(m_listeners.begin(), m_listeners.end(),
           [ & ] (IConnectivityListener *l )  {
                    l->Handle_TelephonyNotification(TeleState);
                });
    LS_LOG_DEBUG("Notify_TelephonyStateChange\n");
}

void ConnectionStateObserver::Notify_KernelResume() {
    std::for_each(m_listeners.begin(), m_listeners.end(),
           [ & ] (IConnectivityListener *l )  {
                    l->Handle_KernelResumeNotification();
                });
    LS_LOG_DEBUG("Notify_KernelResume\n");
}

void ConnectionStateObserver::register_wifi_status(LSHandle *HandleConn) {
    LSError lserror;
    LSErrorInit(&lserror);
    bool result;

    result = LSCall(HandleConn,
                    "luna://com.webos.service.wifi/getstatus",
                    "{\"subscribe\":true}",
                    ConnectionStateObserver::wifi_status_cb,
                    this,
                    NULL,
                    &lserror);
    if (!result) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }
}

void ConnectionStateObserver::register_connectivity_status(LSHandle *HandleConn) {
    LSError lserror;
    LSErrorInit(&lserror);
    bool result;

    result = LSCall(HandleConn,
                    "luna://com.webos.service.connectionmanager/getStatus",
                    "{\"subscribe\":true}",
                    ConnectionStateObserver::connectivity_status_cb,
                    this,
                    NULL,
                    &lserror);
    if (!result) {
        LS_LOG_INFO("subscription failure for connection manager");
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }
}

void ConnectionStateObserver::register_telephony_status(LSHandle *HandleConn) {
    LSError lserror;
    LSErrorInit(&lserror);
    bool result;

    //Check modem status
    //Not implemented, will be implemented in future
    result = LSCall(HandleConn,
                    "luna://com.webos.service.telephony/isTelephonyReady",
                    "{\"subscribe\":true}",
                    ConnectionStateObserver::telephony_status_cb,
                    this,
                    NULL,
                    &lserror);
    if (!result) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }
}

bool ConnectionStateObserver::_wifi_status_cb(LSHandle *sh, LSMessage *message) {
    bool ret = false;
    bool found = false;
    char *wifisrvEnable = NULL;
    bool wifistatus = false;
    jvalue_ref jsonSubObj = NULL;
    jvalue_ref parsedObj = NULL;

    JSchemaInfo schemaInfo;

    jschema_ref input_schema = jschema_parse(j_cstr_to_buffer(SCHEMA_ANY), DOMOPT_NOOPT, NULL);
    if (!input_schema)
        return true;

    jschema_info_init(&schemaInfo, input_schema, NULL, NULL); // no external refs & no error handlers
    parsedObj = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schemaInfo);
    jschema_release(&input_schema);

    if (jis_null(parsedObj)) {
        return true;
    }

    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("returnValue"), &jsonSubObj)) {
        jboolean_get(jsonSubObj, &ret);
        found = true;
    }

    if (found == false) {
        Notify_WifiStateChange(wifistatus);
        j_release(&parsedObj);
        return true;
    }

    found = false;

    if (ret == false) {
        int error = -1;

        if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("errorCode"), &jsonSubObj)) {
            jnumber_get_i32(jsonSubObj, &error);
        }

        if (error == -1) {
            LS_LOG_ERROR("wifi service is not running");
            Notify_WifiStateChange(false);
        }

        j_release(&parsedObj);
        return true;
    }

    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("status"), &jsonSubObj)) {
        raw_buffer buf = jstring_get(jsonSubObj);
        wifisrvEnable = strdup(buf.m_str);
        jstring_free_buffer(buf);
        if (wifisrvEnable != NULL)
            found = true;
    }

    if (found == false) {
        Notify_WifiStateChange(wifistatus);
        j_release(&parsedObj);
        return true;
    }

    LS_LOG_DEBUG("wifisrvEnable %s", wifisrvEnable);

    if ((strcmp(wifisrvEnable, "serviceEnabled") == 0)) {
        wifistatus = true;
        Notify_WifiStateChange(wifistatus);
    } else if (strcmp(wifisrvEnable, "serviceDisabled") == 0) {
        wifistatus = false;
        Notify_WifiStateChange(wifistatus);
        Notify_WifiInternetStateChange(false);
    } else if (strcmp(wifisrvEnable, "connectionStateChanged") == 0) {
        LS_LOG_DEBUG("connectionStateChanged %s", wifisrvEnable);
        wifistatus = true;
        Notify_WifiStateChange(wifistatus);
        Notify_WifiInternetStateChange(true);
    }

    free(wifisrvEnable);

    j_release(&parsedObj);
    return true;
}

/*luna-send -i palm://com.palm.connectionmanager/getstatus '{}'
 {"returnValue":true,"isInternetConnectionAvailable":false,"wired":{"state":"disconnected"},"wifi":{"state":"connected","interfaceName":"wlan0","ipAddress":"192.168.207.11
 9","netmask":"255.255.248.0","gateway":"192.168.200.1","dns1":"8.8.8.8","dns2":"4.2.2.2","method":"dhcp","ssid":"LGSI-TEST","isWakeOnWifiEnabled":false,"onInternet":"no"}
 }*/

bool ConnectionStateObserver::_connectivity_status_cb(LSHandle *sh, LSMessage *message) {
    bool ret = false;
    bool found = false;
    bool isInternetConnectionAvailable = false;
    jvalue_ref jsonSubObj = NULL;
    jvalue_ref parsedObj = NULL;
    JSchemaInfo schemaInfo;

    jschema_ref input_schema = jschema_parse(j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);
    if (!input_schema)
        return false;

    jschema_info_init(&schemaInfo, input_schema, NULL, NULL); // no external refs & no error handlers
    parsedObj = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schemaInfo);
    jschema_release(&input_schema);

    if (jis_null(parsedObj)) {
        return true;
    }

    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("returnValue"), &jsonSubObj)) {
        jboolean_get(jsonSubObj, &ret);
        found = true;
    }

    if (found == false) {
        Notify_ConnectivityStateChange(isInternetConnectionAvailable);
        j_release(&parsedObj);
        return true;
    }

    found = false;

    if (ret) {
        if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("isInternetConnectionAvailable"), &jsonSubObj)) {
            jboolean_get(jsonSubObj, &isInternetConnectionAvailable);
            found = true;
        }

        if (found == false) {
            Notify_ConnectivityStateChange(false);
            j_release(&parsedObj);
            return true;
        }

    } else {
        int error = -1;

        if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("errorCode"), &jsonSubObj)) {
            jnumber_get_i32(jsonSubObj, &error);
        }

        if (error == -1) {
            LS_LOG_DEBUG("WAN service is not running");
            Notify_ConnectivityStateChange(false);
        }

        j_release(&parsedObj);
        return true;
    }
    LS_LOG_DEBUG("isInternetConnectionAvailable %d", isInternetConnectionAvailable);

    Notify_ConnectivityStateChange(isInternetConnectionAvailable);

    j_release(&parsedObj);
    return true;
}

bool ConnectionStateObserver::_telephony_status_cb(LSHandle *sh, LSMessage *message) {
    bool ret = false;
    bool found = false;
    bool isTelephonyAvailable = false;
    jvalue_ref extendedobj = NULL;
    jvalue_ref jsonSubObj = NULL;
    jvalue_ref parsedObj = NULL;
    JSchemaInfo schemaInfo;

    jschema_ref input_schema = jschema_parse(j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);

    if (!input_schema)
        return true;

    jschema_info_init(&schemaInfo, input_schema, NULL, NULL); // no external refs & no error handlers
    parsedObj = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schemaInfo);
    jschema_release(&input_schema);

    if (jis_null(parsedObj)) {
        return true;
    }

    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("returnValue"), &jsonSubObj)) {
        jboolean_get(jsonSubObj, &ret);
        found = true;
    }

    if (found == false) {
        Notify_TelephonyStateChange(isTelephonyAvailable);
        j_release(&parsedObj);
        return true;
    }

    if (ret == false) {
        j_release(&parsedObj);
        return true;
    } else {

        if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("extended"), &extendedobj)) {
            LS_LOG_DEBUG("extendedobj reading power state %s", jvalue_tostring_simple(extendedobj));
            if (jobject_get_exists(extendedobj, J_CSTR_TO_BUF("power"), &jsonSubObj))
                jboolean_get(jsonSubObj, &isTelephonyAvailable);
        }

    }

    Notify_TelephonyStateChange(isTelephonyAvailable);
    j_release(&parsedObj);
    return true;
}

bool ConnectionStateObserver::_wifi_service_status_cb(LSHandle *sh, const char *serviceName, bool connected) {
    if (connected) {
        LS_LOG_DEBUG("wifi service Name = %s connected", serviceName);
        register_wifi_status(sh);
    } else {
        LS_LOG_DEBUG("wifi service Name = %s disconnected", serviceName);
    }
    return true;
}

bool ConnectionStateObserver::_wan_service_status_cb(LSHandle *sh, const char *serviceName, bool connected) {
    if (connected) {
        LS_LOG_DEBUG("WAN service Name = %s connected", serviceName);
        register_connectivity_status(sh);
    } else {
        LS_LOG_DEBUG("WAN service Name = %s disconnected", serviceName);
    }
    return true;
}

bool ConnectionStateObserver::_tel_service_status_cb(LSHandle *sh, const char *serviceName, bool connected) {
    if (connected) {
        LS_LOG_DEBUG("telephony service Name = %s connected", serviceName);
        register_telephony_status(sh);
    } else {
        LS_LOG_DEBUG("telephony service Name = %s disconnected", serviceName);
    }
    return true;
}
