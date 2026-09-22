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


#include <GPSPositionProvider.h>

using namespace std;


GQuark GPSErrorQuark(void) {
    static GQuark quark = 0;

    if (quark == 0) {
        quark = g_quark_from_static_string("geoclue-error-quark");
    }

    return quark;
}

XtraTime::XtraTime() {
    utcTime = 0;
    timeReference = 0;
    uncertainty = 0;
}

XtraData::XtraData() {
    data = "";
}

NetworkInfo::NetworkInfo() {
    available = 0;
    apn = "";
    bearerType = 0;
}

void * GPSPositionProvider::gpsThreadProc(void *args) {
    GPSPositionProvider *gpsService = (GPSPositionProvider *) args;
    nyx_error_t rc;

    printf_debug("enter gpsThreadProc\n");

    if (!gpsService) {
        printf_warning("Invalid input parameters\n");
        return nullptr;
    }

    pthread_mutex_lock(&gpsService->mGPSThreadMutex);

    do {
	while (gpsService->mGPSThreadAction == ACTION_NONE) {
		pthread_cond_wait(&gpsService->mGPSThreadCond, &gpsService->mGPSThreadMutex);
        }
        printf_debug("gps thread unblocked, action = %d\n",
                     gpsService->mGPSThreadAction);

        if (ACTION_QUIT == gpsService->mGPSThreadAction) {
            printf_debug("ACTION_QUIT\n");
            break;
        }

        switch (gpsService->mGPSThreadAction) {
            case ACTION_OPEN_ATL:
            printf_debug("ACTION_OPEN_ATL\n");

            if (gpsService->mNetworkInfo.available) {
                rc = gpsService->mGPSNyxInterface.dataConnectionOpen(
                        &gpsService->mNetworkInfo);
                printf_debug("data_conn_open returned %d\n", rc);
                gpsService->mGPSDataConnectionState = DATA_CONNECTION_OPEN;
            } else if (gpsService->mGpsWanInterface.serviceStatus()) {
                gpsService->mGPSDataConnectionState = DATA_CONNECTION_IDLE;
                gpsService->mGpsWanInterface.connect();
            }
                break;

        case ACTION_CLOSE_ATL:
            printf_debug("ACTION_CLOSE_ATL\n");

            if (gpsService->mNetworkInfo.available) {
                gpsService->mGpsWanInterface.disconnect();
            } else {
                    rc = gpsService->mGPSNyxInterface.dataConnectionClosed(
                            NYX_AGPS_TYPE_SUPL);
                            printf_debug("data_conn_closed returned %d\n", rc);
                    gpsService->mGPSDataConnectionState = DATA_CONNECTION_CLOSED;
            }
            break;

            case ACTION_FAIL_ATL:
                printf_debug("ACTION_FAIL_ATL\n");
                rc = gpsService->mGPSNyxInterface.dataConnectionFailed(
                        NYX_AGPS_TYPE_SUPL);
                printf_debug("data_conn_failed returned %d\n", rc);
                gpsService->mGPSDataConnectionState = DATA_CONNECTION_FAILED;
                break;

            case ACTION_NI_NOTIFY:
                printf_debug("ACTION_NI_NOTIFY\n");
                break;

            case ACTION_XTRA_DATA:
                printf_debug("ACTION_XTRA_DATA\n");
                rc = gpsService->mGPSNyxInterface.injectExtraData(
                        &gpsService->mXtraData);
                printf_debug("inject_xtra_data returned %d\n", rc);
                gpsService->mDownloadXtraDataStatus = IDLE;
                break;

            case ACTION_XTRA_TIME:
                printf_debug("ACTION_XTRA_TIME\n");
                rc = gpsService->mGPSNyxInterface.injectExtraTime(
                        &gpsService->mXtraTime);
                printf_debug("inject_time returned %d\n", rc);

                break;

            default:
                printf_debug("unknown event\n");
                break;
        }

        gpsService->mGPSThreadAction = ACTION_NONE;
    } while (1);

    pthread_mutex_unlock(&gpsService->mGPSThreadMutex);

    printf_debug("exit gpsThreadProc\n");
    return nullptr;
}

bool GPSPositionProvider::handleGpsEnable(void *data) {
    printf_info("requesting GPSENABLE...\n");
    if (mEngineStarted) {
        printf_info("gps engine is already started\n");
    } else {
        if (mGPSNyxInterface.startGPS() != NYX_ERROR_NONE) {
            printf_error("failed to start gps engine\n");
            return false;
        }

        if (mNMEALogEnable) {
            loc_logger_start_logging(&mNMEALogger, nullptr, "nmea");
        }

        if (mCEPLogEnable) {
            loc_logger_start_logging(&mCEPLogger, nullptr, "cep");
        }

        mEngineStarted = true;
        printf_info("succeeded to start gps engine\n");
    }
    return true;
}

