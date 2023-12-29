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


#include <stdio.h>
#include "LocationService.h"
#include "MockLocation.h"
#include <JsonUtility.h>
#include <LunaLocationServiceUtil.h>
#include <lunaprefs.h>
#include <random>

using namespace std;

#define LOC_REQ_LOG_MAX_SIZE        (512 * 1024)
#define LOC_REQ_LOG_MAX_ROTATION    2
#define LOC_REQ_LOG_TITLE           "location_requests_info"

const double LocationService::INVALID_LAT = 0;
const double LocationService::INVALID_LONG = 0;

/**
 @var LSMethod LocationService::rootMethod[]
 methods belonging to root category
 */
LSMethod LocationService::rootMethod[] = {
        {"getNmeaData",               LocationService::_getNmeaData},
        {"getReverseLocation",        LocationService::_getReverseLocation},
        {"getGeoCodeLocation",        LocationService::_getGeoCodeLocation},
        {"getAllLocationHandlers",    LocationService::_getAllLocationHandlers},
        {"getGpsStatus",              LocationService::_getGpsStatus},
        {"setState",                  LocationService::_setState},
        {"getState",                  LocationService::_getState},
        {"getLocationHandlerDetails", LocationService::_getLocationHandlerDetails},
        {"getGpsSatelliteData",       LocationService::_getGpsSatelliteData},
        {"getTimeToFirstFix",         LocationService::_getTimeToFirstFix},
        {"getLocationUpdates",        LocationService::_getLocationUpdates},
        {"getCachedPosition",         LocationService::_getCachedPosition},
        {"sendExtraCommand",          LocationService::_sendExtraCommand},
        {"stopGPS",                   LocationService::_stopGPS},
        {"exitLocation",              LocationService::_exitLocation},
        {"setGPSParameters",          LocationService::_setGPSParameters},
        {0,                           0}
};

/**
 @var LSMethod LocationService::prvMethod[]
 methods belonging to root private category
 */
LSMethod LocationService::prvMethod[] = {
//        {"sendExtraCommand", LocationService::_sendExtraCommand},
//        {"stopGPS",          LocationService::_stopGPS},
 //       {"exitLocation",     LocationService::_exitLocation},
//        {"setGPSParameters", LocationService::_setGPSParameters},
        {0,                  0}
};

/**
 @var LSMethod LocationService::geofenceMethod[]
 methods belonging to geofence category
 */
LSMethod LocationService::geofenceMethod[] = {
        {"addGeofenceArea",    LocationService::_addGeofenceArea},
        {"removeGeofenceArea", LocationService::_removeGeofenceArea},
        /*{ "pauseGeofenceArea", LocationService::_pauseGeofence},
        { "resumeGeofenceArea", LocationService::_resumeGeofence},*/ //to uncommented later once geofence lib is available.
        {0,                    0}
};

LocationService *LocationService::locService = NULL;
const char *LocationService::geofenceStateText[GEOFENCE_MAXIMUM] = {
        "added",                // GEOFENCE_ADD_SUCCESS
        "removed",              // GEOFENCE_REMOVE_SUCCESS
        "paused",               // GEOFENCE_PAUSE_SUCCESS
        "resumed",              // GEOFENCE_RESUME_SUCCESS
        "transitionEntered",    // GEOFENCE_TRANSITION_ENTERED
        "transitionExited",     // GEOFENCE_TRANSITION_EXITED
        "transitionUncertain"   // GEOFENCE_TRANSITION_UNCERTAIN
};

LSMethod LocationService::mockPublicMethod[] = {
    { "setLocation", LocationService::_setMockLocation },
    { 0, 0 },
};

LSMethod LocationService::mockPrivateMethod[] = {
    { "enable", LocationService::_enableMockLocation },
    { "disable", LocationService::_disableMockLocation },
    { 0, 0 },
};

LocationService *LocationService::getInstance() {
    static LocationService _instance;
    return &_instance;
}

LocationService::LocationService() :
        mGpsStatus(false),
        mNwStatus(false),
        suspended_state(false),
        htPseudoGeofence(nullptr),
        mServiceHandle(nullptr),
        m_lifeCycleMonitor(nullptr),
        m_enableSuspendBlocker(false),
        nwGeolocationKey(nullptr),
        lbsGeocodeKey(nullptr),
        location_request_logger(nullptr),
        mCachedGpsEngineStatus(false),
        wifistate(false),
        isInternetConnectionAvailable(false),
        isTelephonyAvailable(false),
        isWifiInternetAvailable(false),
        mMainLoop(nullptr),
        mNetReqMgr(nullptr),
        mLBSProvider(nullptr),
        mNetworkProvider(nullptr),
        mGPSProvider(nullptr),
        connectionStateObserverObj(nullptr) {
    LS_LOG_DEBUG("LocationService object created");
}

void LocationService::LSErrorPrintAndFree(LSError *ptrLSError) {
    if (ptrLSError != NULL) {
        LSErrorPrint(ptrLSError, stderr);
        LSErrorFree(ptrLSError);
    }
}

bool LocationService::init(GMainLoop *mainLoop) {
    memset(is_geofenceId_used, 0x00, MAX_GEOFENCE_ID);

    if (locationServiceRegister(LOCATION_SERVICE_NAME, mainLoop, &mServiceHandle) == false) {
        LS_LOG_ERROR("com.webos.service.location service registration failed");
        return false;
    }

    LS_LOG_DEBUG("mServiceHandle =%p", mServiceHandle);

    mNetReqMgr = NetworkRequestManager::getInstance();
    mNetReqMgr->init();

    //create providers
    mLBSProvider = LocationWebServiceProvider::getInstance();

    if (nullptr == mLBSProvider)
        return false;

    mNetworkProvider = new(nothrow) NetworkPositionProvider(mServiceHandle);

    if (!mNetworkProvider) {
        LS_LOG_ERROR("failed to create NetworkProvider");
        return false;
    }

    mNetworkProvider->enable();
    mNetworkProvider->setCallback(this);

    mGPSProvider = GPSPositionProvider::getInstance();
    bool bGPSEnabled = mGPSProvider->init(mServiceHandle);
    if (bGPSEnabled) {
        mGPSProvider->enable();
        mGPSProvider->setCallback(this);
    }

    //Load initial settings from DB
    mGpsStatus = loadHandlerStatus(GPS);
    mNwStatus = loadHandlerStatus(NETWORK);

    if (LSMessageInitErrorReply() == false) {
        return false;
    }

    // For memory check tool
    mMainLoop = mainLoop;

  if (htPseudoGeofence == nullptr) {
        htPseudoGeofence = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                 (GDestroyNotify) g_free, NULL);
   }

  if (location_request_logger == nullptr)
        location_request_logger = loc_logger_create();

    loc_logger_start_logging_with_rotation(&location_request_logger,
                                           "/var/log",
                                           LOC_REQ_LOG_TITLE,
                                           LOC_REQ_LOG_MAX_ROTATION,
                                           LOC_REQ_LOG_MAX_SIZE);

    // SUSPEND BLOCKER
    if (!m_lifeCycleMonitor && m_enableSuspendBlocker) {
        m_lifeCycleMonitor = new LifeCycleMonitor();
    }

    if (m_lifeCycleMonitor)
        m_lifeCycleMonitor->registerSuspendMonitor(mServiceHandle);

    LS_LOG_INFO("Successfully LocationService object initialized");

    connectionStateObserverObj = new(std::nothrow) ConnectionStateObserver();

  if (connectionStateObserverObj == nullptr) {
       return false;
    }

    connectionStateObserverObj->init(getPrivatehandle());
    //Register
    connectionStateObserverObj->RegisterListener(this);
    connectionStateObserverObj->RegisterListener(mNetworkProvider);

    return true;
}

bool LocationService::locationServiceRegister(const char *srvcname, GMainLoop *mainLoop, LSHandle **msvcHandle) {
    bool bRetVal; // changed to local variable from class variable
    LSError mLSError;
    LSErrorInit(&mLSError);

    // Register webos service
    bRetVal = LSRegister(srvcname, msvcHandle, &mLSError);
    LSERROR_CHECK_AND_PRINT(bRetVal, mLSError);

    //Gmain attach
    bRetVal = LSGmainAttach(*msvcHandle, mainLoop, &mLSError);
    LSERROR_CHECK_AND_PRINT(bRetVal, mLSError);

    // add root category public
    bRetVal = LSRegisterCategoryAppend(*msvcHandle, "/", rootMethod, NULL, &mLSError);
    LSERROR_CHECK_AND_PRINT(bRetVal, mLSError);

    // add root category private
    bRetVal = LSRegisterCategoryAppend(*msvcHandle, "/", prvMethod, NULL, &mLSError);
    LSERROR_CHECK_AND_PRINT(bRetVal, mLSError);

    bRetVal = LSCategorySetData(*msvcHandle, "/", this, &mLSError);
    LSERROR_CHECK_AND_PRINT(bRetVal, mLSError);

    // add geofence category
    bRetVal = LSRegisterCategoryAppend(*msvcHandle, "/geofence", geofenceMethod, NULL, &mLSError);
    LSERROR_CHECK_AND_PRINT(bRetVal, mLSError);

    bRetVal = LSCategorySetData(*msvcHandle, "/geofence", this, &mLSError);
    LSERROR_CHECK_AND_PRINT(bRetVal, mLSError);

    // add mock categoty
    bRetVal = LSRegisterCategoryAppend(*msvcHandle, "/mock", mockPublicMethod, NULL, &mLSError);
    LSERROR_CHECK_AND_PRINT(bRetVal, mLSError);

    bRetVal = LSRegisterCategoryAppend(*msvcHandle, "/mock", mockPrivateMethod, NULL, &mLSError);
    LSERROR_CHECK_AND_PRINT(bRetVal, mLSError);

    bRetVal = LSCategorySetData(*msvcHandle, "/mock", this, &mLSError);
    LSERROR_CHECK_AND_PRINT(bRetVal, mLSError);

    //Register cancel function cb to publicbus
    bRetVal = LSSubscriptionSetCancelFunction(*msvcHandle,
                                              LocationService::_cancelSubscription,
                                              this,
                                              &mLSError);
    LSERROR_CHECK_AND_PRINT(bRetVal, mLSError);
    return true;
}

LocationService::~LocationService(){

}


bool LocationService::deinit() {
    // Reviewed by Sathya & Abhishek
    // If only GetCurrentPosition is called and if service is killed then we need to free the memory allocated for handler.
    bool ret;

    LSMessageReleaseErrorReply();

    if (htPseudoGeofence) {
        g_hash_table_destroy(htPseudoGeofence);
        htPseudoGeofence = NULL;
    }

    if (nwGeolocationKey) {
        g_free(nwGeolocationKey);
        nwGeolocationKey = NULL;
    }

    if (lbsGeocodeKey) {
        g_free(lbsGeocodeKey);
        lbsGeocodeKey = NULL;
    }

    //Unregister
    connectionStateObserverObj->UnregisterListener(this);
    connectionStateObserverObj->finalize(getPrivatehandle());


    connectionStateObserverObj->UnregisterListener(getNwProvider());
    delete connectionStateObserverObj;

    LSError m_LSErr;
    LSErrorInit(&m_LSErr);
    ret = LSUnregister(mServiceHandle, &m_LSErr);

    if (ret == false) {
        LSErrorPrint(&m_LSErr, stderr);
        LSErrorFree(&m_LSErr);
    }

    loc_logger_destroy(&location_request_logger);



    // SUSPEND BLOCKER
    if (m_lifeCycleMonitor) {
        delete m_lifeCycleMonitor;
        m_lifeCycleMonitor = NULL;
    }

    mNetReqMgr->deInit();

    delete mNetworkProvider;

    return true;
}

/*****API's exposed to applications****/

/**
 * <Funciton >   getNmeaData
 * <Description>  API to get repeated nmea data if the subscribe is set to TRUE
 * @param     LunaService handle
 * @param     LunaService message
 * @param     user data
 * @return    successful return true else false
 */
bool LocationService::getNmeaData(LSHandle *sh, LSMessage *message, void *data) {
    printMessageDetails("LUNA-API", message, sh);
    int ret;
    LSError mLSError;

    LSErrorInit(&mLSError);
    jvalue_ref parsedObj = NULL;
    LocationErrorCode errorCode = LOCATION_SUCCESS;

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_GET_NMEA_DATA, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in getNmeaData");
        return true;
    }

    if (getHandlerStatus(GPS) == false) {
        errorCode = LOCATION_LOCATION_OFF;
        goto EXIT;
    }

    // Add to subsciption list with method name as key
    LS_LOG_DEBUG("isSubcriptionListEmpty = %d", isSubscListFilled(message, SUBSC_GPS_GET_NMEA_KEY, false));
    bool mRetVal;
    mRetVal = LSSubscriptionAdd(sh, SUBSC_GPS_GET_NMEA_KEY, message, &mLSError);

    if (mRetVal == false) {
        LS_LOG_ERROR("Failed to add to subscription list");
        LSErrorPrintAndFree(&mLSError);
        errorCode = LOCATION_UNKNOWN_ERROR;
        goto EXIT;
    }

    LS_LOG_DEBUG("Call getNmeaData handler");

    ret = ERROR_NONE;
    if (suspended_state == false) {
        PositionRequest request("GPS", NMEA_CMD);
        ret = mGPSProvider->processRequest(request);
    }

    if (ret == ERROR_NOT_AVAILABLE)
    {
        errorCode = LOCATION_GPS_NYX_SOURCE_UNAVAILABLE;
        goto EXIT;
    }
    else if (ret != ERROR_NONE && ret != ERROR_DUPLICATE_REQUEST) {
        LS_LOG_ERROR("Error in getNmeaData");
        errorCode = LOCATION_UNKNOWN_ERROR;
        goto EXIT;
    }

    /* SUSPEND BLOCKER
    if (m_enableSuspendBlocker)
        m_lifeCycleMonitor->setWakeLock(true);
    */

    EXIT:
    if (!jis_null(parsedObj))
        j_release(&parsedObj);

    if (errorCode != LOCATION_SUCCESS)
        LSMessageReplyError(sh, message, errorCode);

    return true;
}

void LocationService::getReverseGeocodeData(jvalue_ref *parsedObj, GString **posData, Position *pos) {
    jvalue_ref jsonSubObject = NULL;
    jvalue_ref arrObject = NULL;
    std::string strlat;
    std::string strlng;

    if (jobject_get_exists(*parsedObj, J_CSTR_TO_BUF("latitude"), &jsonSubObject)) {
        jnumber_get_f64(jsonSubObject, &pos->latitude);
        strlat = std::to_string(pos->latitude);
    }

    if (jobject_get_exists(*parsedObj, J_CSTR_TO_BUF("longitude"), &jsonSubObject)) {
        jnumber_get_f64(jsonSubObject, &pos->longitude);
        strlng = std::to_string(pos->longitude);
    }

    *posData = g_string_new("latlng=");
    g_string_append(*posData, strlat.c_str());
    g_string_append(*posData, ",");
    g_string_append(*posData, strlng.c_str());

    raw_buffer nameBuf;

    if (jobject_get_exists(*parsedObj, J_CSTR_TO_BUF("language"), &jsonSubObject)) {
        nameBuf = jstring_get(jsonSubObject);
        g_string_append(*posData, "&language=");
        g_string_append(*posData, nameBuf.m_str);
        jstring_free_buffer(nameBuf);
    }

    if (jobject_get_exists(*parsedObj, J_CSTR_TO_BUF("result_type"), &jsonSubObject)) {
        long int size = jarray_size(jsonSubObject);
        g_string_append(*posData, "&result_type=");
        LS_LOG_DEBUG("result_type size [%d]", size);
        for (int i = 0; i < size; i++) {
            arrObject = jarray_get(jsonSubObject, i);
            nameBuf = jstring_get(arrObject);

            if (i > 0)
                g_string_append(*posData, "|");

            g_string_append(*posData, nameBuf.m_str);
            jstring_free_buffer(nameBuf);
        }
    }

    if (jobject_get_exists(*parsedObj, J_CSTR_TO_BUF("location_type"), &jsonSubObject)) {
        long int size = jarray_size(jsonSubObject);
        g_string_append(*posData, "&location_type=");
        LS_LOG_DEBUG("location_type size [%d]", size);

        for (long int i = 0; i < size; i++) {
            arrObject = jarray_get(jsonSubObject, i);
            nameBuf = jstring_get(arrObject);

            if (i > 0)
                g_string_append(*posData, "|");

            g_string_append(*posData, nameBuf.m_str);

            jstring_free_buffer(nameBuf);
        }
    }
}

bool LocationService::getReverseLocation(LSHandle *sh, LSMessage *message, void *data) {
    printMessageDetails("LUNA-API", message, sh);
    ErrorCodes ret;
    jvalue_ref parsedObj = NULL;
    GString *posData = NULL;
    Position pos;
    LocationErrorCode errorCode = LOCATION_SUCCESS;
    GeoLocation geoLocInfo("");
    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_GET_GOOGLE_REVERSE_LOCATION, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in getReverseLocation");
        return true;
    }

    errorCode = mLBSProvider->readWSPConfiguration();
    if (errorCode != LOCATION_SUCCESS) {
        goto EXIT;
    }

    if (!isInternetConnectionAvailable /*&& !isWifiInternetAvailable*/) {
        errorCode = LOCATION_DATA_CONNECTION_OFF;
        goto EXIT;
    }

    getReverseGeocodeData(&parsedObj, &posData, &pos);
    geoLocInfo = GeoLocation(posData->str);

    ret = mLBSProvider->getGeocodeImpl()->reverseGeoCode(geoLocInfo,
                                                                bind(&LocationService::reverseGeocodingCb, this,
                                                                     placeholders::_1, placeholders::_2,
                                                                     placeholders::_3),
                                                                FALSE,
                                                                message);
    if (ret != ERROR_NONE) {
        errorCode = LOCATION_UNKNOWN_ERROR;
        goto EXIT;
    } else {
        LSMessageRef(message);
    }

    /* SUSPEND BLOCKER
    if (m_enableSuspendBlocker)
        m_lifeCycleMonitor->setWakeLock(true);
    */

    EXIT:
    if (!jis_null(parsedObj))
        j_release(&parsedObj);

    if (errorCode != LOCATION_SUCCESS) {
        LSMessageReplyError(sh, message, errorCode);
    }

    if (posData != NULL)
        g_string_free(posData, TRUE);

    return true;
}

