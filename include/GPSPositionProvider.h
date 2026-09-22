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


#ifndef GPSPOSITIONPROVIDER_H_
#define GPSPOSITIONPROVIDER_H_

#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string>
#include <exception>
#include <pthread.h>
#include <glib-object.h>
#include <nyx/common/nyx_gps_common.h>
#include <nyx/common/nyx_device.h>
#include <nyx/client/nyx_gps.h>
#include <loc_http.h>
#include <loc_geometry.h>
#include <loc_geometry.h>
#include <loc_log.h>
#include <loc_logger.h>
#include <gps_cfg.h>
#include <pbnjson.h>
#include <Location.h>
#include <Position.h>
#include <Gps_stored_data.h>
#include <PositionProviderInterface.h>
#include <GPSServiceConfig.h>
#include <GpsWanInterface.h>
#include "HandlerBase.h"

using namespace std;

#define printf_debug LS_LOG_DEBUG
#define printf_error LS_LOG_ERROR
#define printf_info LS_LOG_INFO
#define printf_warning LS_LOG_WARNING

#define GPS_CONF_FILE    "/etc/location/gpsConfig.conf"

#define GPS_UPDATE_INTERVAL_MAX     12*60

// Data connection state
#define DATA_CONNECTION_IDLE            0
#define DATA_CONNECTION_OPEN            1
#define DATA_CONNECTION_CLOSED          2
#define DATA_CONNECTION_FAILED          3

// Default parameters
#define DEFAULT_FIX_INTERVAL            1000

// NTP Time
// NTP time stamp of 1 Jan 1970 = 2,208,988,800
#define NTP_EPOCH                       (86400U * (365U * 70U + 17U))
#define NTP_REPLY_TIMEOUT               6000

#define MILLI_SEC_FRAQ                  1000000
#define NANO_SEC_FRAQ                   1000000000LL
#define SOCK_MODE                       (3 << 3) | 3
#define SOCK_DEFAULT_PORT               123
#define OFFSET                          1000

#define HTTP_SUCESS_STATUS_CODE         200

// LGE HAL command prefix
#define LGE_GPS_EXTRACMD                "lge.gps.extracmd"

#define SCHEMA_ANY                      "{}"

// Data connection
#define USER_DATA_CONNECTION_TYPE       "cellular"
#define USER_DATA_CONNECTION_NAME       "default"

#define GPS_ERROR (GPSErrorQuark ())

GQuark GPSErrorQuark(void);

typedef enum {
    PROGRESS_NONE = 0,
    GET_POSITION_ON = 1 << 0,
    START_TRACKING_ON = 1 << 1,
    NMEA_GET_DATA_ON = 1 << 2,
    SATELLITE_GET_DATA_ON = 1 << 3,
    LOCATION_UPDATES_ON = 1 << 4
} ProviderStateFlags;

/* GPS thread action */
typedef enum {
    ACTION_NONE,
    ACTION_QUIT,
    ACTION_OPEN_ATL,
    ACTION_CLOSE_ATL,
    ACTION_FAIL_ATL,
    ACTION_NI_NOTIFY,
    ACTION_XTRA_DATA,
    ACTION_XTRA_TIME,
    ACTION_NLP_RESPONSE,
    ACTION_PHONE_CONTEXT_UPDATE
} GPSThreadActionEType;

typedef enum {
    PENDINGNETWORK = 0,
    DOWNLOADING,
    IDLE
} DownloadStateEType;

/**
 * AccuracyLevel:
 *
 * Enum values used to define the approximate accuracy of
 * Position or Address information. These are ordered in
 * from lowest accuracy possible to highest accuracy possible.
 * geoclue_accuracy_get_details() can be used to get get the
 * current accuracy. It is up to the provider to set the
 * accuracy based on analysis of its queries.
 **/
typedef enum {
    GEOCLUE_ACCURACY_LEVEL_NONE = 0,
    GEOCLUE_ACCURACY_LEVEL_COUNTRY,
    GEOCLUE_ACCURACY_LEVEL_REGION,
    GEOCLUE_ACCURACY_LEVEL_LOCALITY,
    GEOCLUE_ACCURACY_LEVEL_POSTALCODE,
    GEOCLUE_ACCURACY_LEVEL_STREET,
    GEOCLUE_ACCURACY_LEVEL_DETAILED,
} GeoclueGpsAccuracyLevel;

typedef enum {
    GPS_POSITION_FIELDS_NONE = 0,
    GPS_POSITION_FIELDS_LATITUDE = 1 << 0,
    GPS_POSITION_FIELDS_LONGITUDE = 1 << 1,
    GPS_POSITION_FIELDS_ALTITUDE = 1 << 2,
    GPS_POSITION_FIELDS_SPEED = 1 << 3,
    GPS_POSITION_FIELDS_DIRECTION = 1 << 4,
    GPS_POSITION_FIELDS_CLIMB = 1 << 5
} PositionGPSFields;


/**
 * GeoclueVelocityFields:
 *
 * GeoclueVelocityFields is a bitfield that defines the validity of
 * Velocity values.
 **/

/**
 * GeoclueError:
 * @GEOCLUE_ERROR_NOT_IMPLEMENTED: Method is not implemented
 * @GEOCLUE_ERROR_NOT_AVAILABLE: Needed information is not currently
 * available (e.g. web service did not respond)
 * @GEOCLUE_ERROR_FAILED: Generic fatal error
 *
 * Error values for providers.
 **/