bool GPSPositionProvider::handleGpsDisable(void *data) {
    printf_info("requesting GPSDISABLE...\n");
    if (!mEngineStarted) {
        printf_info("gps engine is already stopped\n");
    } else {
        if (mGpsParamModified) {
            printf_info("resetting gps parameters as defaults...\n");

            setGpsParameters(mGPSConf.mLgeGPSPositionMode,
                             NYX_GPS_POSITION_RECURRENCE_PERIODIC,
                             DEFAULT_FIX_INTERVAL, mGPSConf.mSUPLHost, mGPSConf.mSUPLPort);

            mPositionMode = mGPSConf.mLgeGPSPositionMode;
            mFixInterval = DEFAULT_FIX_INTERVAL;
            mGpsParamModified = false;
        }

        if (NYX_ERROR_NONE != mGPSNyxInterface.stopGPS()) {
            printf_error("failed to stop gps engine\n");
            return false;
        }

        if (mNMEALogEnable) {
            loc_logger_stop_logging(&mNMEALogger);
        }

        if (mCEPLogEnable) {
            char cep_info[256];

            snprintf(cep_info, sizeof(cep_info), "Reference Position: %f, %f\n",
                    loc_geometry_rtcep_get_ref_position(&mCEPCalculator)->latitude,
                    loc_geometry_rtcep_get_ref_position(&mCEPCalculator)->longitude);

            loc_logger_feed_data(&mCEPLogger, cep_info, strlen(cep_info));

            snprintf(cep_info, sizeof(cep_info), "Maximum count of measurement: %d\n",
                    loc_geometry_rtcep_get_max_count(&mCEPCalculator));

            loc_logger_feed_data(&mCEPLogger, cep_info, strlen(cep_info));

            snprintf(cep_info, sizeof(cep_info), "Measured count: %d\n",
                    loc_geometry_rtcep_get_current_count(&mCEPCalculator));

            loc_logger_feed_data(&mCEPLogger, cep_info, strlen(cep_info));
            snprintf(cep_info, sizeof(cep_info),
                    "CEP (50): %f\nDRMS (63 ~ 68): %f\n2DRMS (95 ~ 98): %f\nR95 (= CEP 95): %f\n",
                    loc_geometry_rtcep_get_cep(&mCEPCalculator),
                    loc_geometry_rtcep_get_drms(&mCEPCalculator),
                    loc_geometry_rtcep_get_2drms(&mCEPCalculator),
                    loc_geometry_rtcep_get_r95(&mCEPCalculator));

            loc_logger_feed_data(&mCEPLogger, cep_info, strlen(cep_info));

            loc_logger_stop_logging(&mCEPLogger);

            loc_geometry_rtcep_reset(&mCEPCalculator);
        }

        mEngineStarted = false;
        printf_info("succeeded to stop gps engine\n");
    }
    return true;
}

bool GPSPositionProvider::handlePeriodicUpdates(void *data) {
    printf_info("requesting PERIODICUPDATES...\n");
    setPosModeWithCapability(mPositionMode,
                             NYX_GPS_POSITION_RECURRENCE_PERIODIC, mFixInterval);
    return true;
}

bool GPSPositionProvider::handleForceTimeInjection(void *data) {
    printf_info("requesting force_time_injection...\n");
    mGPSNyxInterface.gpsRequestUtcTimeCb(&mGPSNyxInterface);
    return true;
}

bool GPSPositionProvider::handleForceXtraInjection(void *data) {
    printf_info("requesting force_xtra_injection...\n");
    if (IDLE == mDownloadXtraDataStatus)
        mGPSNyxInterface.gpsXtraDownloadRequestCb(&mGPSNyxInterface);
    return true;
}

bool GPSPositionProvider::handleDeleteAidingData(void *data) {
    printf_info("requesting delete_aiding_data...\n");
    if (NYX_ERROR_NONE != mGPSNyxInterface.deleteAidingData()) {
        printf_error("failed to delete aiding data\n");
        return false;
    }
    return true;
}

bool GPSPositionProvider::handleEnableNMEALog(void *data) {
    if (!mNMEALogger)
        mNMEALogger = loc_logger_create();

    mNMEALogEnable = true;
    printf_info("requesting enable_nmea_log...\n");
    return true;
}

bool GPSPositionProvider::handleDisableNMEALog(void *data) {
    mNMEALogEnable = false;
    printf_info("requesting disable_nmea_log...\n");
    return true;
}