void LocationService::getGeocodeData(jvalue_ref *parsedObj, GString *addressData) {
    jvalue_ref jsonBoundSubObject = NULL;
    jvalue_ref jsonSubObject = NULL;
    jvalue_ref jsonComponetObject = NULL;
    raw_buffer nameBuf;

    if (jobject_get_exists(*parsedObj, J_CSTR_TO_BUF("components"), &jsonComponetObject)) {

        if (jobject_get_exists(*parsedObj, J_CSTR_TO_BUF("address"), &jsonSubObject))
            g_string_append(addressData, "&components=");
        else
            g_string_append(addressData, "components=");

        if (jobject_get_exists(jsonComponetObject, J_CSTR_TO_BUF("route"), &jsonSubObject)) {
            nameBuf = jstring_get(jsonSubObject);
            g_string_append(addressData, "route:");
            g_string_append(addressData, nameBuf.m_str);
            jstring_free_buffer(nameBuf);
        }

        if (jobject_get_exists(jsonComponetObject, J_CSTR_TO_BUF("locality"), &jsonSubObject)) {
            nameBuf = jstring_get(jsonSubObject);
            g_string_append(addressData, "|locality:");
            g_string_append(addressData, nameBuf.m_str);
            jstring_free_buffer(nameBuf);
        }

        if (jobject_get_exists(jsonComponetObject, J_CSTR_TO_BUF("administrative_area"), &jsonSubObject)) {
            nameBuf = jstring_get(jsonSubObject);
            g_string_append(addressData, "|administrative_area:");
            g_string_append(addressData, nameBuf.m_str);
            jstring_free_buffer(nameBuf);
        }

        if (jobject_get_exists(jsonComponetObject, J_CSTR_TO_BUF("postal_code"), &jsonSubObject)) {
            nameBuf = jstring_get(jsonSubObject);
            g_string_append(addressData, "|postal_code:");
            g_string_append(addressData, nameBuf.m_str);
            jstring_free_buffer(nameBuf);
        }

        if (jobject_get_exists(jsonComponetObject, J_CSTR_TO_BUF("country"), &jsonSubObject)) {
            nameBuf = jstring_get(jsonSubObject);
            g_string_append(addressData, "|country:");
            g_string_append(addressData, nameBuf.m_str);
            jstring_free_buffer(nameBuf);
        }
    }

    if (jobject_get_exists(*parsedObj, J_CSTR_TO_BUF("bounds"), &jsonBoundSubObject)) {
        double southwestLat;
        double southwestLon;
        double northeastLat;
        double northeastLon;

        jsonSubObject = jobject_get(jsonBoundSubObject, J_CSTR_TO_BUF("southwestLat"));
        jnumber_get_f64(jsonSubObject, &southwestLat);
        std::string strsouthwestLat = std::to_string(southwestLat);
        jsonSubObject = jobject_get(jsonBoundSubObject, J_CSTR_TO_BUF("southwestLon"));
        jnumber_get_f64(jsonSubObject, &southwestLon);
        std::string strsouthwestLon = std::to_string(southwestLon);
        jsonSubObject = jobject_get(jsonBoundSubObject, J_CSTR_TO_BUF("northeastLat"));
        jnumber_get_f64(jsonSubObject, &northeastLat);
        std::string strnortheastLat = std::to_string(northeastLat);
        jsonSubObject = jobject_get(jsonBoundSubObject, J_CSTR_TO_BUF("northeastLon"));
        jnumber_get_f64(jsonSubObject, &northeastLon);
        std::string strnortheastLon = std::to_string(northeastLon);

        LS_LOG_DEBUG("value of bounds %f,%f,%f", southwestLat, southwestLon, northeastLat);
        g_string_append(addressData, "&bounds=");
        g_string_append(addressData, strsouthwestLat.c_str());
        g_string_append(addressData, ",");
        g_string_append(addressData, strsouthwestLon.c_str());
        g_string_append(addressData, "|");
        g_string_append(addressData, strnortheastLat.c_str());
        g_string_append(addressData, ",");
        g_string_append(addressData, strnortheastLon.c_str());
    }

    if (jobject_get_exists(*parsedObj, J_CSTR_TO_BUF("language"), &jsonSubObject)) {
        nameBuf = jstring_get(jsonSubObject);
        g_string_append(addressData, "&language=");
        g_string_append(addressData, nameBuf.m_str);
        jstring_free_buffer(nameBuf);
    }

    if (jobject_get_exists(*parsedObj, J_CSTR_TO_BUF("region"), &jsonSubObject)) {
        nameBuf = jstring_get(jsonSubObject);
        g_string_append(addressData, "&region=");
        g_string_append(addressData, nameBuf.m_str);
        jstring_free_buffer(nameBuf);
    }

}

bool LocationService::getGeoCodeLocation(LSHandle *sh, LSMessage *message,
                                         void *data) {
    printMessageDetails("LUNA-API", message, sh);
    jvalue_ref parsedObj = NULL;
    jvalue_ref jsonSubObject = NULL;
    int errorCode = LOCATION_SUCCESS;
    ErrorCodes ret = ERROR_NONE;
    GString *addressData = NULL;
    bool bRetVal = false;
    GeoAddress geoAddressInfo("");

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_GET_GOOGLE_GEOCODE_LOCATION, &parsedObj)) {
        LS_LOG_ERROR("LSMessageValidateSchemaReplyOnError");
        return true;
    }

    errorCode = mLBSProvider->readWSPConfiguration();
    if (errorCode != LOCATION_SUCCESS) {
        goto EXIT;
    }

    if (!isInternetConnectionAvailable /*&& !isWifiInternetAvailable*/) {
        errorCode = LOCATION_DATA_CONNECTION_OFF;
        goto EXIT;
    }

    if ((!jobject_get_exists(parsedObj, J_CSTR_TO_BUF("address"),
                             &jsonSubObject))
        && (!jobject_get_exists(parsedObj, J_CSTR_TO_BUF("components"),
                                &jsonSubObject))) {
        LS_LOG_ERROR("Schema validation error");
        errorCode = LOCATION_INVALID_INPUT;
        goto EXIT;
    }

    addressData = g_string_new(NULL);
    bRetVal = jobject_get_exists(parsedObj, J_CSTR_TO_BUF("address"), &jsonSubObject);

    if (bRetVal == true) {
        raw_buffer nameBuf = jstring_get(jsonSubObject);
        std::string str(nameBuf.m_str);
        LS_LOG_DEBUG("value of address %s", str.c_str());
        std::replace(str.begin(), str.end(), ' ', '+');
        g_string_append(addressData, "address=");
        g_string_append(addressData, str.c_str());
        LS_LOG_DEBUG("value of address %s", addressData->str);
        jstring_free_buffer(nameBuf);
    }

    getGeocodeData(&parsedObj, addressData);

    geoAddressInfo = GeoAddress(addressData->str);

    ret = mLBSProvider->getGeocodeImpl()->geoCode(geoAddressInfo,
                                                         bind(&LocationService::geocodingCb, this, placeholders::_1,
                                                              placeholders::_2, placeholders::_3),
                                                         FALSE,
                                                         message);
    if (ret == ERROR_NONE) {
        LSMessageRef(message);
    } else {
        errorCode = LOCATION_UNKNOWN_ERROR;
        goto EXIT;
    }

    EXIT:
    if (errorCode != LOCATION_SUCCESS) {
        LSMessageReplyError(sh, message, errorCode);
    }

    if (!jis_null(parsedObj))
        j_release(&parsedObj);

    if (addressData != NULL)
        g_string_free(addressData, TRUE);
    return true;
}


bool LocationService::getAllLocationHandlers(LSHandle *sh, LSMessage *message, void *data) {
    printMessageDetails("LUNA-API", message, sh);
    jvalue_ref handlersArrayItem = NULL;
    jvalue_ref handlersArrayItem1 = NULL;
    jvalue_ref handlersArray = NULL;
    jvalue_ref serviceObject = NULL;
    jvalue_ref parsedObj = NULL;
    bool isSubscription = false;
    bool bRetVal;
    LSError mLSError;

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_GET_ALL_LOCATION_HANDLERS, &parsedObj)) {
        return true;
    }

    /*Handle subscription case*/
    if ((isSubscribeTypeValid(sh, message, false, &isSubscription)) && isSubscription) {

        if (LSSubscriptionAdd(sh, SUBSC_GETALLLOCATIONHANDLERS, message, &mLSError) == false) {
            LS_LOG_ERROR("Failed to add getAllLocationHandlers to subscription list");
            LSErrorPrintAndFree(&mLSError);
            LSMessageReplyError(sh, message, LOCATION_UNKNOWN_ERROR);
            return true;
        }
    }

    handlersArrayItem = jobject_create();

    if (jis_null(handlersArrayItem)) {
        LSMessageReplyError(sh, message, LOCATION_OUT_OF_MEM);
        return true;
    }

    handlersArrayItem1 = jobject_create();

    if (jis_null(handlersArrayItem1)) {
        LSMessageReplyError(sh, message, LOCATION_OUT_OF_MEM);
        j_release(&handlersArrayItem);
        j_release(&parsedObj);
        return true;
    }

    handlersArray = jarray_create(NULL);;

    if (jis_null(handlersArray)) {
        LSMessageReplyError(sh, message, LOCATION_OUT_OF_MEM);
        j_release(&handlersArrayItem1);
        j_release(&handlersArrayItem);
        j_release(&parsedObj);
        return true;
    }

    serviceObject = jobject_create();

    if (jis_null(serviceObject)) {
        LSMessageReplyError(sh, message, LOCATION_OUT_OF_MEM);
        j_release(&handlersArrayItem);
        j_release(&handlersArrayItem1);
        j_release(&handlersArray);
        j_release(&parsedObj);
        return true;
    }

    jobject_put(handlersArrayItem, J_CSTR_TO_JVAL("name"), jstring_create(GPS));
    jobject_put(handlersArrayItem, J_CSTR_TO_JVAL("state"), jboolean_create(getHandlerStatus(GPS)));
    jarray_append(handlersArray, handlersArrayItem);
    jobject_put(handlersArrayItem1, J_CSTR_TO_JVAL("name"), jstring_create(NETWORK));
    jobject_put(handlersArrayItem1, J_CSTR_TO_JVAL("state"), jboolean_create(getHandlerStatus(NETWORK)));
    jarray_append(handlersArray, handlersArrayItem1);
    /*Form the json object*/
    location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
    jobject_put(serviceObject, J_CSTR_TO_JVAL("handlers"), handlersArray);

    LS_LOG_DEBUG("Inside LSMessageReply");
    bRetVal = LSMessageReply(sh, message, jvalue_tostring_simple(serviceObject), &mLSError);

    if (bRetVal == false)
        LSErrorPrintAndFree(&mLSError);

    j_release(&serviceObject);
    j_release(&parsedObj);
    return true;
}

bool LocationService::getGpsStatus(LSHandle *sh, LSMessage *message, void *data) {
    printMessageDetails("LUNA-API", message, sh);
    LSError mLSError;
    bool isSubscription = false;
    jvalue_ref serviceObject = NULL;
    jvalue_ref parsedObj = NULL;
    int state = 0;
    LSErrorInit(&mLSError);

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_GET_GPS_STATUS, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in getGpsStatus");
        return true;
    }

    serviceObject = jobject_create();

    if (jis_null(serviceObject)) {
        LSMessageReplyError(sh, message, LOCATION_OUT_OF_MEM);
        return true;
    }

    state = mGPSProvider->getGPSStatus();

    if (state == GPS_STATUS_AVAILABLE)
        mCachedGpsEngineStatus = true;
    else
        mCachedGpsEngineStatus = false;

    if ((isSubscribeTypeValid(sh, message, false, &isSubscription)) && isSubscription) {
        LS_LOG_ERROR("Subscription call\n");

        if (LSSubscriptionAdd(sh, SUBSC_GPS_ENGINE_STATUS, message, &mLSError) == false) {
            LSErrorPrint(&mLSError, stderr);
            LSMessageReplyError(sh, message, LOCATION_UNKNOWN_ERROR);
            goto DONE_GET_GPS_STATUS;
        }

        location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
        jobject_put(serviceObject, J_CSTR_TO_JVAL("state"), jboolean_create(mCachedGpsEngineStatus));

        if (!LSMessageReply(sh, message, jvalue_tostring_simple(serviceObject), &mLSError))
            LSErrorPrint(&mLSError, stderr);

    } else {
        LS_LOG_DEBUG("Not subscription call\n");

        location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
        jobject_put(serviceObject, J_CSTR_TO_JVAL("state"), jboolean_create(mCachedGpsEngineStatus));

        if (!LSMessageReply(sh, message, jvalue_tostring_simple(serviceObject), &mLSError))
            LSErrorPrint(&mLSError, stderr);
    }

    DONE_GET_GPS_STATUS:

    if (!jis_null(parsedObj))
        j_release(&parsedObj);

    if (!jis_null(serviceObject))
        j_release(&serviceObject);

    return true;
}

bool LocationService::getState(LSHandle *sh, LSMessage *message, void *data) {
    printMessageDetails("LUNA-API", message, sh);
    int state;
    LPAppHandle lpHandle = NULL;
    bool isSubscription = false;
    char *handler = nullptr;
    LSError mLSError;
    jvalue_ref serviceObject = NULL;

    //Read Handler from json
    jvalue_ref handlerObj = NULL;
    jvalue_ref stateObj = NULL;
    LSErrorInit(&mLSError);
    jvalue_ref parsedObj = NULL;

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_GET_STATE, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in getState");
        return true;
    }

    serviceObject = jobject_create();

    if (jis_null(serviceObject)) {
        LSMessageReplyError(sh, message, LOCATION_OUT_OF_MEM);
        return true;
    }

    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("Handler"), &handlerObj)) {
        raw_buffer handler_buf = jstring_get(handlerObj);
        handler = g_strdup(handler_buf.m_str);
        jstring_free_buffer(handler_buf);
    }

    //Read state from json
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("state"), &stateObj)) {
        jnumber_get_i32(stateObj, &state);
    }

    if (LPAppGetHandle(LOCATION_SERVICE_NAME, &lpHandle) == LP_ERR_NONE) {
        int lpret;
        lpret = LPAppCopyValueInt(lpHandle, handler, &state);
        LPAppFreeHandle(lpHandle, true);

        if ((isSubscribeTypeValid(sh, message, false, &isSubscription)) && isSubscription) {
            //Add to subscription list with handler+method name
            char subscription_key[MAX_GETSTATE_PARAM];
            strncpy(subscription_key, handler, sizeof(handler));
            subscription_key[sizeof(handler)+1] = '\0';
            LS_LOG_INFO("handler_key=%s len =%zu", subscription_key, (strlen(SUBSC_GET_STATE_KEY) + strlen(handler)));

            if (LSSubscriptionAdd(sh, strncat(subscription_key, SUBSC_GET_STATE_KEY, strlen(SUBSC_GET_STATE_KEY)), message, &mLSError) == false) {
                LS_LOG_ERROR("Failed to add to subscription list");
                LSErrorPrintAndFree(&mLSError);
                LSMessageReplyError(sh, message, LOCATION_UNKNOWN_ERROR);
                goto EXIT;
            }
            LS_LOG_DEBUG("handler_key=%s", subscription_key);
        }

        if (lpret != LP_ERR_NONE) {
            location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
            jobject_put(serviceObject, J_CSTR_TO_JVAL("state"), jnumber_create_i32(0));
            if (!LSMessageReply(sh, message, jvalue_tostring_simple(serviceObject), &mLSError))
                 LSErrorPrintAndFree(&mLSError);
            goto EXIT;
        }

        location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
        jobject_put(serviceObject, J_CSTR_TO_JVAL("state"), jnumber_create_i32(state));

        LS_LOG_INFO("state=%d", state);

        if (!LSMessageReply(sh, message, jvalue_tostring_simple(serviceObject), &mLSError))
            LSErrorPrint(&mLSError, stderr);
    } else {
        LS_LOG_ERROR("LPAppGetHandle is not created ");
        LSMessageReplyError(sh, message, LOCATION_UNKNOWN_ERROR);
        goto EXIT;
    }

    EXIT:
    if (!jis_null(parsedObj))
        j_release(&parsedObj);

    if (!jis_null(serviceObject))
        j_release(&serviceObject);

    g_free(handler);

    return true;
}