class XtraTime {
public:
    XtraTime();

public:
    int64_t utcTime;
    int64_t timeReference;
    int uncertainty;
};

class XtraData {
public:
    XtraData();

public:
    string data;
};

class NetworkInfo {
public:
    NetworkInfo();

public:
    int available;
    string apn;
    nyx_agps_bearer_type_t bearerType;
};

#include <GPSNyxInterface.h>

// Timebeing no need of catogery .
// in future we will change it to support other providers too.
enum KGpsCommands {
    COMMAND_GPS_ENABLE = 1,
    COMMAND_GPS_DISABLE,
    COMMAND_GPS_PERIODIC_UPDATE,
    COMMAND_EXTRA_COMMAND,
    COMMAND_SET_GPS_PARAMETER,
    COMMAND_ADD_GEOFENCE,
    COMMAND_REMOVE_GEOFENCE
};

class GPSPositionProvider : public PositionProviderInterface, public HandlerBase {
public:
    static GPSPositionProvider *getInstance() {
        static GPSPositionProvider gpsService;
        return &gpsService;
    }


    ~GPSPositionProvider() {
        printf_debug("Dtor of GPSPositionProvider\n");
        shutdown();
    }

    bool init(LSHandle *sh);

    void enable ();

    void disable();

    ErrorCodes processRequest(PositionRequest request);

    ErrorCodes getLastPosition(Position *position, Accuracy *accuracy);

    void gpsAccuracyGetDetails(double *horizontal_accuracy,
            double *vertical_accuracy);

    void gpsAccuracySetDetails(double horizontal_accuracy,
            double vertical_accuracy);

    void updateNetworkState(NetworkInfo *networkInfo);
    static gboolean gpsTimeoutCb(gpointer userdata);
    static void *gpsThreadProc(void *args);

    GPSStatus getGPSStatus();

    long long getTimeToFirstFixValue ();
    nyx_error_t getDebugData(char *dest, size_t destLen);
private:
    GPSPositionProvider();

    void shutdown();

    void mutexInit();

    void mutexDestroy();

    void setGpsParameters(uint32_t pos_mode,
                          uint32_t pos_recurrence,
                          uint32_t fix_interval,
                          char *supl_host,
                          int supl_port);

    void setPosModeWithCapability(uint32_t pos_mode,
                                  uint32_t pos_recurrence,
                                  uint32_t fix_interval);

    bool hasCapability(unsigned int capability);

    void gpsAccuracyFree(GValueArray *accuracy);

    bool processCommand(KGpsCommands gpsCommand, const char *value = nullptr);

    ErrorCodes requestGpsEngineStart();
    //command Handlers
    bool handleGpsEnable(void *data);

    bool handleGpsDisable(void *data);

    bool handlePeriodicUpdates(void *data);

    bool handleForceTimeInjection(void *data);

    bool handleForceXtraInjection(void *data);

    bool handleDeleteAidingData(void *data);

    bool handleEnableNMEALog(void *data);

    bool handleDisableNMEALog(void *data);

    bool handleCEPLog(void *data);

    bool handleDisableCEPLog(void *data);

    bool handleStartAnonymousLog(void *data);

    bool handleStopAnonymousLog(void *data);

    bool handleWriteAnonymousData(void *data);

    bool handleExtraCommand(void *data);

    bool handleSetGpsParameterCommand(void *data);

    bool handleAddGeofenceCommand(void *data);

    bool handleRemoveGeofenceCommand(void *data);

public:
    GPSStatus mGPSStatus = GPS_STATUS_UNAVAILABLE;

    GPSServiceConfig mGPSConf;
    GpsWanInterface mGpsWanInterface;
    GPSNyxInterface mGPSNyxInterface;
    bool mEngineStarted = false;
    unsigned int mGEngineCapabilities = 0x27;
    uint32_t mPositionMode;
    uint32_t mFixInterval = DEFAULT_FIX_INTERVAL;

    int mGeofenceStatus = NYX_GEOFENCER_UNAVAILABLE;
    bool mGpsParamModified = false;
    uint32_t mGPSDataConnectionState = DATA_CONNECTION_IDLE;
    DownloadStateEType mDownloadXtraDataStatus = IDLE;

    GPSThreadActionEType mGPSThreadAction = ACTION_NONE;
    pthread_t mGPSThreadID = -1;
    pthread_mutex_t mGPSThreadMutex;
    pthread_cond_t mGPSThreadCond;

    long long mFixRequestTime=0;

    long long mLastFixTime=0;
    gboolean mTTFFState = false;
    HttpReqTask *mHttpReqTask = nullptr;
    GeoLocation mLocation;

    XtraTime mXtraTime;
    XtraData mXtraData;
    NetworkInfo mNetworkInfo;

    bool mCEPLogEnable = false;
    bool mNMEALogEnable = false;
    RTCEPCalculator *mCEPCalculator = NULL;
    data_logger_t *mNMEALogger = NULL;
    data_logger_t *mCEPLogger = NULL;
    data_logger_t *mAnonymousLogger = NULL;

    int mAPIProgressFlag = PROGRESS_NONE;

private:

    unsigned int mPosTimer = 0;
    LSHandle *mLSHandle;
    long long mTTFF =0;
};

#endif /* GPSPOSITIONPROVIDER_H_ */