bool GPSPositionProvider::handleCEPLog(void *data) {
    char *cvalue = (char *) data;
    const char *cep_values = nullptr;
    const char *offset = nullptr;
    char temp[256];
    double ref_lat, ref_lon;
    int max_count;
    ref_lat = ref_lon = 0;
    max_count = 0;

    if (cvalue == nullptr)
        return false;

    /*
     * The payload is "enable_cep_log:<lat>,<lon>,<count>" and arrives verbatim
     * from the sendExtraCommand luna call.  The segment lengths were used as
     * strncpy counts without being clamped to the destination, so a caller
     * could overflow temp[] just by sending a long enough field.
     */
    cep_values = strchr(cvalue, ':');
    if (cep_values) {
        size_t seglen;
        // latitude of the known position for CEP
        offset = strchr(cep_values + 1, ',');
        if (offset) {
            memset(temp, 0, sizeof(temp));
            seglen = (size_t)(offset - cep_values - 1);
            if (seglen >= sizeof(temp))
                seglen = sizeof(temp) - 1;
            memcpy(temp, cep_values + 1, seglen);
            ref_lat = g_ascii_strtod(temp, nullptr);
            cep_values = offset;
        }
        // longitude of the known position for CEP
        offset = strchr(cep_values + 1, ',');
        if (offset) {
            memset(temp, 0, sizeof(temp));
            seglen = (size_t)(offset - cep_values - 1);
            if (seglen >= sizeof(temp))
                seglen = sizeof(temp) - 1;
            memcpy(temp, cep_values + 1, seglen);
            ref_lon = g_ascii_strtod(temp, nullptr);
            cep_values = offset;
        }
        // maximum count of measurements for CEP
        g_strlcpy(temp, cep_values + 1, sizeof(temp));
        max_count = atoi(temp);
    }
    if (0 != ref_lat && 0 != ref_lon) {
        GEOCoordinates ref_pos = {ref_lat, ref_lon};
        if (mCEPCalculator)
            loc_geometry_rtcep_destroy(&mCEPCalculator);

        mCEPCalculator = loc_geometry_rtcep_create(&ref_pos);
        loc_geometry_rtcep_set_max_count(&mCEPCalculator, max_count);
    }
    if (mCEPLogger == nullptr)
        mCEPLogger = loc_logger_create();

    mCEPLogEnable = true;
    printf_info("requesting enable_cep_log...\n");
    return true;
}

bool GPSPositionProvider::handleDisableCEPLog(void *data) {
    mCEPLogEnable = false;
    printf_info("requesting disable_cep_log...\n");
    return true;
}

bool GPSPositionProvider::handleStartAnonymousLog(void *data) {
    char *cvalue = (char *) data;
    const char *offset = nullptr;
    char title[256];
    offset = strchr(cvalue, ':');
    memset(title, 0, 256);
    if (offset) {
        strncpy(title, offset + 1, sizeof(title)-1);
    }
    if (!mAnonymousLogger)
        mAnonymousLogger = loc_logger_create();

    loc_logger_start_logging(&mAnonymousLogger, nullptr,
                             (strlen(title) > 0) ? title : nullptr);
    printf_info("requesting start_anonymous_log...\n");
    return true;
}

bool GPSPositionProvider::handleStopAnonymousLog(void *data) {
    loc_logger_stop_logging(&mAnonymousLogger);
    printf_info("requesting stop_anonymous_log...\n");
    return true;
}

bool GPSPositionProvider::handleWriteAnonymousData(void *data) {
    char *cvalue = (char *) data;
    const char *offset = nullptr;
    int len = 0;
    offset = strchr(cvalue, ':');
    if (offset) {
        char *offset1 = g_strdup(offset);
        len = strlen(cvalue) - strlen("write_anonymous_data:");
        loc_logger_feed_data(&mAnonymousLogger, offset1 + 1, len);
        g_free(offset1);
    }
    printf_info("requesting write_anonymous_data...\n");
    return true;
}