bool LocationService::setState(LSHandle *sh, LSMessage *message, void *data) {
    printMessageDetails("LUNA-API", message, sh);
    char *handler = NULL;
    bool state=false;
    LSError mLSError;
    LSErrorInit(&mLSError);
    LPAppHandle lpHandle = 0;
    jvalue_ref parsedObj = NULL;
    jvalue_ref serviceObject = NULL;
    jvalue_ref getAllLocationHandlersReplyObject = NULL;
    jvalue_ref handlersArrayItem = NULL;
    jvalue_ref handlersArrayItem1 = NULL;
    jvalue_ref handlersArray = NULL;
    LocationErrorCode errorCode = LOCATION_SUCCESS;

    jvalue_ref handlerObj = NULL;
    jvalue_ref stateObj = NULL;

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_SET_STATE, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in setState");
        return true;
    }

    serviceObject = jobject_create();

    if (jis_null(serviceObject)) {
        errorCode = LOCATION_OUT_OF_MEM;
        goto EXIT;
    }

    getAllLocationHandlersReplyObject = jobject_create();

    if (jis_null(getAllLocationHandlersReplyObject)) {
        errorCode = LOCATION_OUT_OF_MEM;
        goto EXIT;
    }

    handlersArrayItem = jobject_create();

    if (jis_null(handlersArrayItem)) {
        errorCode = LOCATION_OUT_OF_MEM;
        goto EXIT;
    }

    handlersArrayItem1 = jobject_create();

    if (jis_null(handlersArrayItem1)) {
        errorCode = LOCATION_OUT_OF_MEM;
        j_release(&handlersArrayItem);
        goto EXIT;
    }

    handlersArray = jarray_create(NULL);

    if (jis_null(handlersArray)) {
        errorCode = LOCATION_OUT_OF_MEM;
        j_release(&handlersArrayItem1);
        j_release(&handlersArrayItem);
        goto EXIT;
    }

    //Read Handler from json
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("Handler"), &handlerObj)) {
        raw_buffer handler_buf = jstring_get(handlerObj);
        handler = g_strdup(handler_buf.m_str);
        jstring_free_buffer(handler_buf);
    }

    if (!handler)
        goto EXIT;
    //Read state from json
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("state"), &stateObj)) {
        jboolean_get(stateObj, &state);
    }

    if (LPAppGetHandle(LOCATION_SERVICE_NAME, &lpHandle) == LP_ERR_NONE) {
        LS_LOG_INFO("Writing handler state");

        LPAppSetValueInt(lpHandle, handler, state);
        LPAppFreeHandle(lpHandle, true);

        location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
        bool bRetVal = LSMessageReply(sh, message, jvalue_tostring_simple(serviceObject), &mLSError);
        char subscription_key[MAX_GETSTATE_PARAM];

        LSERROR_CHECK_AND_PRINT(bRetVal, mLSError);

        strncpy(subscription_key, handler, sizeof(handler));
	    subscription_key[sizeof(handler)+1] = '\0';
        strncat(subscription_key, SUBSC_GET_STATE_KEY, strlen(SUBSC_GET_STATE_KEY));

        if ((strcmp(handler, GPS) == 0) && mGpsStatus != state) {
            mGpsStatus = state;
            jobject_put(serviceObject, J_CSTR_TO_JVAL("state"), jnumber_create_i32(mGpsStatus));
            bRetVal = LSSubscriptionReply(mServiceHandle, subscription_key, jvalue_tostring_simple(serviceObject),
                                          &mLSError);

            if (bRetVal == false)
                LSErrorPrintAndFree(&mLSError);

            //*getAllLocationHandlers subscription reply
            jobject_put(handlersArrayItem, J_CSTR_TO_JVAL("name"), jstring_create(GPS));
            jobject_put(handlersArrayItem, J_CSTR_TO_JVAL("state"), jboolean_create(mGpsStatus));
            jarray_append(handlersArray, handlersArrayItem);
            jobject_put(handlersArrayItem1, J_CSTR_TO_JVAL("name"), jstring_create(NETWORK));
            jobject_put(handlersArrayItem1, J_CSTR_TO_JVAL("state"), jboolean_create(mNwStatus));
            jarray_append(handlersArray, handlersArrayItem1);

            /*Form the json object*/
            location_util_form_json_reply(getAllLocationHandlersReplyObject, true, LOCATION_SUCCESS);
            jobject_put(getAllLocationHandlersReplyObject, J_CSTR_TO_JVAL("handlers"), handlersArray);

            bRetVal = LSSubscriptionReply(mServiceHandle,
                                          SUBSC_GETALLLOCATIONHANDLERS,
                                          jvalue_tostring_simple(getAllLocationHandlersReplyObject),
                                          &mLSError);

            if (bRetVal == false)
                LSErrorPrintAndFree(&mLSError);

            if (state == false) {
                mGPSProvider->processRequest(PositionRequest("GPS", STOP_GPS_CMD));
                replyErrorToGpsNwReq(HANDLER_GPS);
            }
        } else if ((strcmp(handler, NETWORK) == 0) && mNwStatus != state) {
            mNwStatus = state;
            jobject_put(serviceObject, J_CSTR_TO_JVAL("state"), jnumber_create_i32(mNwStatus));
            bRetVal = LSSubscriptionReply(mServiceHandle, subscription_key, jvalue_tostring_simple(serviceObject),
                                          &mLSError);

            if (bRetVal == false)
                LSErrorPrintAndFree(&mLSError);

            //*getAllLocationHandlers subscription reply
            jobject_put(handlersArrayItem, J_CSTR_TO_JVAL("name"), jstring_create(GPS));
            jobject_put(handlersArrayItem, J_CSTR_TO_JVAL("state"), jboolean_create(mGpsStatus));
            jarray_append(handlersArray, handlersArrayItem);
            jobject_put(handlersArrayItem1, J_CSTR_TO_JVAL("name"), jstring_create(NETWORK));
            jobject_put(handlersArrayItem1, J_CSTR_TO_JVAL("state"), jboolean_create(mNwStatus));
            jarray_append(handlersArray, handlersArrayItem1);

            /*Form the json object*/
            location_util_form_json_reply(getAllLocationHandlersReplyObject, true, LOCATION_SUCCESS);
            jobject_put(getAllLocationHandlersReplyObject, J_CSTR_TO_JVAL("handlers"), handlersArray);

            bRetVal = LSSubscriptionReply(mServiceHandle,
                                          SUBSC_GETALLLOCATIONHANDLERS,
                                          jvalue_tostring_simple(getAllLocationHandlersReplyObject),
                                          &mLSError);

            if (bRetVal == false)
                LSErrorPrintAndFree(&mLSError);

            if (state == false) {
                mNetworkProvider->processRequest(PositionRequest("network", STOP_POSITION_CMD));
                replyErrorToGpsNwReq(HANDLER_NETWORK);
            }
        } else { //memleak
            j_release(&handlersArrayItem1);
            j_release(&handlersArrayItem);
            j_release(&handlersArray);
        }

        if (mNwStatus == false && mGpsStatus == false) {
            //Force wakelock release
            if (m_lifeCycleMonitor) {
                m_lifeCycleMonitor->setWakeLock(false, true);
            }

            replyErrorToGpsNwReq(HANDLER_HYBRID);
        }
    } else {
        LS_LOG_DEBUG("LPAppGetHandle is not created");
        j_release(&handlersArrayItem1);
        j_release(&handlersArrayItem);
        j_release(&handlersArray);
        errorCode = LOCATION_UNKNOWN_ERROR;
    }

    EXIT:

    if (errorCode != LOCATION_SUCCESS)
        LSMessageReplyError(sh, message, errorCode);

    if (!jis_null(getAllLocationHandlersReplyObject))
        j_release(&getAllLocationHandlersReplyObject);

    if (!jis_null(parsedObj))
        j_release(&parsedObj);

    if (!jis_null(serviceObject))
        j_release(&serviceObject);

    if (handler != NULL)
        g_free(handler);

    return true;
}

bool LocationService::setGPSParameters(LSHandle *sh, LSMessage *message, void *data) {
    printMessageDetails("LUNA-API", message, sh);
    jvalue_ref parsedObj = NULL;
    jvalue_ref serviceObject = NULL;
    char *cmdStr = NULL;
    bool bRetVal;
    int ret;

    LSError mLSError;
    LSErrorInit(&mLSError);

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_SET_GPS_PARAMETERS, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in setGPSParameters");
        return true;
    }

    PositionRequest request("GPS", SET_GPS_PARAMETERS_CMD);
    request.setRequestParams((const char*)jvalue_tostring_simple(parsedObj));

    ret = mGPSProvider->processRequest(request);
    if (ERROR_NONE != ret) {
        LS_LOG_ERROR("Error in %s", cmdStr);
        LSMessageReplyError(sh, message, LOCATION_INVALID_INPUT);
        j_release(&parsedObj);
        return true;
    }

    serviceObject = jobject_create();

    if (jis_null(serviceObject)) {
        LSMessageReplyError(sh, message, LOCATION_OUT_OF_MEM);
        j_release(&parsedObj);
        return true;
    }

    jobject_put(serviceObject, J_CSTR_TO_JVAL("returnValue"), jboolean_create(true));
    jobject_put(serviceObject, J_CSTR_TO_JVAL("errorCode"), jnumber_create_i32(LOCATION_SUCCESS));

    bRetVal = LSMessageReply(sh, message, jvalue_tostring_simple(serviceObject), &mLSError);

    if (bRetVal == false)
        LSErrorPrintAndFree(&mLSError);

    j_release(&serviceObject);
    j_release(&parsedObj);
    return true;
}

bool LocationService::exitLocation(LSHandle *sh, LSMessage *message, void *data) {
    printMessageDetails("LUNA-API", message, sh);
    finalize_mock_location();
    stopGpsEngine();
    g_main_loop_unref(mMainLoop);
    mMainLoop = NULL;
    LSError error;
    bool retVal;

    pbnjson::JValue reply = pbnjson::Object();
    if (reply.isNull())
        return false;

    reply.put("returnValue", true);

    LSError lserror;
    LSErrorInit(&lserror);

    if((retVal=LSMessageReply(sh, message, reply.stringify().c_str(), &lserror))==false){
	    LSErrorPrintAndFree(&error);
    }
    return true;
}

bool LocationService::stopGPS(LSHandle *sh, LSMessage *message, void *data) {

    printMessageDetails("LUNA-API", message, sh);
    int ret;
    LSError mLSError;
    jvalue_ref serviceObject = NULL;
    jvalue_ref parsedObj = NULL;

    LSErrorInit(&mLSError);

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_STOP_GPS, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in stopGPS");
        return true;
    }

    serviceObject = jobject_create();

    if (jis_null(serviceObject)) {
        LS_LOG_ERROR("Failed to allocate memory to serviceObject");
        j_release(&parsedObj);
        LSMessageReplyError(sh, message, LOCATION_OUT_OF_MEM);
        return true;
    }

    ret = mGPSProvider->processRequest(PositionRequest("GPS", STOP_GPS_CMD));

    if (ERROR_NONE != ret) {
        LS_LOG_ERROR("GPS force stop failed");
        LSMessageReplyError(sh, message, LOCATION_UNKNOWN_ERROR);

    } else {
        location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
        bool bRetVal = LSMessageReply(sh, message, jvalue_tostring_simple(serviceObject), &mLSError);

        if (bRetVal == false)
            LSErrorPrintAndFree(&mLSError);
    }

    j_release(&serviceObject);
    j_release(&parsedObj);

    return true;
}

bool LocationService::sendExtraCommand(LSHandle *sh, LSMessage *message, void *data) {
    printMessageDetails("LUNA-API", message, sh);
    jvalue_ref serviceObject = NULL;
    char *command = nullptr;
    bool bRetVal;
    ErrorCodes ret;
    LSError mLSError;
    LSErrorInit(&mLSError);
    jvalue_ref commandObj = NULL;
    jvalue_ref parsedObj = NULL;

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_SEND_EXTRA_COMMAND, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in sendExtraCommand");
        return true;
    }

    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("command"), &commandObj)) {
        raw_buffer handler_buf = jstring_get(commandObj);
        command = g_strdup(handler_buf.m_str);
        jstring_free_buffer(handler_buf);
    }

    if (!command)
        return true;

    if (strcmp(command, "enable_suspend_blocker") == 0) {
        m_enableSuspendBlocker = true;
    } else if (strcmp(command, "disable_suspend_blocker") == 0) {
        m_enableSuspendBlocker = false;
        if (m_lifeCycleMonitor) {
            m_lifeCycleMonitor->setWakeLock(false, true);
        }
    } else {
        LS_LOG_INFO("command = %s", command);

        PositionRequest request("GPS", SEND_GPS_EXTRA_CMD);
        request.setRequestParams(command);

        ret = mGPSProvider->processRequest(request);


        if (ret == ERROR_NOT_AVAILABLE) {
            LS_LOG_ERROR("Error in %s", command);
            LSMessageReplyError(sh, message, LOCATION_INVALID_INPUT);
            j_release(&parsedObj);
            g_free(command);
            return true;
        }

    }

    serviceObject = jobject_create();

    if (jis_null(serviceObject)) {
        LS_LOG_ERROR("Failed to allocate memory to serviceObject");
        LSMessageReplyError(sh, message, LOCATION_OUT_OF_MEM);
        j_release(&parsedObj);
        g_free(command);
        return true;
    }

    location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
    bRetVal = LSMessageReply(sh, message, jvalue_tostring_simple(serviceObject), &mLSError);

    if (bRetVal == false)
        LSErrorPrintAndFree(&mLSError);

    j_release(&parsedObj);
    j_release(&serviceObject);
    g_free(command);

    return true;
}

bool LocationService::getLocationHandlerDetails(LSHandle *sh, LSMessage *message, void *data) {
    printMessageDetails("LUNA-API", message, sh);
    char *handler = NULL;
    int powerRequirement = 0;
    bool bRetVal;
    LSError mLSError;
    jvalue_ref parsedObj = NULL;
    jvalue_ref jsonSubObject = NULL;
    jvalue_ref serviceObject = NULL;

    LSErrorInit(&mLSError);

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_GET_LOCATION_HANDLER_DETAILS, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in getLocationHandlerDetails");
        return true;
    }

    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("Handler"), &jsonSubObject)) {
        raw_buffer nameBuf = jstring_get(jsonSubObject);
        handler = g_strdup(nameBuf.m_str);
        jstring_free_buffer(nameBuf);
    }
    else {
        LS_LOG_ERROR("Invalid param:Handler");
        LSMessageReplyError(sh, message, LOCATION_INVALID_INPUT);
        j_release(&parsedObj);
        return true;
    }

    if (NULL == handler) {
        LS_LOG_ERROR("ParamInput is NULL");
        LSMessageReplyError(sh, message, LOCATION_INVALID_INPUT);
        j_release(&parsedObj);
        return true;
    }

    serviceObject = jobject_create();

    if (jis_null(serviceObject)) {
        LS_LOG_ERROR("Failed to allocate memory to serviceObject");
        LSMessageReplyError(sh, message, LOCATION_OUT_OF_MEM);
        g_free(handler);
        j_release(&parsedObj);
        return true;
    }

    if (strcmp(handler, "gps") == 0) {
        powerRequirement = 1;
        LS_LOG_INFO("getHandlerStatus(GPS)");

        jobject_put(serviceObject, J_CSTR_TO_JVAL("returnValue"), jboolean_create(true));
        jobject_put(serviceObject, J_CSTR_TO_JVAL("errorCode"), jnumber_create_i32(LOCATION_SUCCESS));
        jobject_put(serviceObject, J_CSTR_TO_JVAL("accuracy"), jnumber_create_i32(ACCURACY_LEVEL_HIGH));
        jobject_put(serviceObject, J_CSTR_TO_JVAL("powerRequirement"), jnumber_create_i32(powerRequirement));
        jobject_put(serviceObject, J_CSTR_TO_JVAL("requiresNetwork"), jboolean_create(false));
        jobject_put(serviceObject, J_CSTR_TO_JVAL("requiresCell"), jboolean_create(false));
        jobject_put(serviceObject, J_CSTR_TO_JVAL("monetaryCost"), jboolean_create(false));

        bRetVal = LSMessageReply(sh, message, jvalue_tostring_simple(serviceObject), &mLSError);

        if (bRetVal == false) {
            LSErrorPrintAndFree(&mLSError);
        }
    } else if (strcmp(handler, "network") == 0) {
        LS_LOG_INFO("getHandlerStatus(network)");
        powerRequirement = 3;

        jobject_put(serviceObject, J_CSTR_TO_JVAL("returnValue"), jboolean_create(true));
        jobject_put(serviceObject, J_CSTR_TO_JVAL("errorCode"), jnumber_create_i32(LOCATION_SUCCESS));
        jobject_put(serviceObject, J_CSTR_TO_JVAL("accuracy"), jnumber_create_i32(ACCURACY_LEVEL_LOW));
        jobject_put(serviceObject, J_CSTR_TO_JVAL("powerRequirement"), jnumber_create_i32(powerRequirement));
        jobject_put(serviceObject, J_CSTR_TO_JVAL("requiresNetwork"), jboolean_create(true));
        jobject_put(serviceObject, J_CSTR_TO_JVAL("requiresCell"), jboolean_create(true));
        jobject_put(serviceObject, J_CSTR_TO_JVAL("monetaryCost"), jboolean_create(true));

        bRetVal = LSMessageReply(sh, message, jvalue_tostring_simple(serviceObject), &mLSError);

        if (bRetVal == false) {
            LSErrorPrintAndFree(&mLSError);
        }
    } else {
        LS_LOG_ERROR("LPAppGetHandle is not created");
        LSMessageReplyError(sh, message, LOCATION_INVALID_INPUT);
    }

    j_release(&parsedObj);
    j_release(&serviceObject);
    g_free(handler);
    return true;
}

bool LocationService::getGpsSatelliteData(LSHandle *sh, LSMessage *message, void *data) {
    printMessageDetails("LUNA-API", message, sh);
    int ret;
    bool bRetVal;
    LSError mLSError;
    LocationErrorCode errorCode = LOCATION_SUCCESS;
    jvalue_ref parsedObj = NULL;

    LSErrorInit(&mLSError);

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_GET_GPS_SATELLITE_DATA, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in getGpsSatelliteData");
        return true;
    }

    if (!getHandlerStatus(GPS)) {
        LS_LOG_ERROR("getGpsSatelliteData GPS OFF");
        errorCode = LOCATION_LOCATION_OFF;
        goto EXIT;
    }

    bRetVal = LSSubscriptionAdd(sh, SUBSC_GET_GPS_SATELLITE_DATA, message, &mLSError);

    if (bRetVal == false) {
        LSErrorPrintAndFree(&mLSError);
        errorCode = LOCATION_SUCCESS;
        goto EXIT;
    }

    ret = ERROR_NONE;

    if (suspended_state == false) {
        PositionRequest request("GPS", SATELITTE_CMD);

        ret = mGPSProvider->processRequest(request);
    }

    if (ret == ERROR_NOT_AVAILABLE)
    {
        errorCode = LOCATION_GPS_NYX_SOURCE_UNAVAILABLE;
        goto EXIT;
    }
    else if (ret != ERROR_NONE && ret != ERROR_DUPLICATE_REQUEST) {
        errorCode = LOCATION_UNKNOWN_ERROR;
        goto EXIT;
    }

    /* SUSPEND BLOCKER
    if (m_enableSuspendBlocker)
        m_lifeCycleMonitor->setWakeLock(true);
    */

    EXIT:
    if (!jis_null(parsedObj))
        j_release(&parsedObj);

    if (errorCode != LOCATION_SUCCESS)
        LSMessageReplyError(sh, message, errorCode);

    return true;
}

bool LocationService::getTimeToFirstFix(LSHandle *sh, LSMessage *message, void *data) {
    printMessageDetails("LUNA-API", message, sh);
    bool bRetVal;
    long long TTFF = 0;
    LSError mLSError;
    jvalue_ref parsedObj = NULL;
    jvalue_ref serviceObject = NULL;
    LSErrorInit(&mLSError);

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_GET_TIME_TO_FIRST_FIX, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in getTimeToFirstFix");
        return true;
    }

    serviceObject = jobject_create();

    if (jis_null(serviceObject)) {
        LSMessageReplyError(sh, message, LOCATION_OUT_OF_MEM);
        return true;
    }



    TTFF = mGPSProvider->getTimeToFirstFixValue();


    location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
    jobject_put(serviceObject, J_CSTR_TO_JVAL("TTFF"),jnumber_create_i64(TTFF));

    LS_LOG_DEBUG("get time to first fix %lli reply payload %s", TTFF, jvalue_tostring_simple(serviceObject));

    bRetVal = LSMessageReply(sh, message, jvalue_tostring_simple(serviceObject), &mLSError);

    if (bRetVal == false) {
        LSErrorPrintAndFree(&mLSError);
    }

    j_release(&parsedObj);
    j_release(&serviceObject);

    return true;
}

