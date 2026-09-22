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


#ifndef H_LocationService
#define H_LocationService

#include <glib.h>
#include <gio/gio.h>
#include <luna-service2/lunaservice.h>
#include <ServiceAgent.h>
#include <IConnectivityListener.h>
#include <ILocationCallbacks.h>
#include <string.h>
#include <Location.h>
#include "boost/shared_ptr.hpp"
#include "boost/array.hpp"
#include "ConnectionStateObserver.h"
#include <vector>
#include <pthread.h>
#include <loc_log.h>
#include <pbnjson.h>
#include <sys/time.h>
#include <LifeCycleMonitor.h>
#include <loc_logger.h>
#include <unordered_map>
#include <LocationWebServiceProvider.h>
#include <WSPConfigurationFileParser.h>
#include <NetworkRequestManager.h>
#include <NetworkPositionProvider.h>
#include <PositionProviderInterface.h>
#include <GPSPositionProvider.h>
#include <Position.h>

#define SHORT_RESPONSE_TIME                 10000
#define MEDIUM_RESPONSE_TIME                100000
#define LONG_RESPONSE_TIME                  150000
#define ACCURACY_LEVEL_HIGH 1
#define ACCURACY_LEVEL_MEDIUM 2
#define ACCURACY_LEVEL_LOW 3
#define RESPONSE_LEVEL_LOW 1
#define RESPONSE_LEVEL_MEDIUM 2
#define RESPONSE_LEVEL_HIGH 3
#define MAX_RESULT_LENGTH 400
#define MAX_GETSTATE_PARAM 32
#define TIME_SCALE_SEC 1000
#define LOCATION_SERVICE_NAME           "com.webos.service.location"

#define GEOFENCE_ENTERED                    0
#define GEOFENCE_EXITED                     1
#define GEOFENCE_UNCERTAIN                  2
#define GEOFENCE_UNAVAILABLE                (1<<0L)
#define GEOFENCE_AVAILABLE                  (1<<1L)
#define GEOFENCE_OPERATION_SUCCESS          0
#define GEOFENCE_ERROR_TOO_MANY_GEOFENCES   -100
#define GEOFENCE_ERROR_ID_EXISTS            -101
#define GEOFENCE_ERROR_ID_UNKNOWN           -102
#define GEOFENCE_ERROR_INVALID_TRANSITION   -103
#define GEOFENCE_ERROR_GENERIC              -149

#define MAX_GEOFENCE_ID                     200
#define MIN_GEOFENCE_RANGE                  1000
#define MAX_GEOFENCE_RANGE                  1200
#define KEY_MAX                             64

#define LSERROR_CHECK_AND_PRINT(ret, lsError)\
    do {                          \
        if (ret == false) {               \
            LSErrorPrintAndFree(&lsError);\
            return false; \
        }                 \
    } while (0)