bool GPSPositionProvider::handleExtraCommand(void *data) {

    char *cvalue = (char *) data;
    bool retVal = false;
    printf_info("requesting cvalue=%s command generic ...\n", cvalue);
    if (0 == g_strcmp0(cvalue, "force_time_injection")) {
        retVal = handleForceTimeInjection(cvalue);
    } else if (0 == g_strcmp0(cvalue, "force_xtra_injection")) {
        retVal = handleForceXtraInjection(cvalue);

    } else if (0 == g_strcmp0(cvalue, "delete_aiding_data")) {
        retVal = handleDeleteAidingData(cvalue);
    } else if (0 == g_strcmp0(cvalue, "enable_nmea_log")) {
        retVal = handleEnableNMEALog(cvalue);

    } else if (0 == g_strcmp0(cvalue, "disable_nmea_log")) {
        retVal = handleDisableNMEALog(cvalue);
    } else if (0
               == strncmp(cvalue, "enable_cep_log:", strlen("enable_cep_log:"))) {
        retVal = handleCEPLog(cvalue);
    } else if (0 == g_strcmp0(cvalue, "disable_cep_log")) {
        retVal = handleDisableCEPLog(cvalue);
    } else if (0
               == strncmp(cvalue, "start_anonymous_log:",
                          strlen("start_anonymous_log:"))) {
        retVal = handleStartAnonymousLog(cvalue);
    } else if (0 == g_strcmp0(cvalue, "stop_anonymous_log")) {
        retVal = handleStopAnonymousLog(cvalue);
    } else if (0
               == strncmp(cvalue, "write_anonymous_data:",
                          strlen("write_anonymous_data:"))) {
        retVal = handleWriteAnonymousData(cvalue);
    } else if (0
               == strncmp(cvalue, LGE_GPS_EXTRACMD, strlen(LGE_GPS_EXTRACMD))) {
        retVal =
                (NYX_ERROR_NONE == mGPSNyxInterface.injectExtraCommand(cvalue));

    }
    return retVal;
}

bool GPSPositionProvider::handleSetGpsParameterCommand(void *data) {
    if (data == nullptr)
        return false;
    char *cvalue = (char *) data;
    jvalue_ref jsonSubObject = nullptr;
    int pos_mode = 0;
    int fix_interval = 0;
    char supl_host[256];
    int supl_port = 0;
    JSchemaInfo schemaInfo;

    printf_info("value of json in setGPSParameters %s\n", cvalue);
    // parse json object
    jschema_ref input_schema = jschema_parse(j_cstr_to_buffer(SCHEMA_ANY),
                                             DOMOPT_NOOPT, nullptr);
    if (!input_schema)
        return false;
    jschema_info_init(&schemaInfo, input_schema, nullptr, nullptr);
    jvalue_ref parsedObj = jdom_parse(j_cstr_to_buffer(cvalue), DOMOPT_NOOPT,
                                      &schemaInfo);
    if (jis_null(parsedObj)) {
        jschema_release(&input_schema);
        return false;
    }
    pos_mode = mGPSConf.mLgeGPSPositionMode;
    fix_interval = DEFAULT_FIX_INTERVAL;
    g_strlcpy(supl_host, mGPSConf.mSUPLHost, sizeof(supl_host));
    supl_port = mGPSConf.mSUPLPort;
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("posmode"),
                           &jsonSubObject)) {
        jnumber_get_i32(jsonSubObject, &pos_mode);
    }
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("fixinterval"),
                           &jsonSubObject)) {
        jnumber_get_i32(jsonSubObject, &fix_interval);
    }
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("supladdress"),
                           &jsonSubObject)) {
        raw_buffer nameBuf = jstring_get(jsonSubObject);
        g_strlcpy(supl_host, (nameBuf.m_str != nullptr) ? nameBuf.m_str : "", sizeof(supl_host));
        jstring_free_buffer(nameBuf);
    }
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("suplport"),
                           &jsonSubObject)) {
        jnumber_get_i32(jsonSubObject, &supl_port);
    }
    setGpsParameters(pos_mode,
                     NYX_GPS_POSITION_RECURRENCE_PERIODIC, fix_interval, supl_host, supl_port);
    mPositionMode = pos_mode;
    mFixInterval = fix_interval;
    mGpsParamModified = true;
    j_release(&parsedObj);
    jschema_release(&input_schema);
    return true;
}

bool GPSPositionProvider::handleAddGeofenceCommand(void *data) {
    if (!data)
        return false;
    PositionRequest *request = (PositionRequest*)data;
    printf_info("requesting add geofence...\n");

    return mGPSNyxInterface.geofenceAdd(request->getGeofenceID(),
                                        request->getGeofenceLatitude(),
                                        request->getGeofenceLongitude(),
                                        request->getGeofenceRadius());
}

bool GPSPositionProvider::handleRemoveGeofenceCommand(void *data) {
    if (!data)
        return false;
    int32_t geofenceId = *(int32_t*)data;
    printf_info("requesting remove geofence, id = %d...\n", geofenceId);

    return mGPSNyxInterface.geofenceRemove(geofenceId);
}