bool LocationService::addGeofenceArea(LSHandle *sh, LSMessage *message, void *data) {
    printMessageDetails("LUNA-API", message, sh);
    gdouble latitude = 0.0;
    gdouble longitude = 0.0;
    gdouble radius = 0.0;
    int geofenceId = -1;
    int count = 0;
    int errorCode = LOCATION_SUCCESS;
    char strGeofenceId[32];
    LSError mLSError;
    jvalue_ref parsedObj = NULL;
    jvalue_ref jsonSubObject = NULL;
    ErrorCodes ret;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(MIN_GEOFENCE_RANGE, MAX_GEOFENCE_RANGE);

    LS_LOG_DEBUG("=======addGeofenceArea=======\n");

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_ADD_GEOFENCE_AREA, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in addGeofenceArea\n");
        return true;
    }
    PositionRequest request("GPS", ADD_GEOFENCE_CMD);

    //GPS  off
    if (getHandlerStatus(GPS) == HANDLER_STATE_DISABLED) {
        errorCode = LOCATION_LOCATION_OFF;
        goto EXIT;
    }

    /* Extract latitude from message */
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("latitude"), &jsonSubObject))
        jnumber_get_f64(jsonSubObject, &latitude);

    /* Extract longitude from message */
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("longitude"), &jsonSubObject))
        jnumber_get_f64(jsonSubObject, &longitude);

    /* Extract radius from message */
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("radius"), &jsonSubObject))
        jnumber_get_f64(jsonSubObject, &radius);
    request.setGeofenceCoordinates(longitude, latitude, radius);

     /* Generate random Geofence Id which is not used by previous request */
    while (count < (MAX_GEOFENCE_RANGE - MIN_GEOFENCE_RANGE)) {
	geofenceId = dis(gen);
        LS_LOG_DEBUG("=======addGeofenceArea ID genrated %d=======\n", geofenceId);

        if (!is_geofenceId_used[geofenceId - MIN_GEOFENCE_RANGE]) {
            is_geofenceId_used[geofenceId - MIN_GEOFENCE_RANGE] = true;
            break;
        }

        count++;
    }

    if (count == (MAX_GEOFENCE_ID - MIN_GEOFENCE_RANGE)) {
        LS_LOG_DEBUG("MAX_GEOFENCE_ID reached\n");
        errorCode = LOCATION_GEOFENCE_TOO_MANY_GEOFENCE;
        goto EXIT;
    }

    request.setGeofenceID(geofenceId);

    ret = mGPSProvider->processRequest(request);

    LS_LOG_DEBUG("addGeofenceArea : value of ret is: %d", ret);

    if ( ERROR_NONE != ret ) {
        errorCode = LOCATION_UNKNOWN_ERROR;
    } else {
        LSErrorInit(&mLSError);
        snprintf(strGeofenceId, sizeof(strGeofenceId), "%d%s", geofenceId, SUBSC_GEOFENCE_ADD_AREA_KEY);

        if (LSSubscriptionAdd(sh, strGeofenceId, message, &mLSError)) {
            g_hash_table_insert(htPseudoGeofence,
                                strdup(LSMessageGetUniqueToken(message)),
                                GINT_TO_POINTER(geofenceId));
        } else {
            LSErrorPrintAndFree(&mLSError);
            errorCode = LOCATION_UNKNOWN_ERROR;
        }
    }

EXIT:
    if (errorCode != LOCATION_SUCCESS)
        LSMessageReplyError(sh, message, errorCode);

    if (!jis_null(parsedObj))
        j_release(&parsedObj);

    return true;
}

bool LocationService::removeGeofenceArea(LSHandle *sh, LSMessage *message, void *data) {
    int geofenceId = -1;
    int errorCode = LOCATION_SUCCESS;
    char strGeofenceId[32];
    LSError mLSError;
    jvalue_ref parsedObj = NULL;
    jvalue_ref jsonSubObject = NULL;
    ErrorCodes ret;

    printMessageDetails("LUNA-API", message, sh);

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_REMOVE_GEOFENCE_AREA, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in removeGeofenceArea\n");
        return true;
    }

    PositionRequest request("GPS", REMOVE_GEOFENCE_CMD);
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("geofenceid"), &jsonSubObject))
        jnumber_get_i32(jsonSubObject, &geofenceId);

    snprintf(strGeofenceId, sizeof(strGeofenceId), "%d%s", geofenceId, SUBSC_GEOFENCE_ADD_AREA_KEY);

    if (isSubscListFilled(message, strGeofenceId, false) == false) {
        errorCode = LOCATION_GEOFENCE_ID_UNKNOWN;
        goto EXIT;
    }

    request.setGeofenceID(geofenceId);

    ret = mGPSProvider->processRequest(request);

    LS_LOG_DEBUG("removeGeofenceArea :value of ret is: %d", ret);

    if ( ERROR_NONE != ret ) {
        errorCode = LOCATION_UNKNOWN_ERROR;
    } else {
        LSErrorInit(&mLSError);
        snprintf(strGeofenceId, sizeof(strGeofenceId), "%d%s", geofenceId, SUBSC_GEOFENCE_REMOVE_AREA_KEY);

        if (!LSSubscriptionAdd(sh, strGeofenceId, message, &mLSError)) {
            LSErrorPrintAndFree(&mLSError);
            errorCode = LOCATION_UNKNOWN_ERROR;
        }
    }

EXIT:
    if (errorCode != LOCATION_SUCCESS)
        LSMessageReplyError(sh, message, errorCode);

    if (!jis_null(parsedObj))
        j_release(&parsedObj);

    return true;
}

bool LocationService::pauseGeofence(LSHandle *sh, LSMessage *message, void *data) {
    int geofence_id = -1;
    int errorReply = LOCATION_SUCCESS;
    jvalue_ref parsedObj = NULL;
    char str_geofence_id[32];

    jvalue_ref jsonObject = NULL;

    LS_LOG_DEBUG("=======pauseGeofence======\n");

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_PAUSE_GEOFENCE_AREA, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in removeGeofenceArea\n");
        return true;
    }

    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("geofenceid"), &jsonObject))
        jnumber_get_i32(jsonObject, &geofence_id);

    snprintf(str_geofence_id, sizeof(str_geofence_id), "%d%s", geofence_id, SUBSC_GEOFENCE_ADD_AREA_KEY);
    if (isSubscListFilled(message, str_geofence_id, false) == false) {
        errorReply = LOCATION_GEOFENCE_ID_UNKNOWN;
        goto EXIT;
    }

    EXIT:

    if (errorReply != LOCATION_SUCCESS)
        LSMessageReplyError(sh, message, errorReply);

    if (!jis_null(parsedObj))
        j_release(&parsedObj);

    return true;
}

bool LocationService::resumeGeofence(LSHandle *sh, LSMessage *message, void *data) {
    int geofenceid = -1;

    int errorCode = LOCATION_SUCCESS;
    char str_geofenceid[32];
    LSError mLSError;
    jvalue_ref parsedObj = NULL;
    jvalue_ref jsonSubObject = NULL;

    LS_LOG_DEBUG("=======resumeGeofence======\n");

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_RESUME_GEOFENCE_AREA, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in resumeGeofenceArea\n");
        return true;
    }

    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("geofenceid"), &jsonSubObject))
        jnumber_get_i32(jsonSubObject, &geofenceid);

    snprintf(str_geofenceid, sizeof(str_geofenceid), "%d%s", geofenceid, SUBSC_GEOFENCE_ADD_AREA_KEY);
    if (isSubscListFilled(message, str_geofenceid, false) == false) {
        errorCode = LOCATION_GEOFENCE_ID_UNKNOWN;
        goto EXIT;
    }


    LSErrorInit(&mLSError);
    snprintf(str_geofenceid, sizeof(str_geofenceid), "%d%s", geofenceid, SUBSC_GEOFENCE_RESUME_AREA_KEY);

    if (!LSSubscriptionAdd(sh, str_geofenceid, message, &mLSError)) {
        LSErrorPrintAndFree(&mLSError);
        errorCode = LOCATION_UNKNOWN_ERROR;
    }


    EXIT:
    if (errorCode != LOCATION_SUCCESS)
        LSMessageReplyError(sh, message, errorCode);

    if (!jis_null(parsedObj))
        j_release(&parsedObj);

    return true;
}

bool LocationService::getLocationParameter( _Location* loc, jvalue_ref parsedObj ) {
    jvalue_ref param;
    jvalue_ref object = jobject_get(parsedObj, J_CSTR_TO_BUF("location") );
    if ( jis_null(object) ) {
        return false;
    }

    memset( loc, 0, sizeof(_Location) );

    loc->flag = 0;

    /* lon & lat are mendatory */
    param = jobject_get(object, J_CSTR_TO_BUF("longitude") );
    if ( jis_null(param) ) {
        return false;
    }
    jnumber_get_f64(param, &loc->longitude );

    param = jobject_get(object, J_CSTR_TO_BUF("latitude") );
    if ( jis_null(param) ) {
        return false;
    }
    jnumber_get_f64(param, &loc->latitude );

    /* optional */
    if (jobject_get_exists(object, J_CSTR_TO_BUF("altitude"), &param)) {
        jnumber_get_f64(param, &loc->altitude );
        loc->flag |= LOCATION_ALTITUDE_BIT;
    }

    if (jobject_get_exists(object, J_CSTR_TO_BUF("speed"), &param)) {
        jnumber_get_f64(param, &loc->speed );
        loc->flag |= LOCATION_SPEED_BIT;
    }

    if (jobject_get_exists(object, J_CSTR_TO_BUF("horizAccuracy"), &param)) {
        jnumber_get_f64(param, &loc->horizontalAccuracy );
        loc->flag |= LOCATION_HORIZONTAL_ACCURACY_BIT;
    }

    if (jobject_get_exists(object, J_CSTR_TO_BUF("verAccuracy"), &param)) {
        jnumber_get_f64(param, &loc->verticalAccuracy );
        loc->flag |= LOCATION_VERTICAL_ACCURACY_BIT;
    }

    if (jobject_get_exists(object, J_CSTR_TO_BUF("heading"), &param)) {
        jnumber_get_f64(param, &loc->heading );
        loc->flag |= LOCATION_HEADING_BIT;
    }

    if (jobject_get_exists(object, J_CSTR_TO_BUF("timestamp"), &param)) {
        jnumber_get_i64(param, &loc->timestamp );
        loc->flag |= LOCATION_TIMESTAMP_BIT;
    }

    return true;
}

bool LocationService::setMockLocationState(LSHandle *sh, LSMessage *message, void *data, int state ) {
    jvalue_ref parsedObj;
    MOCK_CALL_LOG;
    if (!LSMessageValidateSchemaReplyOnError( sh, message,
            STRICT_SCHEMA(
                PROPS_1(
                    ENUM_PROP(name,string,"gps","network")
                ) REQUIRED_1(name)
            ),
        &parsedObj)) {
        LS_LOG_ERROR( MOCK_TAG "Schema Error in %s\n", __func__);
    }
    else {
        LocationErrorCode e;
        bool locSubsStarted = false;
        int ret;
        jvalue_ref object  = jobject_get( parsedObj, J_CSTR_TO_BUF("name") );
        if ( jis_null(object) ) {
            e = LOCATION_OUT_OF_MEM;
        } else {
            raw_buffer name = jstring_get(object);
            if (isSubscListFilled(NULL, SUBSC_GET_LOC_UPDATES_GPS_KEY, false)) {
                locSubsStarted = true;
                ret = mGPSProvider->processRequest(PositionRequest("GPS", STOP_GPS_CMD));
                if (ERROR_NONE != ret) LS_LOG_ERROR("GPS force stop failed");
            }
            e = set_mock_location_state( name.m_str, state );
            if (locSubsStarted) {
                ret = mGPSProvider->processRequest(PositionRequest("GPS", POSITION_CMD));
                if (ERROR_NONE != ret) LS_LOG_ERROR("GPS force start failed");
            }

            jstring_free_buffer(name);
        }
        j_release(&parsedObj);

        LSMessageReplyError( sh, message, e );
    }
    return true;
}

bool LocationService::enableMockLocation(LSHandle *sh, LSMessage *message, void *data) {
    return setMockLocationState( sh, message, data, 1 );
}

bool LocationService::disableMockLocation(LSHandle *sh, LSMessage *message, void *data) {
    return setMockLocationState( sh, message, data, 0 );
}

bool LocationService::setMockLocation(LSHandle *sh, LSMessage *message, void *data) {
    struct _Location loc;
    jvalue_ref parsedObj = NULL;

    MOCK_CALL_LOG;

    /* get parameters */
    if (!LSMessageValidateSchemaReplyOnError( sh, message,
            STRICT_SCHEMA(
                PROPS_2(
                    ENUM_PROP(name,string,"gps","network"),
                    OBJECT(location,
                        OBJSCHEMA_1( PROP(longitude,number) "," PROP(latitude,number) )
                    )
                ) REQUIRED_2(name,location)
            ),
        &parsedObj)) {
        LS_LOG_ERROR( MOCK_TAG "Schema Error in %s\n", __func__);
        return true;
    }

    while ( getLocationParameter( &loc, parsedObj ) ) {
        jvalue_ref object  = jobject_get( parsedObj, J_CSTR_TO_BUF("name") );
        if ( jis_null(object) ) {
            break;
        }

        raw_buffer name = jstring_get(object);
        LocationErrorCode e = set_mock_location( name.m_str, &loc );
        jstring_free_buffer(name);
        LSMessageReplyError( sh, message, e );
        break;
    }
    if (!jis_null(parsedObj))
        j_release(&parsedObj);
    return true;
}

bool LocationService::getLocationUpdates(LSHandle *sh, LSMessage *message, void *data) {
    printMessageDetails("LUNA-API", message, sh);
    int errorCode = LOCATION_SUCCESS;
    LSError mLSError;
    jvalue_ref parsedObj = NULL;
    jvalue_ref serviceObj = NULL;
    char key[KEY_MAX] = {0x00};
    char *handlerName = NULL;
    bool bRetVal = false;
    unsigned char startedHandlers = 0;
    int32_t responseTime = 0;
    int sel_handler = LocationService::GETLOC_UPDATE_INVALID;
    int minInterval = 0;
    int minDistance = 0;
    int handlertype = -1;
    bool bWakeLock = false;

    LSErrorInit(&mLSError);

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCEHMA_GET_LOCATION_UPDATES, &parsedObj)) {
        LS_LOG_ERROR("Schema error in getLocationUpdates");
        return true;
    }

    if ((getHandlerStatus(GPS) == HANDLER_STATE_DISABLED) &&
        (getHandlerStatus(NETWORK) == HANDLER_STATE_DISABLED)) {
        LS_LOG_ERROR("Both handler are OFF\n");
        errorCode = LOCATION_LOCATION_OFF;
        goto EXIT;
    }

    /* Parse minimumInterval in milliseconds */
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("minimumInterval"), &serviceObj)) {
        jnumber_get_i32(serviceObj, &minInterval);
        LS_LOG_DEBUG("minimumInterval %d", minInterval);
    }

    /* Parse minimumDistance in meters*/
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("minimumDistance"), &serviceObj)) {
        jnumber_get_i32(serviceObj, &minDistance);
        LS_LOG_DEBUG("minimumDistance %d", minDistance);
    }
    /* Parse Handler name */
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("Handler"), &serviceObj)) {
        raw_buffer nameBuf = jstring_get(serviceObj);
        handlerName = g_strdup(nameBuf.m_str);
        jstring_free_buffer(nameBuf);
    } else {
        handlerName = g_strdup(HYBRID);
        handlertype = HANDLER_HYBRID;
    }

    LS_LOG_INFO("handlerName %s", handlerName);

    if (((strcmp(handlerName, GPS) == 0) && (getHandlerStatus(GPS) == false)) ||
        ((strcmp(handlerName, NETWORK) == 0) && (getHandlerStatus(NETWORK) == false))) {
        errorCode = LOCATION_LOCATION_OFF;
        goto EXIT;
    }

    sel_handler = getHandlerVal(handlerName);


    if (sel_handler == LocationService::GETLOC_UPDATE_INVALID) {
        errorCode = LOCATION_INVALID_INPUT;
        goto EXIT;
    }

    /* Parse responseTime in seconds */
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("responseTimeout"), &serviceObj)) {
        jnumber_get_i32(serviceObj, &responseTime);
        LS_LOG_DEBUG("responseTime in seconds %d", responseTime);
    }

    /* Paser wakelock */
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("wakelock"), &serviceObj)) {
        jboolean_get(serviceObj, &bWakeLock);
    }

    if (enableHandlers(sel_handler, key, &startedHandlers) || (sel_handler == LocationService::GETLOC_UPDATE_PASSIVE)) {
        struct timeval tv;
        gettimeofday(&tv, (struct timezone *) NULL);
        long long reqTime = tv.tv_sec * 1000LL + tv.tv_usec / 1000;
        TimerData *timerdata = NULL;
        guint timerID = 0;

        timerdata = new(std::nothrow) TimerData(message, sh, startedHandlers, key);
        if (timerdata == NULL) {
            LS_LOG_ERROR("timerdata null Out of memory");
            errorCode = LOCATION_OUT_OF_MEM;
            goto EXIT;
        }

        LS_LOG_INFO("timerdata %p", timerdata);
        if (responseTime != 0) {
            timerID = g_timeout_add_seconds(responseTime, &TimerCallbackLocationUpdate, timerdata);
            LS_LOG_INFO("timerID %d started for responseTime %d", timerID, responseTime);
        }

        LocationUpdateRequest *locUpdateReq = new(std::nothrow) LocationUpdateRequest(message,
                                                                                      reqTime,
                                                                                      timerID,
                                                                                      timerdata,
                                                                                      LocationService::INVALID_LAT,
                                                                                      LocationService::INVALID_LONG,
                                                                                      handlertype);

        if (locUpdateReq == NULL) {
            LS_LOG_ERROR("locUpdateReq null Out of memory");
            errorCode = LOCATION_OUT_OF_MEM;
            goto EXIT;
        }

        boost::shared_ptr<LocationUpdateRequest> req(locUpdateReq);
        m_locUpdate_req_table[message] = req;

        /*Add to subsctiption list*/
        bRetVal = LSSubscriptionAdd(sh, key, message, &mLSError);

        if (bRetVal == false) {
            LSErrorPrintAndFree(&mLSError);
            errorCode = LOCATION_UNKNOWN_ERROR;
            goto EXIT;
        }

        // SUSPEND BLOCKER
        if (m_enableSuspendBlocker && bWakeLock) {
            if ((startedHandlers & HANDLER_GPS_BIT) ||
                (startedHandlers & HANDLER_NETWORK_BIT)) {

                if (m_lifeCycleMonitor)
                    m_lifeCycleMonitor->setWakeLock(true);
            }
        }

        /********Request gps Handler*****************************/
        if (startedHandlers & HANDLER_GPS_BIT) {
            if (suspended_state == false) {
                PositionRequest request("GPS", POSITION_CMD);
                int requestError = mGPSProvider->processRequest(request);
                /* Check for mock location*/
                struct _mock_location_provider* mlp = get_mock_location_provider(GPS);
                if (mlp) {
                    if (mlp->flag & MOCKLOC_FLAG_ENABLED) {
                        LS_LOG_DEBUG("Mock Location is Enabled\n");
                        goto EXIT;
                    }
                }

                if (requestError == LOCATION_LOCATION_OFF)
                {
                    errorCode = LOCATION_GPS_NYX_SOURCE_UNAVAILABLE;
                    goto EXIT;
                }
            }
        }
        /********Request NWHandler*****************************/
        if (startedHandlers & HANDLER_NETWORK_BIT) {
            errorCode = mNetworkProvider->processRequest(PositionRequest("network", POSITION_CMD));
            goto EXIT;
        }

    } else {
        LS_LOG_ERROR("Unable to start handlers");
        errorCode = LOCATION_DATA_CONNECTION_OFF;
        goto EXIT;
    }


    EXIT:
    if (!jis_null(parsedObj))
        j_release(&parsedObj);

    if (errorCode != LOCATION_SUCCESS) {
        LSMessageReplyError(sh, message, errorCode);
    }

    if (handlerName != NULL)
        g_free(handlerName);

    return true;
}