#define LOCATION_ASSERT(cond) \
    do {                    \
        if (!(cond)) {                      \
            LS_LOG_WARNING(#cond ": failed in %s, %s, %d", __FUNCTION__, __FILE__, __LINE__ );    \
            assert(!#cond);                 \
        }                                   \
    } while (0)

#define LOCATION_SERVICE_METHOD(name) \
    bool name(LSHandle *sh, LSMessage *message, void *data); \
    static bool _##name(LSHandle *sh, LSMessage *message, void *data) { \
        return ((LocationService*)data)->name(sh, message, NULL); \
    }

/**
 * @brief CONSTANT DEFINITIONS
 */
typedef struct _GPSStatusData {
    char *statusString;
    LSHandle *lsHandle;
} GPSStatusData;

typedef struct _NmeaData {
    char *nmeaString;
    LSHandle *lsHandle;
} NmeaData;

typedef struct _NfwData {
    char *nfwString;
    LSHandle *lsHandle;
} NfwData;

typedef struct _SatelliteData {
    char *satelliteString;
    LSHandle *lsHandle;
} SatelliteData;

typedef struct _GeofenceAddData {
            char *geofenceString;
            LSHandle *lsHandle;
            int32_t geofenceId;
        }GeofenceAddData;

typedef struct _GeofenceRemoveData {
            char *geofenceString;
            LSHandle *lsHandle;
            int32_t geofenceId;
            int32_t status;
        } GeofenceRemoveData;
typedef struct _PositionData {
    Position pos;
    Accuracy acc;
    char *key1;
    char *key2;
    char *retString1;
    char *retString2;
} PositionData;

class LocationService : public IConnectivityListener,public ILocationCallbacks {
public:
    static const int GETLOC_UPDATE_NW = 0;
    static const int GETLOC_UPDATE_GPS_NW = 1;
    static const int GETLOC_UPDATE_GPS = 2;
    static const int GETLOC_UPDATE_PASSIVE = 3;
    static const int GETLOC_UPDATE_INVALID = 4;

    static const double INVALID_LAT;
    static const double INVALID_LONG;

    class TimerData {
    public:
        TimerData(LSMessage *message, LSHandle *sh, unsigned char handlerType, const char *argkey) {
            m_message = message;
            m_sh = sh;
            m_handlerType = handlerType;
            timerStart = true;
            /*
             * MIN(MAX, ...) did not mean what it reads as: MAX is glib's
             * two-argument macro, so with no argument list it fell through to
             * the only MAX in scope - FeatureType::MAX, the geocoding
             * feature-type enumerator, whose value is 6.  Every subscription
             * key was therefore truncated to six bytes and, because six is
             * shorter than any real key, left unterminated: "gps/getLocationU-
             * pdate" became "gps/ge" followed by whatever was on the stack.
             * getKey() hands that to LSSubscriptionAcquire, so the timeout path
             * looked up a garbage key and read past the buffer doing it.
             */
            memset(key, 0x00, sizeof(key));

            if (argkey != NULL)
                g_strlcpy(key, argkey, sizeof(key));
        }

        LSHandle *GetHandle() const {
            return m_sh;
        }

        LSMessage *GetMessage() const {
            return m_message;
        }

        unsigned char GetHandlerType() const {
            return m_handlerType;
        }

        void stopTimer() {
            timerStart = false;
        }

        bool getTimerStatus() {
            return timerStart;
        }

        const char *getKey() {
            return key;
        }

    private:
        LSMessage *m_message;
        LSHandle *m_sh;
        unsigned char m_handlerType;
        bool timerStart;
        char key[KEY_MAX];
    };

    class LocationUpdateRequest {
    public:
        LocationUpdateRequest(LSMessage *msg, long long reqTime, guint timerID, TimerData *timerData, double latitude,
                              double longitude, int hander_type) {
            m_message = msg;
            m_requestTime = reqTime;
            m_lastlat = latitude;
            m_lastlong = longitude;
            m_isFirstReply = true;
            m_timerdata = timerData;
            m_timerID = timerID;
            m_handler_type = hander_type;
        }

        LSMessage *getMessage() {
            return m_message;
        }

        long long getRequestTime() {
            return m_requestTime;
        }

        void updateRequestTime(long long currentTime) {
            m_requestTime = currentTime;
        }

        double getLatitude() {
            return m_lastlat;
        }

        double getLongitude() {
            return m_lastlong;
        }

        guint getTimerID() const {
            return m_timerID;
        }

        void updateLatAndLong(double latitude, double longitude) {
            m_lastlat = latitude;
            m_lastlong = longitude;
        }

        bool getFirstReply() {
            return m_isFirstReply;
        }

        void updateFirstReply(bool firstreply) {
            m_isFirstReply = firstreply;
        }

        TimerData *getTimerData() {
            return m_timerdata;
        }

        void setTimerData(TimerData *timerData) {
            m_timerdata = timerData;
        }

        int getHandlerType() {
            return m_handler_type;
        }

    private:
        LSMessage *m_message;
        long long m_requestTime;
        double m_lastlong;
        double m_lastlat;
        bool m_isFirstReply;
        guint m_timerID;
        TimerData *m_timerdata;
        int m_handler_type;
    };

    virtual ~LocationService();

    bool init(GMainLoop *);

    bool locationServiceRegister(const char *srvcname, GMainLoop *mainLoop, LSHandle **mServiceHandle);

    static LocationService *getInstance();

    // /**Callback called from Handlers********/

    static void sendNmeaData(GObject *source, GAsyncResult *res, gpointer userdata);
    static void sendNfwNotification(GObject *source, GAsyncResult *res, gpointer userdata);

    static void nmeaDataUnref(gpointer data);
    static void nfwDataUnref(gpointer data);

    static void sendSatelliteData(GObject *source, GAsyncResult *res, gpointer userdata);

    static void satelliteDataUnref(gpointer data);

    static void sendPositionData(GObject *source, GAsyncResult *res, gpointer userdata);

    static void positionDataUnref(gpointer data);

    static void sendGPSStatus(GObject *source, GAsyncResult *res, gpointer userdata);

    static void gpsStatusUnref(gpointer data);
    static void sendGeofenceAddData(GObject *source, GAsyncResult *res, gpointer userdata);
    static void sendGeofenceRemoveData(GObject *source, GAsyncResult *res, gpointer userdata);
    static void sendGeofenceBreachData(GObject *source, GAsyncResult *res, gpointer userdata);
    static void geofenceAddDataUnref(gpointer data);
    static void geofenceRemoveDataUnref(gpointer data);

    static gboolean TimerCallbackLocationUpdate(void *data) {
        return getInstance()->_TimerCallbackLocationUpdate(data);
    }


    bool getHandlerStatus(const char *);

    bool loadHandlerStatus(const char *handler);

    LSHandle *getPrivatehandle() {
        return mServiceHandle;
    }

    void setWifiState(bool state) {

        if (state == true) {
            LS_LOG_INFO("WIFI turned on");
        } else {
            LS_LOG_INFO("WIFI turned off");
        }

        wifistate = state;
    }

    bool getWifiState() {
        return wifistate;
    }

    void updateConnectionManagerState(bool state) {

        if (state == true) {
            LS_LOG_INFO("Internet connection available");
        } else {
            LS_LOG_INFO("Internet connection not available");
        }

        isInternetConnectionAvailable = state;
    }

    bool getConnectionManagerState() {
        return isInternetConnectionAvailable;
    }

    void updateTelephonyState(bool state) {

        if (state == true) {
            LS_LOG_INFO("Telephony connection available");
        } else {
            LS_LOG_INFO("Telephony connection not available");
        }

        isTelephonyAvailable = state;
    }

    /*
     * sleepd on LuneOS broadcasts "suspended" before every suspend attempt
     * and a "resume" after each one, refused or not (see SleepdSignals.h);
     * ConnectionStateObserver only forwards the kernel-typed resume, so this
     * runs once per real wake. The GPS engine is no longer stopped ahead of
     * an attempt: the attempt is often refused, and the stop/restart pair on
     * every refusal was pointless churn. While a fix is being computed the
     * GNSS stack holds its own wakelock, which is the proper veto. Re-issuing
     * the active requests is a no-op when the engine kept running (the
     * provider rejects duplicates) and restarts it if the sleep dropped the
     * session.
     */
    void handleKernelResume() {
        LS_LOG_INFO("sleepd resume after a kernel suspend: re-issuing active GPS requests\n");
        resumeGpsEngine();
    }

    bool getTelephonyState() {
        return isTelephonyAvailable;
    }

    void updateWifiInternetState(bool state) {

        if (state == true) {
            LS_LOG_INFO("WIfi Internet connetion available");
        } else {
            LS_LOG_INFO("WIfi Internet connection not available");
        }

        isWifiInternetAvailable = state; //state; for TESTING
    }

    bool getWifiInternetState() {
        return isWifiInternetAvailable;
    }

    void Handle_WifiNotification(bool wifi_state) {
        setWifiState(wifi_state);
    }

    void Handle_ConnectivityNotification(bool Conn_state) {
        updateConnectionManagerState(Conn_state);
    }

    void Handle_TelephonyNotification(bool Tele_state) {
        updateTelephonyState(Tele_state);
    }

    void Handle_KernelResumeNotification() {
        handleKernelResume();
    }

    void Handle_WifiInternetNotification(bool Internet_state) {
        updateWifiInternetState(Internet_state);
    }

    void updateNwKey(const char *nwKey) {
        if (!nwKey)
            return;

        if (nwGeolocationKey) {
            g_free(nwGeolocationKey);
            nwGeolocationKey = NULL;
        }

        nwGeolocationKey = g_strdup(nwKey);
    }

    void updateLbsKey(const char *lbsKey) {
        if (!lbsKey)
            return;

        if (lbsGeocodeKey) {
            g_free(lbsGeocodeKey);
            lbsGeocodeKey = NULL;
        }

        lbsGeocodeKey = g_strdup(lbsKey);
    }

    void LSSubscriptionNonSubscriptionRespond(LSHandle *psh, const char *key, const char *payload);

    void LSSubscriptionNonSubscriptionReply(LSHandle *sh, const char *key, const char *payload);

    bool isSubscribeTypeValid(LSHandle *sh, LSMessage *message, bool isMandatory, bool *isSubscription);

    void stopNonSubcription(const char *key);

    bool isSubscListFilled(LSMessage *message, const char *key, bool cancelCase);

    bool enableGpsHandler(unsigned char *startedHandlers);

    bool enableNwHandler(unsigned char *startedHandlers);

    int enableHandlers(int sel_handler, char *key, unsigned char *startedHandlers);

    void LSSubNonSubRespondGetLocUpdateCase(Position *pos, Accuracy *acc, LSHandle *psh, const char *key,
                                            const char *payload);

    void LSSubNonSubReplyLocUpdateCase(Position *pos, Accuracy *acc, LSHandle *sh, const char *key,
                                       const char *payload);

    bool LSMessageReplyLocUpdateCase(LSMessage *msg,
                                     LSHandle *sh,
                                     const char *key,
                                     const char *payload,
                                     LSSubscriptionIter *iter);

    bool meetsCriteria(LSMessage *msg, Position *pos, Accuracy *acc, int minInterval, int minDist);

    void getLocRequestStopSubscription(LSHandle *sh, LSMessage *message);

    bool LSMessageRemoveReqList(LSMessage *message);

    bool removeTimer(LSMessage *message);

    NetworkPositionProvider *getNwProvider(void) {
        return mNetworkProvider;
    }

    void LSSubNonSubRespondGetLocUpdateCasePubPri(Position *pos, Accuracy *acc, const char *key, const char *payload);

    bool deinit();
public:
    void getLocationUpdateCb(GeoLocation& location, ErrorCodes errCode,HandlerTypes type);
    void getNmeaDataCb(long long timestamp, char *data, int length);
    void nfwNotifyCb(nyx_gps_nfw_notification_t *notification);
    void getGpsStatusCb(int state);
    void getGpsSatelliteDataCb(Satellite *);
    void geofenceAddCb(int32_t geofence_id, int32_t status, gpointer user_data);
    void geofenceRemoveCb(int32_t geofence_id, int32_t status, gpointer user_data);
    void geofencePauseCb(int32_t geofence_id, int32_t status, gpointer user_data);
    void geofenceResumeCb(int32_t geofence_id, int32_t status, gpointer user_data);
    void geofenceBreachCb(int32_t geofence_id, int32_t status, int64_t timestamp, double latitude, double longitude, gpointer user_data);
    void geofenceStatusCb (int32_t status, Position *last_position, Accuracy *accuracy, gpointer user_data);
private:
    bool mGpsStatus;
    bool mNwStatus;
    bool mCachedGpsEngineStatus;
    typedef boost::shared_ptr<LocationUpdateRequest> LocationUpdateRequestPtr;
    std::unordered_map<LSMessage *, LocationUpdateRequestPtr> m_locUpdate_req_table;
    bool wifistate;
    bool isInternetConnectionAvailable;
    bool isTelephonyAvailable;
    bool isWifiInternetAvailable;
    static LocationService *locService;
    static LSMethod rootMethod[];
    static LSMethod prvMethod[];
    static LSMethod geofenceMethod[];
    static LSMethod mockPublicMethod[];
    static LSMethod mockPrivateMethod[];
    bool is_geofenceId_used[MAX_GEOFENCE_ID];
    static bool buildGetStateSubscriptionKey(char *dest, size_t destLen, const char *handler);
    bool geofenceSlot(int geofenceId, int *slot);
    GHashTable *htPseudoGeofence;
    LSHandle *mServiceHandle;

    // time to first fix for most recent session
    static long mTTFF;

    GMainLoop *mMainLoop;
    NetworkRequestManager *mNetReqMgr;
    LocationWebServiceProvider *mLBSProvider;
    NetworkPositionProvider *mNetworkProvider;
    GPSPositionProvider *mGPSProvider;
    ConnectionStateObserver * connectionStateObserverObj;
    static const char *geofenceStateText[GEOFENCE_MAXIMUM];

    LifeCycleMonitor *m_lifeCycleMonitor;
    bool m_enableSuspendBlocker;

    char *nwGeolocationKey;
    char *lbsGeocodeKey;

    data_logger_t *location_request_logger;

    LocationService();

    LOCATION_SERVICE_METHOD(getNmeaData);
    LOCATION_SERVICE_METHOD(getReverseLocation);
    LOCATION_SERVICE_METHOD(getGeoCodeLocation);
    LOCATION_SERVICE_METHOD(getAllLocationHandlers);
    LOCATION_SERVICE_METHOD(getGpsStatus);
    LOCATION_SERVICE_METHOD(setState);
    LOCATION_SERVICE_METHOD(getState);
    LOCATION_SERVICE_METHOD(sendExtraCommand);
    LOCATION_SERVICE_METHOD(setGPSParameters);
    LOCATION_SERVICE_METHOD(stopGPS);
    LOCATION_SERVICE_METHOD(exitLocation);
    LOCATION_SERVICE_METHOD(getLocationHandlerDetails);
    LOCATION_SERVICE_METHOD(getGpsSatelliteData);
    LOCATION_SERVICE_METHOD(getTimeToFirstFix);
    LOCATION_SERVICE_METHOD(getGpsDebugData);
    LOCATION_SERVICE_METHOD(getNfwNotifications);
    LOCATION_SERVICE_METHOD(getLocationUpdates);
    LOCATION_SERVICE_METHOD(getCachedPosition);
    LOCATION_SERVICE_METHOD(cancelSubscription);
    LOCATION_SERVICE_METHOD(addGeofenceArea);
    LOCATION_SERVICE_METHOD(getGeofenceStatus);
    LOCATION_SERVICE_METHOD(pauseGeofence);
    LOCATION_SERVICE_METHOD(resumeGeofence);
    LOCATION_SERVICE_METHOD(removeGeofenceArea);
    LOCATION_SERVICE_METHOD(enableMockLocation);
    LOCATION_SERVICE_METHOD(disableMockLocation);
    LOCATION_SERVICE_METHOD(setMockLocation);

    gboolean _TimerCallbackLocationUpdate(void *data);

    void geocodingReply(const char *response, int error, LSMessage *message);

    void geocodingCb(GeoLocation& location, int errCode, LSMessage *message);

    void reverseGeocodingCb(GeoAddress& address, int errCode, LSMessage *message);

    void geofence_add_reply(int32_t geofence_id, int32_t status);

    void geofence_breach_reply(int32_t geofence_id, int32_t status, int64_t timestamp, double latitude,
                               double longitude);

    void geofence_remove_reply(int32_t geofence_id, int32_t status);

    void geofence_pause_reply(int32_t geofence_id, int32_t status);

    void geofence_resume_reply(int32_t geofence_id, int32_t status);
    void geofence_status_reply(int32_t status, Position *last_position, Accuracy *accuracy);
    void getLocationUpdate_reply(Position *pos, Accuracy *acc, int, int);

    void stopSubcription(LSHandle *sh, const char *key);

    void LSErrorPrintAndFree(LSError *ptrLSError);

    int getHandlerVal(char *handlerName);

    Position comparePositionTimeStamps(Position pos1, Position pos2, Accuracy acc1, Accuracy acc2, Accuracy *retAcc);

    void getReverseGeocodeData(jvalue_ref *parsedObj, GString **pos_data, Position *pos);

    void getGeocodeData(jvalue_ref *parsedObj, GString *addressData);

    void printMessageDetails(const char *usage, LSMessage *msg, LSHandle *sh);

    bool isListFilled(LSHandle *sh, LSMessage *message, const char *key, bool cancelCase);

    void replyErrorToGpsNwReq(HandlerTypes handler);

    void LSSubscriptionNonSubscriptionRespondPubPri(const char *key, const char *payload);

    void stopGpsEngine(void);

    void resumeGpsEngine(void);

    bool getLocationParameter( _Location* loc, jvalue_ref parsedObj );
    bool setMockLocationState( LSHandle *sh, LSMessage *message, void *data, int state );
};

#endif  //  H_LocationService