/**
 * gpsAccuracyGetDetails:
 * @accuracy: A #struct _Accuracy
 * @level: Pointer to returned #GeoclueAccuracyLevel or %NULL
 * @horizontal_accuracy: Pointer to returned horizontal accuracy in meters or %NULL
 * @vertical_accuracy: Pointer to returned vertical accuracy in meters or %NULL
 *
 * @horizontal_accuracy and @vertical_accuracy will only be defined
 * if @level is %GEOCLUE_ACCURACY_LEVEL_DETAILED.
 */
void GPSPositionProvider::gpsAccuracyGetDetails(double *horizontal_accuracy,
                                                double *vertical_accuracy) {
    if (horizontal_accuracy != NULL) {
        *horizontal_accuracy =mLocation.getHorizontalAccuracy();
    }
    if (vertical_accuracy != NULL) {
        *vertical_accuracy = mLocation.getVerticalAccuracy();
    }
}

/**
 * gpsAccuracySetDetails:
 * @accuracy: A #struct _Accuracy
 * @level: A #GeoclueGpsAccuracyLevel
 * @horizontal_accuracy: Horizontal accuracy in meters
 * @vertical_accuracy: Vertical accuracy in meters
 *
 * Replaces @accuracy values with given ones.
 */
void GPSPositionProvider::gpsAccuracySetDetails(double horizontal_accuracy,
                                                double vertical_accuracy) {
    mLocation.setHorizontalAccuracy(horizontal_accuracy);
    mLocation.setVerticalAccuracy(vertical_accuracy);
}

void GPSPositionProvider::mutexInit() {
    pthread_mutex_init(&mGPSThreadMutex, nullptr);
    pthread_cond_init(&mGPSThreadCond, nullptr);
}

void GPSPositionProvider::mutexDestroy() {
    pthread_mutex_destroy(&mGPSThreadMutex);
    pthread_cond_destroy(&mGPSThreadCond);
}

void GPSPositionProvider::updateNetworkState(NetworkInfo *networkInfo) {

    pthread_mutex_lock(&mGPSThreadMutex);
    printf_debug("enter GPSPositionProvider::updateNetworkState %d \n",networkInfo->available);

    if ((networkInfo->available == 1) && (0 == mNetworkInfo.available))
    {
        if (!networkInfo->apn.empty())
            mNetworkInfo.apn = networkInfo->apn;

        mNetworkInfo.bearerType = networkInfo->bearerType;
        mNetworkInfo.available = 1;

        mGPSNyxInterface.updateNetworkAvailablity(&mNetworkInfo);

        printf_debug("mGPSDataConnectionState %d \n",mGPSDataConnectionState);

        if (DATA_CONNECTION_IDLE == mGPSDataConnectionState ||
            DATA_CONNECTION_FAILED == mGPSDataConnectionState) {
            mGPSThreadAction = ACTION_OPEN_ATL;
            pthread_cond_signal(&mGPSThreadCond);
        }
    } else if ((networkInfo->available == 0) && (1 == mNetworkInfo.available)) {

            mNetworkInfo.available = 0;

            if (DATA_CONNECTION_OPEN == mGPSDataConnectionState)
            {
                mGPSThreadAction = ACTION_CLOSE_ATL;
                pthread_cond_signal(&mGPSThreadCond);
            }
    }

    LS_LOG_DEBUG("Network state: available = %d, apn = %s, bearerType = %d\n",
                mNetworkInfo.available, mNetworkInfo.apn.c_str(), mNetworkInfo.bearerType);

    pthread_mutex_unlock(&mGPSThreadMutex);
}

bool GPSPositionProvider::hasCapability(unsigned int capability) {
    gboolean hasCapability = false;

    hasCapability = ((mGEngineCapabilities & capability) != 0);

    printf_info("engine capability = %d, target capability = %d, has = %d\n",
                mGEngineCapabilities, capability, hasCapability);
    return hasCapability;
}