bool LocationService::getCachedPosition(LSHandle *sh, LSMessage *message, void *data) {
    printMessageDetails("LUNA-API", message, sh);
    LSError lsError;
    int errorCode = LOCATION_SUCCESS;
    Position pos;
    Accuracy acc;
    Position gpspos;
    Accuracy gpsacc ;
    Position nwpos;
    Accuracy nwacc;
    jvalue_ref parsedObj = NULL;
    jvalue_ref handlerObj = NULL;
    jvalue_ref serviceObj = NULL;
    jvalue_ref maximumAgeObj = NULL;
    char *handlerName = NULL;
    int maximumAge = 0;
    bool bRetVal;

    LSErrorInit(&lsError);
    LS_LOG_INFO("=======getCachedPosition======= payload %s", LSMessageGetPayload(message));

    if (!LSMessageValidateSchemaReplyOnError(sh, message, JSCHEMA_GET_CACHED_POSITION, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in getCachedPosition");
        return true;
    }

    serviceObj = jobject_create();

    if (jis_null(serviceObj)) {
        errorCode = LOCATION_OUT_OF_MEM;
        goto EXIT;
    }

    memset(&pos, 0x00, sizeof(Position));
    memset(&gpspos, 0x00, sizeof(Position));
    memset(&nwpos, 0x00, sizeof(Position));
    memset(&gpsacc, 0x00, sizeof(Accuracy));
    memset(&nwacc, 0x00, sizeof(Accuracy));
    memset(&acc, 0x00, sizeof(Accuracy));
    /*Parse Handler name*/
    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("Handler"), &handlerObj)) {
        raw_buffer nameBuf = jstring_get(handlerObj);
        handlerName = g_strdup(nameBuf.m_str);
        LS_LOG_INFO("handlerName %s", handlerName);
        jstring_free_buffer(nameBuf);
    } else {
        handlerName = g_strdup(HYBRID);
    }
    //check in cache first if maximumAge is -1

    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("maximumAge"), &maximumAgeObj)) {
        jnumber_get_i32(maximumAgeObj, &maximumAge);
    }

    LS_LOG_INFO("maximumAge= %d", maximumAge);

    if (strcmp(handlerName, GPS) == 0) {
        mGPSProvider->getLastPosition(&pos,
                                      &acc);

    } else if (strcmp(handlerName, NETWORK) == 0) {
        mNetworkProvider->getLastPosition(&pos,
                                          &acc);

    } else if (strcmp(handlerName, HYBRID) == 0 || strcmp(handlerName, PASSIVE) == 0) {
        mGPSProvider->getLastPosition(&gpspos,
                                      &gpsacc);

        mNetworkProvider->getLastPosition(&nwpos,
                                          &nwacc);

        pos = comparePositionTimeStamps(gpspos,
                                        nwpos,
                                        gpsacc,
                                        nwacc,
                                        &acc);
    }

    if (pos.latitude == 0 || pos.longitude == 0) {
        errorCode = LOCATION_POS_NOT_AVAILABLE;
    } else {
        if (maximumAge > 0) {
            struct timeval tv;
            gettimeofday(&tv, (struct timezone *) NULL);
            long long currentTime = tv.tv_sec * 1000LL + tv.tv_usec / 1000;

            if (maximumAge < (currentTime - pos.timestamp)) {
                errorCode = LOCATION_POS_NOT_AVAILABLE;
            }
        }
    }

    EXIT:
    if (errorCode != LOCATION_SUCCESS) {
        LS_LOG_INFO("getCachedPosition reply error %d", errorCode);
        LSMessageReplyError(sh, message, errorCode);
    } else {
        //Read according to accuracy value
        location_util_form_json_reply(serviceObj, true, LOCATION_SUCCESS);
        location_util_add_pos_json(serviceObj, &pos);
        location_util_add_acc_json(serviceObj, &acc);

        bRetVal = LSMessageReply(sh, message, jvalue_tostring_simple(serviceObj), &lsError);

        if (bRetVal == false)
            LSErrorPrintAndFree(&lsError);
    }

    if (!jis_null(serviceObj)) {
        LS_LOG_INFO("getCachedPosition reply payload %s", jvalue_tostring_simple(serviceObj));
        j_release(&serviceObj);
    }

    if (!jis_null(parsedObj))
        j_release(&parsedObj);

    if (handlerName != NULL)
        g_free(handlerName);

    return true;
}

Position LocationService::comparePositionTimeStamps(Position pos1,
                                                    Position pos2,
                                                    Accuracy acc1,
                                                    Accuracy acc2,
                                                    Accuracy *retAcc) {
    Position pos;
    memset(&pos, 0x00, sizeof(Position));

    if (pos1.timestamp >= pos2.timestamp) {
        /*timestamp are same then compare accuracy*/
        if (pos1.timestamp == pos2.timestamp) {
            (acc1.horizAccuracy < acc2.horizAccuracy) ? (*retAcc = acc1, pos = pos1) : (*retAcc = acc2, pos = pos2);
        } else {
            *retAcc = acc1;
            pos = pos1;
        }
    } else {
        *retAcc = acc2;
        pos = pos2;
    }

    return pos;
}

int LocationService::enableHandlers(int sel_handler, char *key, unsigned char *startedHandlers) {
    bool gpsHandlerStatus;
    bool nwHandlerStatus;

    switch (sel_handler) {
        case LocationService::GETLOC_UPDATE_GPS:
            if (enableGpsHandler(startedHandlers)) {
                strncpy(key, SUBSC_GET_LOC_UPDATES_GPS_KEY,sizeof(SUBSC_GET_LOC_UPDATES_GPS_KEY));
		        key[sizeof(SUBSC_GET_LOC_UPDATES_GPS_KEY)+1] = '\0';
            }

            break;

        case LocationService::GETLOC_UPDATE_NW:
            if (enableNwHandler(startedHandlers)) {
                strncpy(key, SUBSC_GET_LOC_UPDATES_NW_KEY,sizeof(SUBSC_GET_LOC_UPDATES_NW_KEY));
		        key[sizeof(SUBSC_GET_LOC_UPDATES_NW_KEY)+1] = '\0';
            }

            break;

        case LocationService::GETLOC_UPDATE_GPS_NW:
            gpsHandlerStatus = enableGpsHandler(startedHandlers);
            nwHandlerStatus = enableNwHandler(startedHandlers);

            if (gpsHandlerStatus || nwHandlerStatus) {
                strncpy(key, SUBSC_GET_LOC_UPDATES_HYBRID_KEY,sizeof(SUBSC_GET_LOC_UPDATES_HYBRID_KEY));
		        key[sizeof(SUBSC_GET_LOC_UPDATES_HYBRID_KEY)+1] = '\0';
            }

            break;

        case LocationService::GETLOC_UPDATE_PASSIVE:
            strncpy(key, SUBSC_GET_LOC_UPDATES_PASSIVE_KEY, sizeof(SUBSC_GET_LOC_UPDATES_PASSIVE_KEY));
	        key[sizeof(SUBSC_GET_LOC_UPDATES_PASSIVE_KEY)+1] = '\0';
            break;

        default:
            LS_LOG_DEBUG("LocationService invalid case");
            break;
    }

    if (*startedHandlers == HANDLER_STATE_DISABLED) {
        LS_LOG_INFO("All handlers disabled");
        return false;
    }

    return true;
}


bool LocationService::enableNwHandler(unsigned char *startedHandlers) {

    if (getHandlerStatus(NETWORK)) {
        LS_LOG_DEBUG("network is on in Settings");

    if (getConnectionManagerState()) {
        *startedHandlers |= HANDLER_NETWORK_BIT;
    }

    LS_LOG_INFO("isInternetConnectionAvailable %d TelephonyState %d",
                getConnectionManagerState(),
                getTelephonyState());

    if (*startedHandlers == HANDLER_STATE_DISABLED)
        return false;

    }

    return true;
}

bool LocationService::enableGpsHandler(unsigned char *startedHandlers) {
    LS_LOG_DEBUG("===enableGpsHandler====");

    if (getHandlerStatus(GPS)) {
        LS_LOG_DEBUG("gps is on in Settings");
        *startedHandlers |= HANDLER_GPS_BIT;
    } else {
        return false;
    }

    return true;
}

int LocationService::getHandlerVal(char *handlerName) {
    int sel_handler = LocationService::GETLOC_UPDATE_INVALID;

    if (handlerName == NULL)
        sel_handler = LocationService::GETLOC_UPDATE_INVALID;
    else if (strcmp(handlerName, GPS) == 0)
        sel_handler = LocationService::GETLOC_UPDATE_GPS;
    else if (strcmp(handlerName, NETWORK) == 0)
        sel_handler = LocationService::GETLOC_UPDATE_NW;
    else if (strcmp(handlerName, HYBRID) == 0)
        sel_handler = LocationService::GETLOC_UPDATE_GPS_NW;
    else if (strcmp(handlerName, PASSIVE) == 0)
        sel_handler = LocationService::GETLOC_UPDATE_PASSIVE;

    return sel_handler;

}

/********************* Callback functions**********************************************************************
 ********************   Called from handlers********************************************************************
 **************************************************************************************************************/

void LocationService::geocodingCb(GeoLocation& location, int errCode, LSMessage *message) {
    LS_LOG_DEBUG("geocodingCb");
    getInstance()->geocodingReply(location.toString().c_str(), errCode, message);
}

void LocationService::reverseGeocodingCb(GeoAddress& address, int errCode, LSMessage *message) {
    LS_LOG_DEBUG("reverseGeocodingCb");
    getInstance()->geocodingReply(address.toString().c_str(), errCode, message);
}

void LocationService::geofenceAddCb(int32_t geofenceId, int32_t status, gpointer userData)
{
    LS_LOG_INFO("geofenceAddCb called back ...");
    getInstance()->geofence_add_reply(geofenceId, status);
}

void LocationService::geofenceRemoveCb(int32_t geofenceId, int32_t status, gpointer userData)
{
    LS_LOG_DEBUG("geofenceRemoveCb called back ...");
    getInstance()->geofence_remove_reply(geofenceId, status);
}

void LocationService::geofencePauseCb(int32_t geofenceId, int32_t status, gpointer userData)
{
    LS_LOG_DEBUG("geofencePauseCb called back ...");
    getInstance()->geofence_pause_reply(geofenceId, status);
}

void LocationService::geofenceResumeCb(int32_t geofenceId, int32_t status, gpointer userData)
{
    LS_LOG_DEBUG("geofenceResumeCb called back ...");
    getInstance()->geofence_resume_reply(geofenceId, status);
}

void LocationService::geofenceBreachCb(int32_t geofenceId, int32_t status, int64_t timestamp, double latitude, double longitude, gpointer userData)
{
    LS_LOG_DEBUG("geofenceBreachCb called back ...");
    getInstance()->geofence_breach_reply(geofenceId, status, timestamp, latitude, longitude);
}

void LocationService::geofenceStatusCb (int32_t status, Position *lastPosition, Accuracy *accuracy, gpointer userData)
{
    LS_LOG_DEBUG("geofenceStatusCb called back ...");
    // Not required as it is only intialization status
}



void LocationService::getLocationUpdateCb(GeoLocation& location, ErrorCodes errCode, HandlerTypes type) {
    LS_LOG_DEBUG("getLocationUpdateCb %d getConnectionManagerState() %d",errCode, getConnectionManagerState());

    if (errCode == ERROR_NETWORK_ERROR && getConnectionManagerState())
         return;

    Position pos;
    Accuracy acc = {};
    pos.timestamp = location.getTimeStamp();
    pos.altitude = location.getAltitude();
    pos.latitude = location.getLatitude();
    pos.longitude = location.getLongitude();
    pos.direction = location.getDirection();
    pos.climb = location.getClimb();
    pos.speed = location.getSpeed();
    acc.horizAccuracy = location.getHorizontalAccuracy();
    acc.vertAccuracy = location.getVerticalAccuracy();

    if ((HANDLER_NETWORK == type)&&(ERROR_NETWORK_ERROR == errCode))
        getLocationUpdate_reply(NULL, NULL, errCode, type);
    else
        getLocationUpdate_reply(&pos, &acc, errCode, type);
}

/********************************Response to Application layer*********************************************************************/

void LocationService::getNmeaDataCb(long long timestamp, char *data, int length) {
    const char *retString = NULL;
    jvalue_ref serviceObject = NULL;
    GSimpleAsyncResult *asyncRes = NULL;
    NmeaData *nmeaData = NULL;

    LS_LOG_DEBUG("[DEBUG] getNmeaDataCb called\n");

    serviceObject = jobject_create();
    if (jis_null(serviceObject)) {
        LS_LOG_ERROR("Out of memory\n");
        retString = LSMessageGetErrorReply(LOCATION_OUT_OF_MEM);
        goto EXIT;
    }

    LS_LOG_DEBUG("timestamp=%lld\n", timestamp);
    LS_LOG_DEBUG("data=%s\n", data);

    location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
    jobject_put(serviceObject, J_CSTR_TO_JVAL("timestamp"), jnumber_create_i64(timestamp));

    if (data != NULL)
        jobject_put(serviceObject, J_CSTR_TO_JVAL("nmea"), jstring_create(data));

    retString = jvalue_tostring_simple(serviceObject);

    EXIT:

    asyncRes = g_simple_async_result_new(NULL, sendNmeaData, this, NULL);
    nmeaData = g_slice_new0(NmeaData);
    nmeaData->nmeaString = g_strdup(retString);
    nmeaData->lsHandle = mServiceHandle;

    g_simple_async_result_set_op_res_gpointer(asyncRes, nmeaData, nmeaDataUnref);
    g_simple_async_result_complete_in_idle(asyncRes);
    g_object_unref(asyncRes);

    if (!jis_null(serviceObject))
        j_release(&serviceObject);
}

void LocationService::sendSatelliteData(GObject *source, GAsyncResult *res, gpointer userdata) {
    printf_info("enter sendSatelliteData\n");

    LocationService *locService = (LocationService *) userdata;
    SatelliteData *satelliteData = (SatelliteData *) g_simple_async_result_get_op_res_gpointer(
            G_SIMPLE_ASYNC_RESULT(res));

    locService->LSSubscriptionNonSubscriptionRespond(satelliteData->lsHandle,
                                                     SUBSC_GET_GPS_SATELLITE_DATA,
                                                     satelliteData->satelliteString);
}

void LocationService::satelliteDataUnref(gpointer data) {
    printf_info("enter satelliteDataUnref\n");

    SatelliteData *satelliteData = (SatelliteData *) data;
    g_free(satelliteData->satelliteString);
    g_slice_free(SatelliteData, data);
}

void LocationService::getGpsSatelliteDataCb(Satellite *sat) {
    guint num_satellite_used_count = 0;
    const char *retString = NULL;
    jvalue_ref serviceObject = NULL;
    jvalue_ref serviceArray = NULL;
    jvalue_ref visibleSatelliteItem = NULL;
    GSimpleAsyncResult *asyncRes = NULL;
    SatelliteData *satelliteData = NULL;

    LS_LOG_DEBUG("[DEBUG] getGpsSatelliteDataCb called, reply to application\n");

    if (sat == NULL) {
        LS_LOG_ERROR("satellite data NULL");
        return;
    }

    serviceArray = jarray_create(NULL);
    if (jis_null(serviceArray)) {
        retString = LSMessageGetErrorReply(LOCATION_OUT_OF_MEM);
        goto EXIT;
    }

    serviceObject = jobject_create();
    if (jis_null(serviceObject)) {
        j_release(&serviceArray);
        retString = LSMessageGetErrorReply(LOCATION_OUT_OF_MEM);
        goto EXIT;
    }

    location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
    jobject_put(serviceObject, J_CSTR_TO_JVAL("visibleSatellites"), jnumber_create_i32(sat->visible_satellites_count));

    while (num_satellite_used_count < sat->visible_satellites_count) {
        visibleSatelliteItem = jobject_create();

        if (!jis_null(visibleSatelliteItem)) {
            LS_LOG_DEBUG(" Service Agent value of %llf num_satellite_used_count%d ",
                         sat->sat_used[num_satellite_used_count].azimuth, num_satellite_used_count);
            jobject_put(visibleSatelliteItem, J_CSTR_TO_JVAL("index"), jnumber_create_i32(num_satellite_used_count));
            jobject_put(visibleSatelliteItem, J_CSTR_TO_JVAL("azimuth"),
                        jnumber_create_f64(sat->sat_used[num_satellite_used_count].azimuth));
            jobject_put(visibleSatelliteItem, J_CSTR_TO_JVAL("elevation"),
                        jnumber_create_f64(sat->sat_used[num_satellite_used_count].elevation));
            jobject_put(visibleSatelliteItem, J_CSTR_TO_JVAL("prn"),
                        jnumber_create_i32(sat->sat_used[num_satellite_used_count].prn));
            jobject_put(visibleSatelliteItem, J_CSTR_TO_JVAL("snr"),
                        jnumber_create_f64(sat->sat_used[num_satellite_used_count].snr));
            jobject_put(visibleSatelliteItem, J_CSTR_TO_JVAL("hasAlmanac"),
                        jboolean_create(sat->sat_used[num_satellite_used_count].hasalmanac));
            jobject_put(visibleSatelliteItem, J_CSTR_TO_JVAL("hasEphemeris"),
                        jboolean_create(sat->sat_used[num_satellite_used_count].hasephemeris));
            jobject_put(visibleSatelliteItem, J_CSTR_TO_JVAL("usedInFix"),
                        jboolean_create(sat->sat_used[num_satellite_used_count].used));

            jarray_append(serviceArray, visibleSatelliteItem);
            num_satellite_used_count++;
        }

    }

    jobject_put(serviceObject, J_CSTR_TO_JVAL("satellites"), serviceArray);
    retString = jvalue_tostring_simple(serviceObject);

    EXIT:
    asyncRes = g_simple_async_result_new(NULL, sendSatelliteData, this, NULL);
    satelliteData = g_slice_new0(SatelliteData);
    satelliteData->satelliteString = g_strdup(retString);
    satelliteData->lsHandle = mServiceHandle;

    g_simple_async_result_set_op_res_gpointer(asyncRes, satelliteData, satelliteDataUnref);
    g_simple_async_result_complete_in_idle(asyncRes);
    g_object_unref(asyncRes);

    if (!jis_null(serviceObject))
        j_release(&serviceObject);
}

