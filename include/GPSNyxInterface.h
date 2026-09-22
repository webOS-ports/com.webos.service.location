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


#ifndef GPSNYXINTERFACE_H_
#define GPSNYXINTERFACE_H_
#include <nyx/common/nyx_gps_common.h>
#include <nyx/common/nyx_device.h>
#include <nyx/client/nyx_gps.h>
#include <glib.h>
#include "NtpClient.h"

class GPSNyxInterface :public INtpClinetCallback{
public:
    GPSNyxInterface(): gpsProviderInstance(nullptr), mDownloadNtpDataStatus(IDLE),mNyxGpsSystem(nullptr) {
    }
    nyx_error_t initialize(void *instance);
    void deInitialize();

public:
    void *gpsProviderInstance;
    nyx_error_t startGPS();
    nyx_error_t stopGPS();
    nyx_error_t setPositionMode(nyx_gps_position_mode_t mode,
            nyx_gps_position_recurrence_t recurrence, uint32_t min_interval,
            uint32_t preferred_accuracy, uint32_t preferred_time);
    nyx_error_t setServerInfo(nyx_agps_type_t type, const char *hostname, int port);
    nyx_error_t dataConnectionOpen(NetworkInfo *networkInfo);
    nyx_error_t dataConnectionClosed(nyx_agps_type_t agpsType);
    nyx_error_t dataConnectionFailed(nyx_agps_type_t agpsType);
    nyx_error_t injectExtraData(XtraData *xtraData);
    nyx_error_t injectExtraTime(XtraTime* timeInfo);
    nyx_error_t injectExtraTime(int64_t utcTime, int64_t timeReference,
            int uncertainty);
    nyx_error_t injectExtraCommand(char* command);
    nyx_error_t updateNetworkAvailablity(NetworkInfo *networkInfo);
    nyx_error_t deleteAidingData();
    /*
     * Diagnostics straight from the GNSS engine. dest must be at least
     * NYX_GPS_DEBUG_DATA_MAXLEN bytes; the module answers
     * NYX_ERROR_NOT_IMPLEMENTED where the HAL exposes no IGnssDebug.
     */
    nyx_error_t getDebugData(char *dest, size_t destLen);


    //geofence
    bool geofenceRemove(int32_t geofenceId);
    bool geofencePause(int32_t geofenceId, GError **error);
    bool geofenceResume(int32_t geofenceId, int32_t monitorTransition, GError **error);
    bool geofenceAdd(int32_t geofenceId, double latitude, double longitude, double radius);

    //nyx callbacks
    static void gpsLocationCb(nyx_gps_location_t *positiondata, void *user_data);
    static void agpsStatusCb(nyx_agps_status_t *status, void *user_data);
    static void gpsStatusCb(nyx_gps_status_t *gps_status, void *user_data);
    static void gpsSvStatusCb(nyx_gps_sv_status_t *sat_data, void *user_data);
    static void gpsSetCapabilitiesCb(uint32_t capabilities, void *user_data);
    static void gpsNmeaCb(int64_t timestamp, const char* nmea, int length, void *user_data);
    static void gpsRequestUtcTimeCb(void *user_data);
    static void gpsXtraDownloadRequestCb(void *user_data);
    static void nfwNotifyCb(nyx_gps_nfw_notification_t *notification, void *user_data);
    void injectSystemTime();
    static void xtraTimeCb(int64_t utcTime, int64_t timeReference, int uncertainty, void *user_data);
    static void xtraDataCb(char *data, int length, void *user_data);
    static gpointer xtraDataDownloadThread(gpointer arg);
    static void geofenceResumeCb(int32_t geofence_id, int32_t status, void *user_data);
    static void geofencePauseCb(int32_t geofence_id, int32_t status, void *user_data);
    static void geofenceRemoveCb(int32_t geofence_id, int32_t status, void *user_data);
    static void geofenceAddCb(int32_t geofence_id, int32_t status, void *user_data);
    static void geofenceStatusCb(int32_t status, nyx_gps_location_t *last_location, void *user_data);
    static void geofenceBreachCb(int32_t geofence_id,
            nyx_gps_location_t *location, int32_t transition, int64_t timestamp,
            void *user_data);
    static void gpsReleaseWakelockCb(void *user_data);
    static void gpsAcquireWakelockCb(void *user_data);


private:
    bool mXtraDefault =false;
    NtpClient client;
    DownloadStateEType mDownloadNtpDataStatus ;
public:
    virtual void onRequestCompleted(NtpErrors error, const NTPData *data);
    nyx_gps_nfw_callbacks_t mNfwCallbacks;
    nyx_device_handle_t mNyxGpsSystem;
    nyx_gps_callbacks_t mGPSCallbacks;
    nyx_gps_location_t mPosition;
    nyx_gps_sv_status_t mSatellite;
    nyx_agps_callbacks_t mAGPSCallbacks;
    nyx_gps_geofence_callbacks_t mGeofenceCallbacks;
    nyx_gps_xtra_callbacks_t mXtraCallbacks;
    nyx_gps_xtra_client_callbacks_t mXtraClientCallbacks;
    nyx_gps_xtra_client_config_t mXtraConfig;
};

#endif /* GPSNYXINTERFACE_H_ */