void GPSPositionProvider::setPosModeWithCapability(uint32_t pos_mode,
                                                   uint32_t pos_recurrence, uint32_t fix_interval) {
    uint32_t capable_pos_mode;
    uint32_t capable_fix_interval;
    int rc;

    // check fix interval capability
    capable_fix_interval = fix_interval;
    if (!hasCapability(NYX_GPS_CAPABILITY_SCHEDULING)) {
        printf_warning("no capability of NYX_GPS_CAPABILITY_SCHEDULING\n");
        capable_fix_interval = DEFAULT_FIX_INTERVAL;
    }

    // check position mode capability
    capable_pos_mode = NYX_GPS_POSITION_MODE_STANDALONE;
    switch (pos_mode) {
        case NYX_GPS_POSITION_MODE_MS_BASED: {
            if (hasCapability(NYX_GPS_CAPABILITY_MSB))
                capable_pos_mode = NYX_GPS_POSITION_MODE_MS_BASED;
            else
                printf_warning("no capability of NYX_GPS_CAPABILITY_MSB\n");
        }
            break;

        case NYX_GPS_POSITION_MODE_MS_ASSISTED: {
            if (hasCapability(NYX_GPS_CAPABILITY_MSA))
                capable_pos_mode = NYX_GPS_POSITION_MODE_MS_ASSISTED;
            else
                printf_warning("no capability of NYX_GPS_CAPABILITY_MSA\n");
        }
            break;

        default: {
            capable_pos_mode = NYX_GPS_POSITION_MODE_STANDALONE;
        }
            break;
    }

    printf_debug("setting position mode...\n");
    printf_debug(
            "position mode: %d, position recurrence: %d, fix interval: %d\n",
            capable_pos_mode, pos_recurrence, capable_fix_interval);

    rc = mGPSNyxInterface.setPositionMode(capable_pos_mode, pos_recurrence,
                                          capable_fix_interval, 0, 0);
    if (NYX_ERROR_NONE != rc) {
        printf_error("failed to set position mode: error = %d\n", rc);
    }
}

void GPSPositionProvider::setGpsParameters(uint32_t pos_mode,
                                           uint32_t pos_recurrence, uint32_t fix_interval, char *supl_host,
                                           int supl_port) {
    int rc;

    printf_debug("setting agps server...\n");
    printf_debug("supl host: %s, supl port: %d\n", supl_host, supl_port);

    rc = mGPSNyxInterface.setServerInfo(NYX_AGPS_TYPE_SUPL, supl_host, supl_port);

    if (NYX_ERROR_NONE != rc) {
        printf_warning("failed to set server: error = %d\n", rc);
    }

    setPosModeWithCapability(pos_mode, pos_recurrence, fix_interval);
}

GPSStatus GPSPositionProvider::getGPSStatus() {
    return mGPSStatus;
}

void GPSPositionProvider::shutdown() {
    int thread_return;

    printf_debug("terminating gps-service...\n");

    mGPSNyxInterface.deInitialize();

    pthread_mutex_lock(&mGPSThreadMutex);
    mGPSThreadAction = ACTION_QUIT;
    pthread_cond_signal(&mGPSThreadCond);
    pthread_mutex_unlock(&mGPSThreadMutex);

    pthread_join(mGPSThreadID, (void **) &thread_return);
    printf_debug("pthread_join done\n");

    mutexDestroy();
    printf_debug("mutex destroy done\n");

    loc_http_stop();

    if (mHttpReqTask) {
        loc_http_task_destroy(&mHttpReqTask);
        mHttpReqTask = nullptr;
    }

    loc_geometry_rtcep_destroy(&mCEPCalculator);
    loc_logger_destroy(&mNMEALogger);
    loc_logger_destroy(&mCEPLogger);
    loc_logger_destroy(&mAnonymousLogger);

    printf_debug("gps-service is terminated\n");
}

bool GPSPositionProvider::init(LSHandle *sh) {
    printf_debug("init\n");

    mLSHandle = sh;
    mutexInit();

    if (pthread_create(&mGPSThreadID, nullptr, gpsThreadProc, this) != 0) {
        printf_error("failed to create gps thread\n");
        mutexDestroy();
        return false;
    }

    mGpsWanInterface.initialize(mLSHandle);
    loc_http_start();

    if (NYX_ERROR_NONE != mGPSNyxInterface.initialize(this)) {
        return false;
    }

    setGpsParameters(mGPSConf.mLgeGPSPositionMode,
                     NYX_GPS_POSITION_RECURRENCE_PERIODIC,
                     DEFAULT_FIX_INTERVAL, mGPSConf.mSUPLHost, mGPSConf.mSUPLPort);
    return true;
}

GPSPositionProvider::GPSPositionProvider() : PositionProviderInterface("GPS"),
    mLSHandle(nullptr) {
    printf_debug("ctor of GPSPositionProvider\n");
    RegisterMessageHandlers(COMMAND_GPS_ENABLE, &GPSPositionProvider::handleGpsEnable,
                            COMMAND_GPS_DISABLE, &GPSPositionProvider::handleGpsDisable,
                            COMMAND_GPS_PERIODIC_UPDATE, &GPSPositionProvider::handlePeriodicUpdates,
                            COMMAND_EXTRA_COMMAND, &GPSPositionProvider::handleExtraCommand,
                            COMMAND_SET_GPS_PARAMETER, &GPSPositionProvider::handleSetGpsParameterCommand,
                               COMMAND_ADD_GEOFENCE,&GPSPositionProvider::handleAddGeofenceCommand,
                            COMMAND_REMOVE_GEOFENCE,&GPSPositionProvider::handleRemoveGeofenceCommand,
                            HandlerBase::kEndList);

    mPositionMode = mGPSConf.mLgeGPSPositionMode;
    printf_debug("ctor exit\n");
}