void LocationService::sendGPSStatus(GObject *source, GAsyncResult *res, gpointer userdata) {
    LSError mLSError;
    LSErrorInit(&mLSError);

    printf_info("enter sendGPSStatus\n");


    GPSStatusData *statusData = (GPSStatusData *) g_simple_async_result_get_op_res_gpointer(G_SIMPLE_ASYNC_RESULT(res));

    bool bRetVal = LSSubscriptionReply(statusData->lsHandle,
                                       SUBSC_GPS_ENGINE_STATUS,
                                       statusData->statusString,
                                       &mLSError);

    if (bRetVal == false) {
        LSErrorPrint(&mLSError, stderr);
        LSErrorFree(&mLSError);
    }
}

void LocationService::gpsStatusUnref(gpointer data) {
    printf_info("enter gpsStatusUnref\n");

    GPSStatusData *statusData = (GPSStatusData *) data;
    g_free(statusData->statusString);
    g_slice_free(GPSStatusData, data);
}

/**
 * <Funciton >   getGpsStatus_reply

 * <Description>  Callback function called to update gps status

 * @param     LunaService handle

 * @param     LunaService message

 * @param     user data

 * @return    bool if successful return true else false
 */
void LocationService::getGpsStatusCb(int state) {
    bool isGpsEngineOn;
    GSimpleAsyncResult *asyncRes = NULL;
    GPSStatusData *gpsStatusData = NULL;

    if (state == GPS_STATUS_AVAILABLE)
        isGpsEngineOn = true;
    else
        isGpsEngineOn = false;

    mCachedGpsEngineStatus = isGpsEngineOn;
    jvalue_ref serviceObject = jobject_create();

    if (!jis_null(serviceObject)) {
        location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
        jobject_put(serviceObject, J_CSTR_TO_JVAL("state"), jboolean_create(isGpsEngineOn));

        asyncRes = g_simple_async_result_new(NULL, sendGPSStatus, this, NULL);
        gpsStatusData = g_slice_new0(GPSStatusData);
        gpsStatusData->statusString = g_strdup(jvalue_tostring_simple(serviceObject));
        gpsStatusData->lsHandle = mServiceHandle;

        g_simple_async_result_set_op_res_gpointer(asyncRes, gpsStatusData, gpsStatusUnref);
        g_simple_async_result_complete_in_idle(asyncRes);
        g_object_unref(asyncRes);
        j_release(&serviceObject);
    }

}

void LocationService::geofence_breach_reply(int32_t geofenceId, int32_t status, int64_t timestamp, double latitude, double longitude)
{
    jvalue_ref serviceObject = NULL;
    const char *retString = NULL;
    GSimpleAsyncResult *asyncResGeofence = NULL;
    GeofenceAddData *geofenceAddData = NULL;

    LS_LOG_INFO("geofence_breach_reply: id=%d, status=%d, timestamp=%lld, latitude=%f, longitude=%f\n",
                geofenceId, status, timestamp, latitude, longitude);

    serviceObject = jobject_create();

    if (!jis_null(serviceObject)) {
        location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);

        switch (status) {
            case GEOFENCE_ENTERED:
                retString = geofenceStateText[GEOFENCE_TRANSITION_ENTERED];
                break;

            case GEOFENCE_EXITED:
                retString = geofenceStateText[GEOFENCE_TRANSITION_EXITED];
                break;

            case GEOFENCE_UNCERTAIN:
                retString = geofenceStateText[GEOFENCE_TRANSITION_UNCERTAIN];
                break;

            default:
                retString = geofenceStateText[GEOFENCE_TRANSITION_UNCERTAIN];
                break;
        }

        jobject_put(serviceObject, J_CSTR_TO_JVAL("status"), jstring_create(retString));
        jobject_put(serviceObject, J_CSTR_TO_JVAL("timestamp"), jnumber_create_i64(timestamp));
        jobject_put(serviceObject, J_CSTR_TO_JVAL("latitude"), jnumber_create_f64(latitude));
        jobject_put(serviceObject, J_CSTR_TO_JVAL("longitude"), jnumber_create_f64(longitude));

        retString = jvalue_tostring_simple(serviceObject);
    } else {
        retString = LSMessageGetErrorReply(LOCATION_OUT_OF_MEM);
    }

    asyncResGeofence = g_simple_async_result_new(NULL, sendGeofenceBreachData, this, NULL);
    geofenceAddData = g_slice_new0(GeofenceAddData);
    geofenceAddData->geofenceString = g_strdup(retString);
    geofenceAddData->lsHandle = mServiceHandle;
    geofenceAddData->geofenceId = geofenceId;

    g_simple_async_result_set_op_res_gpointer(asyncResGeofence, geofenceAddData, geofenceAddDataUnref);
    g_simple_async_result_complete_in_idle(asyncResGeofence);
    g_object_unref(asyncResGeofence);

    if (!jis_null(serviceObject))
        j_release(&serviceObject);
}

void LocationService::geofence_add_reply(int32_t geofenceId, int32_t status)
{
    int errorCode = LOCATION_UNKNOWN_ERROR;
    jvalue_ref serviceObject = NULL;
    const char *retString = NULL;
    GSimpleAsyncResult *asyncResGeofence = NULL;
    GeofenceAddData *geofenceAddData = NULL;

    LS_LOG_INFO("geofence_add_reply: id=%d, status=%d\n", geofenceId, status);

    switch (status) {
        case GEOFENCE_OPERATION_SUCCESS:
            errorCode = LOCATION_SUCCESS;
            break;

        case GEOFENCE_ERROR_TOO_MANY_GEOFENCES:
            errorCode = LOCATION_GEOFENCE_TOO_MANY_GEOFENCE;
            break;

        case GEOFENCE_ERROR_ID_EXISTS:
            errorCode = LOCATION_GEOFENCE_ID_EXIST;
            break;

        case GEOFENCE_ERROR_INVALID_TRANSITION:
            errorCode = LOCATION_GEOFENCE_INVALID_TRANSITION;
            break;

        case GEOFENCE_ERROR_GENERIC:
            errorCode = LOCATION_UNKNOWN_ERROR;
            break;

        default:
            errorCode = LOCATION_UNKNOWN_ERROR;
            break;
    }

    if (LOCATION_SUCCESS != errorCode) {
        is_geofenceId_used[geofenceId - MIN_GEOFENCE_RANGE] = false;

        retString = LSMessageGetErrorReply(errorCode);
    } else {
        serviceObject = jobject_create();

        if (!jis_null(serviceObject)) {
            location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
            jobject_put(serviceObject, J_CSTR_TO_JVAL("status"), jstring_create(geofenceStateText[GEOFENCE_ADD_SUCCESS]));
            jobject_put(serviceObject, J_CSTR_TO_JVAL("geofenceid"), jnumber_create_i32(geofenceId));
            retString = jvalue_tostring_simple(serviceObject);
        } else {
            retString = LSMessageGetErrorReply(LOCATION_OUT_OF_MEM);
        }
    }

    asyncResGeofence = g_simple_async_result_new(NULL, sendGeofenceAddData, this, NULL);
    geofenceAddData = g_slice_new0(GeofenceAddData);
    geofenceAddData->geofenceString = g_strdup(retString);
    geofenceAddData->lsHandle = mServiceHandle;
    geofenceAddData->geofenceId = geofenceId;

    g_simple_async_result_set_op_res_gpointer(asyncResGeofence, geofenceAddData, geofenceAddDataUnref);
    g_simple_async_result_complete_in_idle(asyncResGeofence);
    g_object_unref(asyncResGeofence);

    if (!jis_null(serviceObject))
        j_release(&serviceObject);
}

void LocationService::geofence_remove_reply(int32_t geofenceId, int32_t status)
{
    int errorCode = LOCATION_UNKNOWN_ERROR;
    jvalue_ref serviceObject = NULL;
    const char *retString = NULL;

    GSimpleAsyncResult *asyncResGeofence = NULL;
    GeofenceRemoveData *geofenceRemoveData = NULL;


    LS_LOG_INFO("geofence_remove_reply: id=%d, status=%d\n", geofenceId, status);

    switch (status) {
        case GEOFENCE_OPERATION_SUCCESS:
            errorCode = LOCATION_SUCCESS;
            break;

        case GEOFENCE_ERROR_ID_UNKNOWN:
            errorCode = LOCATION_GEOFENCE_ID_UNKNOWN;
            break;

        case GEOFENCE_ERROR_GENERIC:
            errorCode = LOCATION_UNKNOWN_ERROR;
            break;

        default:
            errorCode = LOCATION_UNKNOWN_ERROR;
            break;
    }

    if (errorCode != LOCATION_SUCCESS) {
        retString = LSMessageGetErrorReply(errorCode);
    } else {
        serviceObject = jobject_create();

        if (!jis_null(serviceObject)) {
            location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
            jobject_put(serviceObject, J_CSTR_TO_JVAL("status"), jstring_create(geofenceStateText[GEOFENCE_REMOVE_SUCCESS]));
            retString = jvalue_tostring_simple(serviceObject);
            LS_LOG_DEBUG("value of retString is: %s", retString);
        } else {
            retString = LSMessageGetErrorReply(LOCATION_OUT_OF_MEM);
            LS_LOG_DEBUG("value of retString is: %s", retString);
        }
    }

    asyncResGeofence = g_simple_async_result_new(NULL, sendGeofenceRemoveData, this, NULL);
    geofenceRemoveData = g_slice_new0(GeofenceRemoveData);
    geofenceRemoveData->geofenceString = g_strdup(retString);
    geofenceRemoveData->lsHandle = mServiceHandle;
    geofenceRemoveData->geofenceId = geofenceId;
    geofenceRemoveData->status = status;

    g_simple_async_result_set_op_res_gpointer(asyncResGeofence, geofenceRemoveData, geofenceRemoveDataUnref);
    g_simple_async_result_complete_in_idle(asyncResGeofence);
    g_object_unref(asyncResGeofence);

    if (!jis_null(serviceObject))
        j_release(&serviceObject);

}

void LocationService::geofence_pause_reply(int32_t geofence_id, int32_t status) {
    LSError mLSError;
    char str_geofenceid[32];
    int errorCode;
    jvalue_ref serviceObject = NULL;
    const char *retString = NULL;

    LS_LOG_INFO("geofence_pause_reply: id=%d, status=%d\n", geofence_id, status);

    LSErrorInit(&mLSError);

    switch (status) {
        case GEOFENCE_OPERATION_SUCCESS:
            errorCode = LOCATION_SUCCESS;
            break;

        case GEOFENCE_ERROR_ID_UNKNOWN:
            errorCode = LOCATION_GEOFENCE_ID_UNKNOWN;
            break;

        case GEOFENCE_ERROR_INVALID_TRANSITION:
            errorCode = LOCATION_GEOFENCE_INVALID_TRANSITION;
            break;

        case GEOFENCE_ERROR_GENERIC:
            errorCode = LOCATION_UNKNOWN_ERROR;
            break;

        default:
            errorCode = LOCATION_UNKNOWN_ERROR;
            break;
    }

    if (errorCode != LOCATION_SUCCESS) {
        retString = LSMessageGetErrorReply(errorCode);
    } else {
        serviceObject = jobject_create();

        if (!jis_null(serviceObject)) {
            location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
            jobject_put(serviceObject, J_CSTR_TO_JVAL("status"),
                        jstring_create(geofenceStateText[GEOFENCE_PAUSE_SUCCESS]));
            retString = jvalue_tostring_simple(serviceObject);
        } else {
            retString = LSMessageGetErrorReply(LOCATION_OUT_OF_MEM);
        }
    }

    snprintf(str_geofenceid, sizeof(str_geofenceid), "%d%s", geofence_id, SUBSC_GEOFENCE_PAUSE_AREA_KEY);

    LSSubscriptionNonSubscriptionRespond(mServiceHandle,
                                         str_geofenceid,
                                         retString);

    if (status == GEOFENCE_OPERATION_SUCCESS) {
        snprintf(str_geofenceid, sizeof(str_geofenceid), "%d%s", geofence_id, SUBSC_GEOFENCE_ADD_AREA_KEY);

        if (!LSSubscriptionReply(mServiceHandle,
                                 str_geofenceid,
                                 retString,
                                 &mLSError)) {
            LSErrorPrintAndFree(&mLSError);
        }
    }

    if (!jis_null(serviceObject))
        j_release(&serviceObject);
}

void LocationService::geofence_resume_reply(int32_t geofence_id, int32_t status) {
    LSError mLSError;
    char str_geofenceid[32];
    int errorCode;
    jvalue_ref serviceObject = NULL;
    const char *retString = NULL;

    LS_LOG_INFO("geofence_resume_reply: id=%d, status=%d\n", geofence_id, status);

    LSErrorInit(&mLSError);

    switch (status) {
        case GEOFENCE_OPERATION_SUCCESS:
            errorCode = LOCATION_SUCCESS;
            break;

        case GEOFENCE_ERROR_ID_UNKNOWN:
            errorCode = LOCATION_GEOFENCE_ID_UNKNOWN;
            break;

        case GEOFENCE_ERROR_GENERIC:
            errorCode = LOCATION_UNKNOWN_ERROR;
            break;

        default:
            errorCode = LOCATION_UNKNOWN_ERROR;
            break;
    }

    if (errorCode != LOCATION_SUCCESS) {
        retString = LSMessageGetErrorReply(errorCode);
    } else {
        serviceObject = jobject_create();

        if (!jis_null(serviceObject)) {
            location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
            jobject_put(serviceObject, J_CSTR_TO_JVAL("status"),
                        jstring_create(geofenceStateText[GEOFENCE_RESUME_SUCCESS]));
            retString = jvalue_tostring_simple(serviceObject);
        } else {
            retString = LSMessageGetErrorReply(LOCATION_OUT_OF_MEM);
        }
    }

    snprintf(str_geofenceid, sizeof(str_geofenceid), "%d%s", geofence_id, SUBSC_GEOFENCE_RESUME_AREA_KEY);

    LSSubscriptionNonSubscriptionRespond(mServiceHandle,
                                         str_geofenceid,
                                         retString);

    if (status == GEOFENCE_OPERATION_SUCCESS) {
        snprintf(str_geofenceid, sizeof(str_geofenceid), "%d%s", geofence_id, SUBSC_GEOFENCE_ADD_AREA_KEY);

        if (!LSSubscriptionReply(mServiceHandle,
                                 str_geofenceid,
                                 retString,
                                 &mLSError)) {
            LSErrorPrintAndFree(&mLSError);
        }
    }

    if (!jis_null(serviceObject))
        j_release(&serviceObject);
}

void LocationService::sendPositionData(GObject *source, GAsyncResult *res, gpointer userdata) {
    printf_info("enter sendPositionData\n");

    LocationService *locService = (LocationService *) userdata;
    PositionData *posData = (PositionData *) g_simple_async_result_get_op_res_gpointer(G_SIMPLE_ASYNC_RESULT(res));

    locService->LSSubNonSubRespondGetLocUpdateCasePubPri(&posData->pos,
                                                         &posData->acc,
                                                         posData->key1,
                                                         posData->retString1);

    locService->LSSubNonSubRespondGetLocUpdateCasePubPri(&posData->pos,
                                                         &posData->acc,
                                                         posData->key2,
                                                         posData->retString1);

    /*reply to passive provider*/
    locService->LSSubNonSubRespondGetLocUpdateCasePubPri(&posData->pos,
                                                         &posData->acc,
                                                         SUBSC_GET_LOC_UPDATES_PASSIVE_KEY,
                                                         posData->retString2);
}

void LocationService::positionDataUnref(gpointer data) {
    printf_info("enter positionDataUnref\n");

    PositionData *posData = (PositionData *) data;
    g_free(posData->key1);
    g_free(posData->key2);
    g_free(posData->retString1);
    g_free(posData->retString2);
    g_slice_free(PositionData, data);
}

void LocationService::sendNmeaData(GObject *source, GAsyncResult *res, gpointer userdata) {
    printf_info("enter sendNmeaData\n");

    LocationService *locService = (LocationService *) userdata;
    NmeaData *nmeaData = (NmeaData *) g_simple_async_result_get_op_res_gpointer(G_SIMPLE_ASYNC_RESULT(res));

    locService->LSSubscriptionNonSubscriptionRespond(nmeaData->lsHandle,
                                                     SUBSC_GPS_GET_NMEA_KEY,
                                                     nmeaData->nmeaString);
}
//=========for g_async reply================

void LocationService::geofenceAddDataUnref(gpointer data)
{
    LS_LOG_DEBUG("entered geofenceAddDataUnref\n");

    GeofenceAddData *geofenceAddData =(GeofenceAddData*)data;
    g_free(geofenceAddData->geofenceString);
    g_slice_free(GeofenceAddData, data);
}

void LocationService::geofenceRemoveDataUnref(gpointer data)
{
    LS_LOG_DEBUG("entered geofenceRemoveDataUnref\n");

    GeofenceRemoveData *geofenceRemoveData =(GeofenceRemoveData*)data;
    g_free(geofenceRemoveData->geofenceString);
    g_slice_free(GeofenceRemoveData, data);
}

void LocationService :: sendGeofenceAddData(GObject *source, GAsyncResult *res, gpointer userData)
{
    LS_LOG_DEBUG("entered sendGeofenceAddData\n");
    char strGeofenceId[32];
    LSError mLSError;
    LocationService *locService = (LocationService*)userData;
    GeofenceAddData *geofenceAddData = (GeofenceAddData*)g_simple_async_result_get_op_res_gpointer(G_SIMPLE_ASYNC_RESULT(res));

    LSErrorInit(&mLSError);
    snprintf(strGeofenceId, sizeof(strGeofenceId), "%d%s", geofenceAddData->geofenceId, SUBSC_GEOFENCE_ADD_AREA_KEY);

    if (!LSSubscriptionReply(geofenceAddData->lsHandle, strGeofenceId,
                             geofenceAddData->geofenceString,
                             &mLSError)) {
    locService->LSErrorPrintAndFree(&mLSError);
    }
}

//for remove geofence
void LocationService :: sendGeofenceRemoveData(GObject *source, GAsyncResult *res, gpointer userData)
{
    char strGeofenceId[32];
    LS_LOG_DEBUG("entered sendGeofenceRemoveData \n");

    LocationService *locService = (LocationService*)userData;
    GeofenceRemoveData *geofenceRemoveData = (GeofenceRemoveData*)g_simple_async_result_get_op_res_gpointer(G_SIMPLE_ASYNC_RESULT(res));

    snprintf(strGeofenceId, sizeof(strGeofenceId), "%d%s", geofenceRemoveData->geofenceId, SUBSC_GEOFENCE_REMOVE_AREA_KEY);

    locService->LSSubscriptionNonSubscriptionRespond(geofenceRemoveData->lsHandle,
                                         strGeofenceId,
                                         geofenceRemoveData->geofenceString);

    if (geofenceRemoveData->status== GEOFENCE_OPERATION_SUCCESS) {
        snprintf(strGeofenceId, sizeof(strGeofenceId), "%d%s", geofenceRemoveData->geofenceId, SUBSC_GEOFENCE_ADD_AREA_KEY);

        /*Reply add luna api and remove from list*/
        locService->LSSubscriptionNonSubscriptionRespond(geofenceRemoveData->lsHandle,
                                                         strGeofenceId,
                                                         geofenceRemoveData->geofenceString);

        locService->is_geofenceId_used[geofenceRemoveData->geofenceId- MIN_GEOFENCE_RANGE] = false;

        if ((locService->htPseudoGeofence) != NULL) {
            GHashTableIter iter;
            gpointer key, value;
            key = value = NULL;
            g_hash_table_iter_init(&iter, (locService->htPseudoGeofence));

            while (g_hash_table_iter_next(&iter, &key, &value)) {
                if (GPOINTER_TO_INT(value) == geofenceRemoveData->geofenceId) {
                    g_hash_table_remove((locService->htPseudoGeofence), key);
                    break;
                }
            }
        }
    }
}

//for geofence_breach
void LocationService :: sendGeofenceBreachData(GObject *source, GAsyncResult *res, gpointer userData)
{
    LS_LOG_DEBUG("enter sendGeofenceAddData\n");
    char strGeofenceId[32];
    LSError mLSError;
    LocationService *locService = (LocationService*)userData;
    GeofenceAddData *geofenceAddData = (GeofenceAddData*)g_simple_async_result_get_op_res_gpointer(G_SIMPLE_ASYNC_RESULT(res));

    LSErrorInit(&mLSError);
    snprintf(strGeofenceId, sizeof(strGeofenceId), "%d%s", geofenceAddData->geofenceId, SUBSC_GEOFENCE_ADD_AREA_KEY);

    if (!LSSubscriptionReply(geofenceAddData->lsHandle,
                             strGeofenceId,
                             geofenceAddData->geofenceString,
                             &mLSError)) {
        locService->LSErrorPrintAndFree(&mLSError);
    }
}
void LocationService::nmeaDataUnref(gpointer data) {
    printf_info("enter nmeaDataUnref\n");

    NmeaData *nmeaData = (NmeaData *) data;
    g_free(nmeaData->nmeaString);
    g_slice_free(NmeaData, data);
}

void LocationService::getLocationUpdate_reply(Position *pos, Accuracy *accuracy, int error, int type) {
    LS_LOG_INFO("getLocationUpdate_reply");
    const char *retString = NULL;
    jvalue_ref serviceObject = NULL;
    const char *key1 = NULL;
    const char *key2 = NULL;
    GSimpleAsyncResult *asyncRes = NULL;
    PositionData *posData = NULL;

    if (pos)
        LS_LOG_INFO("latitude %f longitude %f altitude %f timestamp %lld", pos->latitude,
                    pos->longitude,
                    pos->altitude,
                    pos->timestamp);

    if (accuracy)
        LS_LOG_INFO("horizAccuracy %f vertAccuracy %f", accuracy->horizAccuracy, accuracy->vertAccuracy);

    switch (type) {
        case HANDLER_GPS: {
            key1 = SUBSC_GET_LOC_UPDATES_GPS_KEY;
            key2 = SUBSC_GET_LOC_UPDATES_HYBRID_KEY;
            break;
        }
        default: {
            key1 = SUBSC_GET_LOC_UPDATES_NW_KEY;
            key2 = SUBSC_GET_LOC_UPDATES_HYBRID_KEY;
            break;
        }
    }

    serviceObject = jobject_create();

    if (jis_null(serviceObject)) {
        LS_LOG_ERROR("Out of memory");
        retString = LSMessageGetErrorReply(LOCATION_OUT_OF_MEM);
        goto EXIT;
    }

    switch (error) {
        case ERROR_NONE: {
            location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
            location_util_add_pos_json(serviceObject, pos);
            location_util_add_acc_json(serviceObject, accuracy);
            retString = jvalue_tostring_simple(serviceObject);
        }
            break;
        case ERROR_TIMEOUT: {
            retString = LSMessageGetErrorReply(LOCATION_TIME_OUT);
        }
            break;
        case ERROR_NETWORK_ERROR:
        {
            retString = LSMessageGetErrorReply(LOCATION_DATA_CONNECTION_OFF);
        }
        break;
        default: {
            retString = LSMessageGetErrorReply(LOCATION_UNKNOWN_ERROR);
        }
            break;
    }


    EXIT:
    LS_LOG_DEBUG("key1 %s key2 %s", key1, key2);
    if (HANDLER_GPS == type) {

        asyncRes = g_simple_async_result_new(NULL, sendPositionData, this, NULL);
        posData = g_slice_new0(PositionData);
        posData->pos = *pos;
        posData->acc = *accuracy;
        posData->key1 = g_strdup(key1);
        posData->key2 = g_strdup(key2);
        posData->retString1 = g_strdup(retString);
        posData->retString2 = g_strdup(jvalue_tostring_simple(serviceObject));
        g_simple_async_result_set_op_res_gpointer(asyncRes, posData, positionDataUnref);
        g_simple_async_result_complete_in_idle(asyncRes);
        g_object_unref(asyncRes);
    } else {
        LSSubNonSubRespondGetLocUpdateCasePubPri(pos,
                                                 accuracy,
                                                 key1,
                                                 retString);


        LSSubNonSubRespondGetLocUpdateCasePubPri(pos,
                                                 accuracy,
                                                 key2,
                                                 retString);


        /*reply to passive provider*/
        LSSubNonSubRespondGetLocUpdateCasePubPri(pos,
                                                 accuracy,
                                                 SUBSC_GET_LOC_UPDATES_PASSIVE_KEY,
                                                 jvalue_tostring_simple(serviceObject));

    }
    if (!jis_null(serviceObject)) {
        LS_LOG_INFO("reply payload %s", jvalue_tostring_simple(serviceObject));
        j_release(&serviceObject);
    }
}

void LocationService::geocodingReply(const char *response, int error, LSMessage *message) {
    const char *retString = NULL;
    jvalue_ref jval = NULL;
    jdomparser_ref parser = NULL;
    jvalue_ref serviceObject = NULL;
    LSError lsError;
    bool bRetVal;

    if (NULL == response) {
        retString = LSMessageGetErrorReply(LOCATION_UNKNOWN_ERROR);
        goto EXIT;
    }

    switch (error) {
        case ERROR_NOT_AVAILABLE: {
            retString = LSMessageGetErrorReply(LOCATION_UNKNOWN_ERROR);
            goto EXIT;
        }
            break;
        case ERROR_NETWORK_ERROR: {
            if (getConnectionManagerState())
                retString = LSMessageGetErrorReply(LOCATION_TIME_OUT);
            else
                retString = LSMessageGetErrorReply(LOCATION_DATA_CONNECTION_OFF);
            goto EXIT;
        }
            break;
        default: {
            LS_LOG_INFO("geocodingReply: unknown error id received \n");
            goto EXIT;
        }
            break;
        case ERROR_NONE: {
            LS_LOG_INFO("geocodingReply: successful \n");
        }
            break;
    }

    serviceObject = jobject_create();

    if (jis_null(serviceObject)) {
        retString = LSMessageGetErrorReply(LOCATION_OUT_OF_MEM);
        goto EXIT;
    }

    location_util_form_json_reply(serviceObject, true, LOCATION_SUCCESS);
    JSchemaInfo schemaInfo;
    jschema_info_init(&schemaInfo, jschema_all(), NULL, NULL);
    //create parser
    parser = jdomparser_create(&schemaInfo, 0);

    if (!parser) {
        retString = LSMessageGetErrorReply(LOCATION_OUT_OF_MEM);
        goto EXIT;
    }

    if (!jdomparser_feed(parser, response, strlen(response))) {
        retString = LSMessageGetErrorReply(LOCATION_UNKNOWN_ERROR);
        goto EXIT;
    }

    jval = jdomparser_get_result(parser);
    jobject_put(serviceObject, J_CSTR_TO_JVAL("response"), jval);
    retString = jvalue_tostring_simple(serviceObject);

    EXIT:

    bRetVal = LSMessageReply(mServiceHandle, message, retString, &lsError);

    if (bRetVal == false)
        LSErrorPrintAndFree(&lsError);

    LSMessageUnref(message);

    if (!jis_null(serviceObject))
        j_release(&serviceObject);

    if (parser != NULL)
        jdomparser_release(&parser);
}

/**
 * <Funciton >   cancelSubscription

 * <Description>  Callback function called when application cancels the subscription or Application crashed

 * @param     LunaService handle

 * @param     LunaService message

 * @param     user data
 * @return    bool if successful return true else false
 */
bool LocationService::cancelSubscription(LSHandle *sh, LSMessage *message, void *data) {
    LSError mLSError;
    const char *key = LSMessageGetMethod(message);

    printMessageDetails("cancelSubscription", message, sh);

    LSErrorInit(&mLSError);

    if (key == NULL) {
        LS_LOG_ERROR("Not a valid key");
        return true;
    }

    if ((strcmp(key, SUBSC_GEOFENCE_ADD_AREA_KEY)) == 0) {
        int geofenceid = -1;
        gpointer geofenceid_ptr = NULL;
        const char *uniquetoken = NULL;

        if (htPseudoGeofence) {
            uniquetoken = LSMessageGetUniqueToken(message);

            if (uniquetoken) {
                geofenceid_ptr = g_hash_table_lookup(htPseudoGeofence, uniquetoken);

                if (geofenceid_ptr) {
                    geofenceid = GPOINTER_TO_INT(geofenceid_ptr);

                    is_geofenceId_used[geofenceid - MIN_GEOFENCE_RANGE] = false;

                    PositionRequest request("GPS", REMOVE_GEOFENCE_CMD);
                    request.setGeofenceID(geofenceid);

                    mGPSProvider->processRequest(request);
                }
            }
        }
    } else if (key != NULL && (strcmp(key, GET_LOC_UPDATE_KEY) == 0)) {

        if (location_util_req_has_wakeup(message) && m_lifeCycleMonitor) {
            m_lifeCycleMonitor->setWakeLock(false);
        }

        getLocRequestStopSubscription(sh, message);
        LSMessageRemoveReqList(message);

    } else if (!isSubscListFilled(message, key, true)) {
            stopSubcription(sh, key);
    }

    return true;
}

/**
 * <Funciton >   getLocRequestStopSubscription

 * <Description>  Stop gps engine handling for getLocationUpdate when all gps request are empty

 * @param     LunaService handle

 * @param     LunaService message

 * @return    void
 */
void LocationService::getLocRequestStopSubscription(LSHandle *sh, LSMessage *message) {

    LS_LOG_DEBUG("getLocRequestStopSubscription");

    if (!isSubscListFilled(message, SUBSC_GET_LOC_UPDATES_NW_KEY, true) &&
        !isSubscListFilled(message, SUBSC_GET_LOC_UPDATES_HYBRID_KEY, true)) {
        LS_LOG_INFO("getLocRequestStopSubscription Stopping NW handler");
        stopSubcription(sh, SUBSC_GET_LOC_UPDATES_NW_KEY);
    }

    if (!isSubscListFilled(message, SUBSC_GET_LOC_UPDATES_GPS_KEY, true) &&
        !isSubscListFilled(message, SUBSC_GET_LOC_UPDATES_HYBRID_KEY, true)) {
        LS_LOG_INFO("getLocRequestStopSubscription Stopping GPS Engine");
        stopSubcription(sh, SUBSC_GET_LOC_UPDATES_GPS_KEY);
    }

}

bool LocationService::getHandlerStatus(const char *handler) {
    LS_LOG_INFO("getHandlerStatus handler %s", handler);

    if (strcmp(handler, GPS) == 0)
        return mGpsStatus;

    if (strcmp(handler, NETWORK) == 0)
        return mNwStatus;

    return false;
}

bool LocationService::loadHandlerStatus(const char *handler) {
    LPAppHandle lpHandle = 0;
    int state = HANDLER_STATE_DISABLED;
    LS_LOG_INFO("getHandlerStatus ");

    if (LPAppGetHandle(LOCATION_SERVICE_NAME, &lpHandle) == LP_ERR_NONE) {
        int lpret;
        lpret = LPAppCopyValueInt(lpHandle, handler, &state);
        LS_LOG_INFO("state=%d lpret = %d\n", state, lpret);
        LPAppFreeHandle(lpHandle, true);
    } else {
        LS_LOG_DEBUG("[DEBUG] handle initialization failed\n");
    }

    return (state != HANDLER_STATE_DISABLED);
}

void LocationService::stopSubcription(LSHandle *sh, const char *key) {
    if (strcmp(key, SUBSC_GPS_GET_NMEA_KEY) == 0) {
        mGPSProvider->processRequest(PositionRequest("GPS", STOP_NMEA_CMD));

    } else if (strcmp(key, SUBSC_GET_GPS_SATELLITE_DATA) == 0) {
        mGPSProvider->processRequest(PositionRequest("GPS", STOP_SATELITTE_CMD));

    } else if (strcmp(key, SUBSC_GET_LOC_UPDATES_GPS_KEY) == 0) {
        mGPSProvider->processRequest(PositionRequest("GPS", STOP_POSITION_CMD));

    } else if (strcmp(key, SUBSC_GET_LOC_UPDATES_NW_KEY) == 0) {
        mNetworkProvider->processRequest(PositionRequest("network", STOP_POSITION_CMD));

    }
    /* SUSPEND BLOCKER
    if (isLocationRequestEmpty()) {
        m_lifeCycleMonitor->setWakeLock(false);
    }
    */

}

void LocationService::stopNonSubcription(const char *key) {
    if (strcmp(key, SUBSC_GPS_GET_NMEA_KEY) == 0) {
        mGPSProvider->processRequest(PositionRequest("GPS", STOP_NMEA_CMD));

    } else if (strcmp(key, SUBSC_GET_GPS_SATELLITE_DATA) == 0) {
        mGPSProvider->processRequest(PositionRequest("GPS", STOP_SATELITTE_CMD));

    } else if (strcmp(key, SUBSC_GET_LOC_UPDATES_GPS_KEY) == 0) {
        mGPSProvider->processRequest(PositionRequest("GPS", STOP_POSITION_CMD));

    } else if (strcmp(key, SUBSC_GET_LOC_UPDATES_NW_KEY) == 0) {
        //STOP network
        mNetworkProvider->processRequest(PositionRequest("network", STOP_POSITION_CMD));

    }
    /* SUSPEND BLOCKER
    if (isLocationRequestEmpty()) {
        m_lifeCycleMonitor->setWakeLock(false);
    }
    */
}

gboolean LocationService::_TimerCallbackLocationUpdate(void *data) {
    LS_LOG_INFO("======_TimerCallbackLocationUpdate==========");
    char *retString = NULL;
    LSError lserror;
    TimerData *timerdata = (TimerData *) data;
    LSSubscriptionIter *iter = NULL;

    LSErrorInit(&lserror);
    retString = LSMessageGetErrorReply(LOCATION_TIME_OUT);

    if (timerdata == NULL) {
        LS_LOG_ERROR("TimerCallback timerdata is null");
        return false;
    }

    LSMessage *message = timerdata->GetMessage();
    LSHandle *sh = timerdata->GetHandle();
    const char *key = timerdata->getKey();

    if (message == NULL || sh == NULL || key == NULL) {
        LS_LOG_ERROR("Timerdata member is NULL");
        return false;
    }

    if (!LSMessageReply(sh, message, retString, &lserror)) {
        LSErrorPrintAndFree(&lserror);
    }

    if (!LSSubscriptionAcquire(sh, key, &iter, &lserror)) {
        LSErrorPrintAndFree(&lserror);
    } else {
        while (LSSubscriptionHasNext(iter)) {
            LSMessage *msg = LSSubscriptionNext(iter);
            if (msg == message) {
                LSSubscriptionRemove(iter);

                if (location_util_req_has_wakeup(message) && m_lifeCycleMonitor)
                    m_lifeCycleMonitor->setWakeLock(false);

                if (!LSMessageRemoveReqList(msg))
                    LS_LOG_ERROR("Message is not found in the LocationUpdateRequestPtr in timeout");
            }
        }

        LSSubscriptionRelease(iter);
    }

    getLocRequestStopSubscription(sh, message);

    return false;
}

/*Timer Implementation END */
/*Returns true if the list is filled
 *false if it empty*/
bool LocationService::isSubscListFilled(LSMessage *message, const char *key, bool cancelCase) {
    bool webosSrvcListFilled = isListFilled(mServiceHandle, message, key, cancelCase);
    LS_LOG_INFO("key %s webosSrvcListFilled %d", key, webosSrvcListFilled);

    return webosSrvcListFilled;
}

bool LocationService::isListFilled(LSHandle *sh,
                                   LSMessage *message,
                                   const char *key,
                                   bool cancelCase) {
    LSError mLSError;
    LSSubscriptionIter *iter = NULL;
    bool bRetVal;
    LSMessage *msg;
    int i = 0;

    LSErrorInit(&mLSError);
    bRetVal = LSSubscriptionAcquire(sh, key, &iter, &mLSError);

    if (bRetVal == false) {
        LSErrorPrint(&mLSError, stderr);
        LSErrorFree(&mLSError);
        return bRetVal;
    }

    if (LSSubscriptionHasNext(iter) && cancelCase) {
        msg = LSSubscriptionNext(iter);
        if (message == msg) {
            bRetVal = LSSubscriptionHasNext(iter); // checking list of own message
        } else if (msg != NULL) {
            bRetVal = true;
        }
    } else {
        bRetVal = LSSubscriptionHasNext(iter);
    }

    LSSubscriptionRelease(iter);

    if (bRetVal == false)
        LS_LOG_DEBUG("List is empty for key %s i = %d", key, i++);

    if (bRetVal == true)
        LS_LOG_DEBUG("List is not empty for key %s i = %d", key, i);

    return bRetVal;
}