void GPSPositionProvider::enable() {
    printf_info("enter GPSPositionProvider::enable\n");
    if (!mEnabled) {
        mEnabled = true;
    }
}

void GPSPositionProvider::disable() {
    printf_info("enter GPSPositionProvider::disable\n");

    if (mEnabled) {
        setCallback(nullptr);
        mEnabled = false;
    }

}

bool GPSPositionProvider::processCommand(KGpsCommands gpsCommand,
                                         const char *value) {
    return HandleMessage(gpsCommand, (void *) value);
}

ErrorCodes GPSPositionProvider::requestGpsEngineStart() {

    if (false == processCommand(COMMAND_GPS_PERIODIC_UPDATE)) {
        printf_error("Failed to request periodic updates\n");
        return ERROR_NOT_AVAILABLE;
    }

    if (false == processCommand(COMMAND_GPS_ENABLE)) {
        printf_error("Failed to request starting GPS\n");
        return ERROR_NOT_AVAILABLE;
    }

    return ERROR_NONE;
}

ErrorCodes GPSPositionProvider::getLastPosition(Position *position,
                                                Accuracy *accuracy) {
    if (get_stored_position(position, accuracy,
                            (LOCATION_DB_PREF_PATH_GPS))
        == ERROR_NOT_AVAILABLE) {
        LS_LOG_ERROR("getLastPosition Failed to read\n");
        return ERROR_NOT_AVAILABLE;
    }

    return ERROR_NONE;
}

nyx_error_t GPSPositionProvider::getDebugData(char *dest, size_t destLen)
{
    return mGPSNyxInterface.getDebugData(dest, destLen);
}

long long GPSPositionProvider::getTimeToFirstFixValue ()
{
 LS_LOG_DEBUG("getTimeToFirstFix");
 if (mLastFixTime > mFixRequestTime) {
  LS_LOG_DEBUG("mLasttime > mfixrequesttime");
  mTTFF = mLastFixTime - mFixRequestTime;
 } else {
    LS_LOG_INFO("[DEBUG] gps_handler_get_time_to_first_fix %lli ",mTTFF);
   }

    return mTTFF;
}
gboolean GPSPositionProvider::gpsTimeoutCb(gpointer userdata) {
    printf_info("timeout of gps position update\n");

    GPSPositionProvider *gpsService = (GPSPositionProvider *) userdata;
    GeoLocation location;
    if (!gpsService)
        return G_SOURCE_REMOVE;

    if (gpsService->mAPIProgressFlag & LOCATION_UPDATES_ON)
        gpsService->getCallback()->getLocationUpdateCb(location, ERROR_TIMEOUT, HANDLER_GPS);

    return G_SOURCE_REMOVE;
}