void LocationService::LSSubscriptionNonSubscriptionRespond(LSHandle *sh, const char *key, const char *payload) {
    LSSubscriptionNonSubscriptionReply(sh, key, payload);
}

void LocationService::LSSubNonSubRespondGetLocUpdateCase(Position *pos,
                                                         Accuracy *acc,
                                                         LSHandle *sh,
                                                         const char *key,
                                                         const char *payload) {
    LSSubNonSubReplyLocUpdateCase(pos, acc, sh, key, payload);
}

void LocationService::LSSubscriptionNonSubscriptionReply(LSHandle *sh,
                                                         const char *key,
                                                         const char *payload) {
    LSSubscriptionIter *iter = NULL;
    bool isNonSubscibePresent = false;
    LSError error;

    LSErrorInit(&error);
    if (!LSSubscriptionAcquire(sh, key, &iter, &error)) {
        LSErrorPrintAndFree(&error);
        return;
    }

    while (LSSubscriptionHasNext(iter)) {
        LSMessage *msg = LSSubscriptionNext(iter);

        if (!LSMessageReply(sh, msg, payload, &error)) {
            LSErrorPrintAndFree(&error);
        }

        if (!LSMessageIsSubscription(msg)) {
            LSSubscriptionRemove(iter);
            LS_LOG_DEBUG("Removed Non Subscription message from list");
            isNonSubscibePresent = true;
        }
    }

    LSSubscriptionRelease(iter);

    if (isNonSubscibePresent && (isSubscListFilled(NULL, key, false) == false))
        stopNonSubcription(key);
}

void LocationService::LSSubNonSubReplyLocUpdateCase(Position *pos,
                                                    Accuracy *acc,
                                                    LSHandle *sh,
                                                    const char *key,
                                                    const char *payload) {
    LSSubscriptionIter *iter = NULL;
    LSMessage *msg = NULL;
    jvalue_ref parsedObj = NULL;
    jvalue_ref jsonSubObject = NULL;
    JSchemaInfo schemaInfo;
    int minDist, minInterval;
    bool isNonSubscibePresent = false;
    LSError error;

    LS_LOG_DEBUG("key = %s\n", key);

    LSErrorInit(&error);
    if (!LSSubscriptionAcquire(sh, key, &iter, &error)) {
        LSErrorPrintAndFree(&error);
        return;
    }

    jschema_info_init(&schemaInfo, jschema_all(), NULL, NULL);

    while (LSSubscriptionHasNext(iter)) {
        msg = LSSubscriptionNext(iter);

        //parse message to get minDistance
        if (!LSMessageIsSubscription(msg))
            isNonSubscibePresent = true;

        if (pos == NULL || acc == NULL) {
            // means error string will be returned
            LSMessageReplyLocUpdateCase(msg, sh, key, payload, iter);
        } else {
            minInterval = minDist = 0;

            parsedObj = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(msg)),
                                   DOMOPT_NOOPT, &schemaInfo);

            if (!jis_null(parsedObj)) {
                if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("minimumInterval"), &jsonSubObject))
                    jnumber_get_i32(jsonSubObject, &minInterval);

                if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("minimumDistance"), &jsonSubObject))
                    jnumber_get_i32(jsonSubObject, &minDist);

                j_release(&parsedObj);
            }

            if (meetsCriteria(msg, pos, acc, minInterval, minDist))
                LSMessageReplyLocUpdateCase(msg, sh, key, payload, iter);
        }
    }

    LSSubscriptionRelease(iter);

    // check for key sub list and gps_nw sub list*/
    if (isNonSubscibePresent) {
        if (strcmp(key, SUBSC_GET_LOC_UPDATES_HYBRID_KEY) == 0) {

            LS_LOG_DEBUG("GPS_NW_CRITERIA_KEY and GPS_CRITERIA_KEY empty");

            //check gps list empty
            if ((isSubscListFilled(msg, SUBSC_GET_LOC_UPDATES_HYBRID_KEY, false) == false) &&
                (isSubscListFilled(msg, SUBSC_GET_LOC_UPDATES_GPS_KEY, false) == false)) {
                LS_LOG_DEBUG("GPS_NW_CRITERIA_KEY and GPS_CRITERIA_KEY empty");
                stopNonSubcription(SUBSC_GET_LOC_UPDATES_GPS_KEY);
            }

            //check nw list empty and stop nw handler
            if ((isSubscListFilled(msg, SUBSC_GET_LOC_UPDATES_HYBRID_KEY, false) == false) &&
                (isSubscListFilled(msg, SUBSC_GET_LOC_UPDATES_NW_KEY, false) == false)) {
                LS_LOG_DEBUG("GPS_NW_CRITERIA_KEY and NW_CRITERIA_KEY empty");
                stopNonSubcription(SUBSC_GET_LOC_UPDATES_NW_KEY);
            }
        } else if ((isSubscListFilled(msg, key, false) == false) &&
                   (isSubscListFilled(msg, SUBSC_GET_LOC_UPDATES_HYBRID_KEY, false) == false)) {
            LS_LOG_DEBUG("key %s empty", key);
            stopNonSubcription(key);
        }
    }
}

bool LocationService::LSMessageReplyLocUpdateCase(LSMessage *msg,
                                                  LSHandle *sh,
                                                  const char *key,
                                                  const char *payload,
                                                  LSSubscriptionIter *iter) {
    LSError error;
    bool retVal;

    LSErrorInit(&error);

    if ((retVal = LSMessageReply(sh, msg, payload, &error)) == false) {
        LSErrorPrintAndFree(&error);
    }

    if (!LSMessageIsSubscription(msg)) {
        /*remove from subscription list*/
        LSSubscriptionRemove(iter);

        if (location_util_req_has_wakeup(msg) && m_lifeCycleMonitor)
            m_lifeCycleMonitor->setWakeLock(false);

        /*remove from request list*/
        if (!LSMessageRemoveReqList(msg))
            LS_LOG_ERROR("Message is not found in the LocationUpdateRequestPtr");
    } else {
        removeTimer(msg);
    }

    return retVal;
}

bool LocationService::removeTimer(LSMessage *message) {
    std::unordered_map<LSMessage *, LocationUpdateRequestPtr>::iterator it;
    unsigned long size = m_locUpdate_req_table.size();
    bool found = false;
    TimerData *timerdata;
    guint timerID;
    bool timerRemoved = false;

    LS_LOG_INFO("size = %d", size);
    if (size <= 0) {
        LS_LOG_ERROR("m_locUpdate_req_table is empty");
        return false;
    }

    it = m_locUpdate_req_table.find(message);
    //check if predicate is true for atleast one element
    if (it != m_locUpdate_req_table.end()) {
        timerID = (it->second).get()->getTimerID();

        if (timerID != 0)
            timerRemoved = g_source_remove(timerID);

        timerdata = (it->second).get()->getTimerData();
        if (timerdata != NULL) {
            delete timerdata;
            timerdata = NULL;
            (it->second).get()->setTimerData(timerdata);
        }
        LS_LOG_INFO("timerRemoved %d timerID %d", timerRemoved, timerID);
        found = true;
    }
    return found;
}


bool LocationService::LSMessageRemoveReqList(LSMessage *message) {
    std::unordered_map<LSMessage *, LocationUpdateRequestPtr>::iterator it;
    unsigned long size = m_locUpdate_req_table.size();
    bool found = false;
    TimerData *timerdata;
    guint timerID;
    bool timerRemoved  =  false;

    LS_LOG_INFO("m_locUpdate_req_list size %d", size);

    if (size <= 0) {
        LS_LOG_ERROR("m_locUpdate_req_list is empty");
        return false;
    }

    it = m_locUpdate_req_table.find(message);
    if (it != m_locUpdate_req_table.end()) {
        if ((it->second).get()->getMessage() == message) {
            timerID = (it->second).get()->getTimerID();
            if (timerID != 0)
                timerRemoved = g_source_remove(timerID);

            timerdata = (it->second).get()->getTimerData();
            LS_LOG_INFO("timerRemoved %d timerID %d", timerRemoved, timerID);
            if (timerdata != NULL) {
                delete timerdata;
                timerdata = NULL;
                (it->second).get()->setTimerData(timerdata);
            }
            m_locUpdate_req_table.erase(it);
            found = true;
        }
    }
    return found;
}

bool LocationService::meetsCriteria(LSMessage *msg,
                                    Position *pos,
                                    Accuracy *acc,
                                    int minInterval,
                                    int minDist) {
    std::unordered_map<LSMessage *, LocationUpdateRequestPtr>::iterator it;
    bool bMeetsInterval, bMeetsDistance, bMeetsCriteria;
    struct timeval tv;
    long long currentTime, elapsedTime;

    gettimeofday(&tv, (struct timezone *) NULL);
    currentTime = tv.tv_sec * 1000LL + tv.tv_usec / 1000;

    bMeetsCriteria = false;

    it = m_locUpdate_req_table.find(msg);
    if (it != m_locUpdate_req_table.end()) {
        if ((it->second).get()->getMessage() == msg) {
            if ((it->second).get()->getFirstReply()) {
                (it->second).get()->updateRequestTime(currentTime);
                (it->second).get()->updateLatAndLong(pos->latitude, pos->longitude);
                (it->second).get()->updateFirstReply(false);
                bMeetsCriteria = true;
            }
            else {
                // check if it's cached one
                if (pos->timestamp != 0) {
                    bMeetsInterval = bMeetsDistance = true;

                    elapsedTime = currentTime - (it->second).get()->getRequestTime();

                    if (minInterval > 0) {
                        if (elapsedTime <= minInterval)
                            bMeetsInterval = false;
                    }

                    if (minDist > 0) {
                        if (!(loc_geometry_calc_distance(pos->latitude,
                                                        pos->longitude,
                                                        (it->second).get()->getLatitude(),
                                                        (it->second).get()->getLongitude()) >= minDist))
                            bMeetsDistance = false;
                    }

                    if (bMeetsInterval && bMeetsDistance) {
                        if ((it->second).get()->getHandlerType() == HANDLER_HYBRID) {
                            if (elapsedTime > 60000 || acc->horizAccuracy < MINIMAL_ACCURACY)
                                bMeetsCriteria = true;
                        } else {
                            bMeetsCriteria = true;
                        }
                    }

                    if (bMeetsCriteria) {
                        (it->second).get()->updateRequestTime(currentTime);
                        (it->second).get()->updateLatAndLong(pos->latitude, pos->longitude);
                    }
                }
            }
        }
    }
    return bMeetsCriteria;
}

bool LocationService::isSubscribeTypeValid(LSHandle *sh, LSMessage *message, bool isMandatory, bool *isSubscription) {
    jvalue_ref parsedObj = NULL;
    jvalue_ref jsonSubArg = NULL;

    if (!LSMessageValidateSchemaReplyOnError(sh, message, SCHEMA_ANY, &parsedObj)) {
        LS_LOG_ERROR("Schema Error in isSubscribeTypeValid\n");
        return false;
    }


    if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("subscribe"), &jsonSubArg)) {

        if (isSubscription)
            jboolean_get(jsonSubArg, isSubscription);
    } else {

        if (isMandatory) {
            LSMessageReplyError(sh, message, LOCATION_INVALID_INPUT);
            j_release(&parsedObj);
            return false;
        }

        if (isSubscription)
            *isSubscription = false;
    }

    j_release(&parsedObj);
    return true;
}

void LocationService::printMessageDetails(const char *usage, LSMessage *msg, LSHandle *sh) {
    char log[256];
    const char *service_name = LSHandleGetName(sh);

    if (!msg)
        return;

    snprintf(log, 256, "=============== LSMessage Details for %s ===============\n", usage ? usage : "NA");
    loc_logger_feed_log(&location_request_logger, log, strlen(log));
    LS_LOG_DEBUG("%s", log);
    snprintf(log, 256, "Connection \"%s\"\n", service_name ? service_name : "None");
    loc_logger_feed_log(&location_request_logger, log, strlen(log));
    snprintf(log, 256, "UniqueToken: %s\n", LSMessageGetUniqueToken(msg) ? LSMessageGetUniqueToken(msg) : "NULL");
    loc_logger_feed_log(&location_request_logger, log, strlen(log));
    snprintf(log, 256, "ApplicationID: %s\n", LSMessageGetApplicationID(msg) ? LSMessageGetApplicationID(msg) : "NULL");
    loc_logger_feed_log(&location_request_logger, log, strlen(log));
    LS_LOG_DEBUG("%s", log);
    snprintf(log, 256, "Sender: %s\n", LSMessageGetSender(msg) ? LSMessageGetSender(msg) : "NULL");
    loc_logger_feed_log(&location_request_logger, log, strlen(log));
    snprintf(log, 256, "SenderServiceName: %s\n",
             LSMessageGetSenderServiceName(msg) ? LSMessageGetSenderServiceName(msg) : "NULL");
    loc_logger_feed_log(&location_request_logger, log, strlen(log));
    LS_LOG_DEBUG("%s", log);
    snprintf(log, 256, "Category: %s\n", LSMessageGetCategory(msg) ? LSMessageGetCategory(msg) : "NULL");
    loc_logger_feed_log(&location_request_logger, log, strlen(log));
    snprintf(log, 256, "Method: %s\n", LSMessageGetMethod(msg) ? LSMessageGetMethod(msg) : "NULL");
    loc_logger_feed_log(&location_request_logger, log, strlen(log));
    LS_LOG_DEBUG("%s", log);
    snprintf(log, 256, "Payload: %s\n", LSMessageGetPayload(msg) ? LSMessageGetPayload(msg) : "NULL");
    loc_logger_feed_log(&location_request_logger, log, strlen(log));
    LS_LOG_DEBUG("%s", log);
    snprintf(log, 256, "==============================================================\n");
    loc_logger_feed_log(&location_request_logger, log, strlen(log));
    LS_LOG_DEBUG("%s", log);
}

void LocationService::replyErrorToGpsNwReq(HandlerTypes handler) {
    char *payload = NULL;

    payload = LSMessageGetErrorReply(LOCATION_LOCATION_OFF);

    switch (handler) {
        case HANDLER_GPS: {
            if (isSubscListFilled(NULL, SUBSC_GPS_GET_NMEA_KEY, false) == true)
                LSSubscriptionNonSubscriptionRespondPubPri(SUBSC_GPS_GET_NMEA_KEY, payload);

            if (isSubscListFilled(NULL, SUBSC_GET_GPS_SATELLITE_DATA, false) == true)
                LSSubscriptionNonSubscriptionRespondPubPri(SUBSC_GET_GPS_SATELLITE_DATA, payload);

            if (isSubscListFilled(NULL, SUBSC_GET_LOC_UPDATES_GPS_KEY, false) == true)
                LSSubNonSubRespondGetLocUpdateCasePubPri(NULL, NULL, SUBSC_GET_LOC_UPDATES_GPS_KEY, payload);
        }
            break;
        case HANDLER_NETWORK: {
            if (isSubscListFilled(NULL, SUBSC_GET_LOC_UPDATES_NW_KEY, false) == true)
                LSSubNonSubRespondGetLocUpdateCasePubPri(NULL, NULL, SUBSC_GET_LOC_UPDATES_NW_KEY, payload);

        }
            break;
        case HANDLER_HYBRID: {
            if (isSubscListFilled(NULL, SUBSC_GET_LOC_UPDATES_HYBRID_KEY, false) == true)
                LSSubNonSubRespondGetLocUpdateCasePubPri(NULL, NULL, SUBSC_GET_LOC_UPDATES_HYBRID_KEY, payload);
        }
            break;
        default: {
            LS_LOG_ERROR("replyErrorToGpsNwReq: unknown handler type received \n");
        }
            break;
    }
}

void LocationService::LSSubscriptionNonSubscriptionRespondPubPri(const char *key, const char *payload) {
    LSSubscriptionNonSubscriptionRespond(mServiceHandle,
                                         key,
                                         payload);
}

void LocationService::LSSubNonSubRespondGetLocUpdateCasePubPri(Position *pos, Accuracy *acc, const char *key,
                                                               const char *payload) {
    LSSubNonSubRespondGetLocUpdateCase(pos,
                                       acc,
                                       mServiceHandle,
                                       key,
                                       payload);
}

void LocationService::resumeGpsEngine(void) {
    int ret = ERROR_NONE;
    if (mGpsStatus == false) {
        LS_LOG_INFO("GPS state off\n");
        return;
    }

    if (!isSubscListFilled(NULL, SUBSC_GPS_GET_NMEA_KEY, false) &&
        !isSubscListFilled(NULL, SUBSC_GET_GPS_SATELLITE_DATA, false) &&
        !isSubscListFilled(NULL, SUBSC_GET_LOC_UPDATES_GPS_KEY, false) &&
        !isSubscListFilled(NULL, SUBSC_GET_LOC_UPDATES_HYBRID_KEY, false)) {

        LS_LOG_INFO("Request List Empty! Fall back to resume GPS engine ...\n");
    } else {
        LS_LOG_INFO("Request List in Queue! Resuming GPS engine ...\n");

        if (isSubscListFilled(NULL, SUBSC_GPS_GET_NMEA_KEY, false)) {
            PositionRequest request("GPS", NMEA_CMD);
            ret = mGPSProvider->processRequest(request);
        }

        if (isSubscListFilled(NULL, SUBSC_GET_GPS_SATELLITE_DATA, false)) {
            PositionRequest request("GPS", SATELITTE_CMD);
            ret = mGPSProvider->processRequest(request);
        }

        if (isSubscListFilled(NULL, SUBSC_GET_LOC_UPDATES_GPS_KEY, false) ||
            isSubscListFilled(NULL, SUBSC_GET_LOC_UPDATES_HYBRID_KEY, false)) {
            PositionRequest request("GPS", POSITION_CMD);
            ret = mGPSProvider->processRequest(request);
        }

        LS_LOG_INFO("resumeGpsEngine ret %d", ret);
    }
}

void LocationService::stopGpsEngine(void) {
    if (mGpsStatus) {
        LS_LOG_INFO("Stopping GPS engine ...\n");
        PositionRequest request("GPS", STOP_GPS_CMD);
        mGPSProvider->processRequest(request);
    }
}

/* vim:set et: */