ErrorCodes GPSPositionProvider::processRequest(PositionRequest request) {
    ErrorCodes ret = ERROR_NONE;
    printf_info("enter GPSPositionProvider::processRequest\n");
    request.printRequest();

#ifdef WEBOS_TARGET_MACHINE_QEMUX86
    printf_info("emulator code no support for GPS\n");
    ret =ERROR_NOT_APPLICABLE_TO_THIS_HANDLER;
    return ret;
#endif

    if (!mEnabled) {
        LS_LOG_INFO("GPSPositionProvider is not enabled!!!");
        return ERROR_NOT_STARTED;
    }

    switch (request.getRequestType()) {
        case POSITION_CMD: {
            if (mAPIProgressFlag & LOCATION_UPDATES_ON)
                return ERROR_DUPLICATE_REQUEST;

            LS_LOG_DEBUG(" value of mTffstate in processrequest is %d: ",
                         mTTFFState);
            if (!mTTFFState) // if this state is false , that means TTFF has not been obtained yet
            {
                struct timeval tv;
                gettimeofday(&tv, (struct timezone *) NULL);
                mFixRequestTime = tv.tv_sec * 1000LL + tv.tv_usec / 1000;
            }
            ret = requestGpsEngineStart();

            if (ERROR_NONE == ret) {
                if (!mPosTimer)
                    mPosTimer = g_timeout_add_seconds(GPS_UPDATE_INTERVAL_MAX, gpsTimeoutCb, this);

                mAPIProgressFlag |= LOCATION_UPDATES_ON;
            }
        }
        break;

        case NMEA_CMD: {
            if (mAPIProgressFlag & NMEA_GET_DATA_ON)
            return ERROR_DUPLICATE_REQUEST;

            ret = requestGpsEngineStart();

            if (ERROR_NONE == ret)
                mAPIProgressFlag |= NMEA_GET_DATA_ON;
        }
        break;

        case SATELITTE_CMD: {
            if (mAPIProgressFlag & SATELLITE_GET_DATA_ON)
                return ERROR_DUPLICATE_REQUEST;
            ret = requestGpsEngineStart();

            if (ERROR_NONE == ret)
                mAPIProgressFlag |= SATELLITE_GET_DATA_ON;
            }
            break;

        case SET_GPS_PARAMETERS_CMD: {
            string command = request.getRequestParams();

            printf_info("setGpsParameters command= %s\n", command.c_str());

            if (false == processCommand(COMMAND_SET_GPS_PARAMETER, command.c_str()))
                return ERROR_NOT_AVAILABLE;
        }
        break;

        case SEND_GPS_EXTRA_CMD: {
            string command = request.getRequestParams();

            printf_info("sendExtraCommand command= %s\n", command.c_str());

            if (false == processCommand(COMMAND_EXTRA_COMMAND, command.c_str()))
                return ERROR_NOT_AVAILABLE;
        }
        break;

        case ADD_GEOFENCE_CMD: {
            if (false == processCommand(COMMAND_ADD_GEOFENCE, (const char*)&request)) {
                printf_error("Failed to Add Geofence\n");
                return ERROR_NOT_AVAILABLE;
            }
        }
        break;

        case REMOVE_GEOFENCE_CMD: {
            int geofenceID = request.getGeofenceID();
            if (false == processCommand(COMMAND_REMOVE_GEOFENCE, (const char*)(&geofenceID))) {
                printf_error("Failed to Remove Geofence\n");
                return ERROR_NOT_AVAILABLE;
            }
        }
        break;

        case STOP_GPS_CMD:
            if (mEngineStarted) {
                if (false == processCommand(COMMAND_GPS_DISABLE)) {
                    printf_error("Failed to request gps disable\n");
                    return ERROR_NOT_AVAILABLE;
                } else {
                    mAPIProgressFlag = 0;
                    if (mPosTimer) {
                        g_source_remove(mPosTimer);
                        mPosTimer = 0;
                    }
                }
            }
            break;

        case STOP_NMEA_CMD:
            mAPIProgressFlag &= ~NMEA_GET_DATA_ON;
            if (!mAPIProgressFlag) {
                if (mEngineStarted) {
                    if (false == processCommand(COMMAND_GPS_DISABLE)) {
                        printf_error("Failed to request gps disable\n");
                        return ERROR_NOT_AVAILABLE;
                    }
                }
            }
            break;

        case STOP_SATELITTE_CMD:
            mAPIProgressFlag &= ~SATELLITE_GET_DATA_ON;
            if (!mAPIProgressFlag) {
                if (mEngineStarted) {
                    if (false == processCommand(COMMAND_GPS_DISABLE)) {
                        printf_error("Failed to request gps disable\n");
                        return ERROR_NOT_AVAILABLE;
                    }
                }
            }

            break;

        case STOP_POSITION_CMD: {
            if (mPosTimer) {
                g_source_remove(mPosTimer);
                mPosTimer = 0;
            }
            mAPIProgressFlag &= ~LOCATION_UPDATES_ON;

            /*
             * Re-arm the time-to-first-fix measurement for the next session.
             *
             * mTTFFState latches on the first fix and was never cleared, so
             * both timestamps froze for the lifetime of the process and every
             * later getTimeToFirstFix returned the same stale number. Measured
             * on device: a first fix took 245s and reported 244492ms, correctly;
             * a second session two minutes later took 2010ms and still reported
             * 244492ms. TTFF is per session, so it has to reset when the
             * session ends.
             */
            mTTFFState = false;
            mFixRequestTime = 0;
            mLastFixTime = 0;
            mTTFF = 0;
            if (!mAPIProgressFlag) {
                if (mEngineStarted) {
                    if (false == processCommand(COMMAND_GPS_DISABLE)) {
                        printf_error("Failed to request gps disable\n");
                        return ERROR_NOT_AVAILABLE;
                    }
                }
            }
        }
        break;

        case UNKNOWN_CMD:
        default:
            printf_warning("Invalid command = %d\n", request.getRequestType());
            break;
    }

    return ret;
}
