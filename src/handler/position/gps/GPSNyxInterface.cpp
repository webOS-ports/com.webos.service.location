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


#include <GPSPositionProvider.h>
#include <MockLocation.h>


nyx_error_t GPSNyxInterface::initialize(void *instance) {
    int count;
    nyx_error_t rc;
    gpsProviderInstance = instance;
    GPSPositionProvider *gpsInstance =
            (GPSPositionProvider *) gpsProviderInstance;

    memset(&mPosition, 0, sizeof(nyx_gps_location_t));
    memset(&mSatellite, 0, sizeof(nyx_gps_sv_status_t));

    rc = nyx_device_open(NYX_DEVICE_GPS, gpsInstance->mGPSConf.mChipsetID,
                         &mNyxGpsSystem);
    if (NYX_ERROR_NONE != rc) {
        printf_error("failed to open nyx gps module: error = %d\n", rc);
        printf_debug("GPSPositionProvider exit\n");
        return rc;
    }
    // Register callbacks need to check about the instance.
    memset(&mGPSCallbacks, 0, sizeof(nyx_gps_callbacks_t));

    mGPSCallbacks.user_data = this;
    mGPSCallbacks.location_cb = gpsLocationCb;
    mGPSCallbacks.status_cb = gpsStatusCb;
    mGPSCallbacks.sv_status_cb = gpsSvStatusCb;
    mGPSCallbacks.nmea_cb = gpsNmeaCb;
    mGPSCallbacks.set_capabilities_cb = gpsSetCapabilitiesCb;
    mGPSCallbacks.acquire_wakelock_cb = gpsAcquireWakelockCb;
    mGPSCallbacks.release_wakelock_cb = gpsReleaseWakelockCb;
    mGPSCallbacks.request_utc_time_cb = gpsRequestUtcTimeCb;

    memset(&mXtraCallbacks, 0, sizeof(nyx_gps_xtra_callbacks_t));
    mXtraCallbacks.user_data = this;
    mXtraCallbacks.xtra_download_request_cb = gpsXtraDownloadRequestCb;

    memset(&mAGPSCallbacks, 0, sizeof(nyx_agps_callbacks_t));
    mAGPSCallbacks.user_data = this;
    mAGPSCallbacks.status_cb = agpsStatusCb;

    memset(&mGeofenceCallbacks, 0, sizeof(nyx_gps_geofence_callbacks_t));
    mGeofenceCallbacks.user_data = this;
    mGeofenceCallbacks.geofence_transition_cb = geofenceBreachCb;
    mGeofenceCallbacks.geofence_status_cb = geofenceStatusCb;
    mGeofenceCallbacks.geofence_add_cb = geofenceAddCb;
    mGeofenceCallbacks.geofence_remove_cb = geofenceRemoveCb;
    mGeofenceCallbacks.geofence_pause_cb = geofencePauseCb;
    mGeofenceCallbacks.geofence_resume_cb = geofenceResumeCb;

    rc = nyx_gps_init(mNyxGpsSystem, &mGPSCallbacks, &mXtraCallbacks,
                      &mAGPSCallbacks, nullptr, nullptr, &mGeofenceCallbacks);
    if (NYX_ERROR_NONE != rc) {
        printf_error("failed to initialize nyx gps module: error = %d\n", rc);
        printf_debug("GPSPositionProvider exit\n");
        return rc;
    }
    /*
     * Non-framework location notifications. Registered separately from
     * nyx_gps_init because they are not part of the GNSS position flow: they
     * report that something outside the platform asked where we are. Absent on
     * any HAL without IGnssVisibilityControl, which is every 1.x one, so
     * NYX_ERROR_NOT_IMPLEMENTED is an ordinary answer and not a failure.
     */
    memset(&mNfwCallbacks, 0, sizeof(nyx_gps_nfw_callbacks_t));
    mNfwCallbacks.user_data = this;
    mNfwCallbacks.nfw_notify_cb = nfwNotifyCb;

    if (NYX_ERROR_NONE != nyx_gps_set_nfw_callback(mNyxGpsSystem, &mNfwCallbacks))
        printf_info("non-framework location notifications unavailable\n");

    if (strcmp(gpsInstance->mGPSConf.mChipsetID, "Qcom") == 0) {
        mXtraClientCallbacks.user_data = this;
        mXtraClientCallbacks.xtra_client_data_cb =
                (nyx_gps_xtra_client_data_callback) (xtraDataCb);
        mXtraClientCallbacks.xtra_client_time_cb =
                (nyx_gps_xtra_client_time_callback) (xtraTimeCb);

        count = 0;
        strncpy(mXtraConfig.xtra_server_url[count++],
               gpsInstance->mGPSConf.mXtraServer1, sizeof(mXtraConfig.xtra_server_url[0]));
        strncpy(mXtraConfig.xtra_server_url[count++],
               gpsInstance->mGPSConf.mXtraServer2, sizeof(mXtraConfig.xtra_server_url[0]));
        strncpy(mXtraConfig.xtra_server_url[count++],
               gpsInstance->mGPSConf.mXtraServer3, sizeof(mXtraConfig.xtra_server_url[0]));

        count = 0;
        strncpy(mXtraConfig.sntp_server_url[count++],
               gpsInstance->mGPSConf.mNTPServer1, sizeof(mXtraConfig.sntp_server_url[0]));
        strncpy(mXtraConfig.sntp_server_url[count++],
               gpsInstance->mGPSConf.mNTPServer2, sizeof(mXtraConfig.sntp_server_url[0]));
        strncpy(mXtraConfig.sntp_server_url[count++],
               gpsInstance->mGPSConf.mNTPServer3, sizeof(mXtraConfig.sntp_server_url[0]));

        // test need to decide on UA string
        strncpy(mXtraConfig.user_agent_string,
               "LE/1.2.3/OEM/Model/Board/Carrier", sizeof(mXtraConfig.user_agent_string));

        rc = nyx_gps_init_xtra_client(mNyxGpsSystem, &mXtraConfig,
                                      &mXtraClientCallbacks);
        if (NYX_ERROR_NONE != rc) {
            printf_warning("failed to initialize xtra: error = %d\n", rc);
            // use default xtra mechanism
            mXtraDefault = true;
        }
    }
    return rc;

}

void GPSNyxInterface::deInitialize() {
    nyx_gps_cleanup(mNyxGpsSystem);
    nyx_gps_stop_xtra_client(mNyxGpsSystem);
    nyx_device_close(mNyxGpsSystem);
    printf_debug("closed nyx gps module\n");
}

nyx_error_t GPSNyxInterface::startGPS() {
    return nyx_gps_start(mNyxGpsSystem);
}

nyx_error_t GPSNyxInterface::stopGPS() {
    return nyx_gps_stop(mNyxGpsSystem);
}

nyx_error_t GPSNyxInterface::setPositionMode(nyx_gps_position_mode_t mode,
                                             nyx_gps_position_recurrence_t recurrence, uint32_t min_interval,
                                             uint32_t preferred_accuracy, uint32_t preferred_time) {
    return nyx_gps_set_position_mode(mNyxGpsSystem, mode, recurrence,
                                     min_interval, 0, 0);

}

nyx_error_t GPSNyxInterface::setServerInfo(nyx_agps_type_t type,
                                           const char *hostname, int port) {
    return nyx_gps_set_server(mNyxGpsSystem, type, hostname, port);
}

nyx_error_t GPSNyxInterface::dataConnectionOpen(NetworkInfo *networkInfo) {
    return nyx_gps_data_conn_open(mNyxGpsSystem,
                                  NYX_AGPS_TYPE_SUPL, networkInfo->apn.c_str(), networkInfo->bearerType);
}

nyx_error_t GPSNyxInterface::dataConnectionClosed(nyx_agps_type_t agpsType) {
    return nyx_gps_data_conn_closed(mNyxGpsSystem, agpsType);
}

nyx_error_t GPSNyxInterface::dataConnectionFailed(nyx_agps_type_t agpsType) {
    return nyx_gps_data_conn_failed(mNyxGpsSystem, agpsType);
}

nyx_error_t GPSNyxInterface::injectExtraData(XtraData *xtraData) {
    return nyx_gps_inject_xtra_data(mNyxGpsSystem,
                                    (char *) (xtraData->data.c_str()), xtraData->data.size());
}

nyx_error_t GPSNyxInterface::injectExtraTime(XtraTime *timeInfo) {
    mDownloadNtpDataStatus = IDLE;
    return nyx_gps_inject_time(mNyxGpsSystem, timeInfo->utcTime,
                               timeInfo->timeReference, timeInfo->uncertainty);
}

nyx_error_t GPSNyxInterface::injectExtraTime(int64_t utcTime,
                                             int64_t timeReference, int uncertainty) {
    mDownloadNtpDataStatus = IDLE;
    return nyx_gps_inject_time(mNyxGpsSystem, utcTime, timeReference,
                               uncertainty);
}

nyx_error_t GPSNyxInterface::injectExtraCommand(char *command) {
    return nyx_gps_inject_extra_cmd(mNyxGpsSystem, command, strlen(command));
}

nyx_error_t GPSNyxInterface::updateNetworkAvailablity(
        NetworkInfo *networkInfo) {
    return nyx_gps_update_network_availability(mNyxGpsSystem,
                                               networkInfo->available, networkInfo->apn.c_str());
}

nyx_error_t GPSNyxInterface::getDebugData(char *dest, size_t destLen) {
    if (nullptr == mNyxGpsSystem || nullptr == dest || 0 == destLen)
        return NYX_ERROR_INVALID_VALUE;

    return nyx_gps_get_debug_data(mNyxGpsSystem, dest, destLen);
}

nyx_error_t GPSNyxInterface::deleteAidingData() {
    return nyx_gps_delete_aiding_data(mNyxGpsSystem, NYX_GPS_DELETE_ALL);
}

void GPSNyxInterface::agpsStatusCb(nyx_agps_status_t *status, void *user_data) {
    GPSNyxInterface *gpsNyxInterface = (GPSNyxInterface *) user_data;
    GPSPositionProvider *providerInstance =
            (GPSPositionProvider *) gpsNyxInterface->gpsProviderInstance;

    printf_debug("enter agpsStatusCb\n");

    if (!providerInstance) {
        printf_warning("Invalid input parameters\n");
        return;
    }

    printf_debug("agpsStatusCb called %d\n", status->status);

    pthread_mutex_lock(&providerInstance->mGPSThreadMutex);


        if (NYX_AGPS_REQUEST_DATA_CONN == status->status) {
            providerInstance->mGPSThreadAction = ACTION_OPEN_ATL;
            pthread_cond_signal(&providerInstance->mGPSThreadCond);
        } else if (NYX_AGPS_RELEASE_DATA_CONN == status->status) {
            providerInstance->mGPSThreadAction = ACTION_CLOSE_ATL;
            pthread_cond_signal(&providerInstance->mGPSThreadCond);
        }



    pthread_mutex_unlock(&providerInstance->mGPSThreadMutex);

    switch (status->status) {
        case NYX_AGPS_REQUEST_DATA_CONN:
            printf_info("NYX_AGPS_REQUEST_DATA_CONN\n");
            break;

        case NYX_AGPS_RELEASE_DATA_CONN:
            printf_info("NYX_AGPS_RELEASE_DATA_CONN\n");
            break;

        case NYX_AGPS_DATA_CONNECTED:
            printf_info("NYX_AGPS_DATA_CONNECTED\n");
            break;

        case NYX_AGPS_DATA_CONN_DONE:
            printf_info("NYX_AGPS_DATA_CONN_DONE\n");
            break;

        case NYX_AGPS_DATA_CONN_FAILED:
            printf_info("NYX_AGPS_DATA_CONN_FAILED\n");
            break;
        default:
            printf_info("UNKNOWN AGPS State\n");
            break;

    }
}

void GPSNyxInterface::gpsLocationCb(nyx_gps_location_t *positiondata, void *user_data) {
    GPSNyxInterface *gpsNyxInterface = (GPSNyxInterface *) user_data;
    GPSPositionProvider *providerInstance =
            (GPSPositionProvider *) gpsNyxInterface->gpsProviderInstance;

    printf_debug("enter gpsLocationCb\n");

    if (!providerInstance || !positiondata) {
        printf_warning("Invalid input parameters\n");
        return;
    }

    if (providerInstance->mCEPLogEnable) {
        GEOCoordinates measured = {positiondata->latitude,
                                   positiondata->longitude};
        loc_geometry_rtcep_update(&providerInstance->mCEPCalculator, &measured);
    }

    // Set the accuracy to detailed
    providerInstance->gpsAccuracySetDetails(positiondata->accuracy, positiondata->vertical_accuracy);

    memcpy(&gpsNyxInterface->mPosition, positiondata,
           sizeof(nyx_gps_location_t));

    {
        double hor_acc = DEFAULT_VALUE;
        double vert_acc = INVALID_PARAM;

        providerInstance->gpsAccuracyGetDetails(&hor_acc, &vert_acc);

        if (false == providerInstance->mTTFFState)
           {
            struct timeval tv;
            gettimeofday(&tv, (struct timezone *) NULL);
            providerInstance->mLastFixTime = tv.tv_sec * 1000LL + tv.tv_usec / 1000;
            providerInstance->mTTFFState = true;
        }


        set_store_position(positiondata->timestamp, positiondata->latitude,
                           positiondata->longitude, positiondata->altitude,
                           positiondata->speed, positiondata->bearing, hor_acc, vert_acc,
                           LOCATION_DB_PREF_PATH_GPS);

        GeoLocation geoLocation(positiondata->latitude,
                positiondata->longitude, positiondata->altitude, hor_acc,
                positiondata->timestamp, positiondata->bearing, -1,
                positiondata->speed, vert_acc);
        if (providerInstance->mAPIProgressFlag & LOCATION_UPDATES_ON)
            providerInstance->getCallback()->getLocationUpdateCb(geoLocation,
                ERROR_NONE, HandlerTypes::HANDLER_GPS);
    }
}

void GPSNyxInterface::gpsStatusCb(nyx_gps_status_t *gps_status, void *user_data) {
    printf_debug("enter gpsStatusCb...\n");

    GPSNyxInterface *gpsNyxInterface = (GPSNyxInterface *) user_data;
    GPSPositionProvider *providerInstance =
            (GPSPositionProvider *) gpsNyxInterface->gpsProviderInstance;
    GPSStatus status;

    if (!providerInstance || !gps_status) {
        printf_warning("Invalid input parameters\n");
        return;
    }

    printf_debug("gpsStatusCb called %d\n", gps_status->status);

    status = providerInstance->mGPSStatus;

    switch (gps_status->status) {
        case NYX_GPS_STATUS_NONE:
            printf_info("GPS STATUS NONE\n");
            break;

        case NYX_GPS_STATUS_SESSION_BEGIN:
            printf_info("GPS STATUS SESSION BEGIN\n");
            break;

        case NYX_GPS_STATUS_SESSION_END:
            printf_info("GPS STATUS SESSION END\n");
            break;

        case NYX_GPS_STATUS_ENGINE_ON:
            printf_info("GPS STATUS ENGINE ON\n");
            status = GPS_STATUS_AVAILABLE;
            break;

        case NYX_GPS_STATUS_ENGINE_OFF:
            printf_info("GPS STATUS ENGINE OFF\n");
            status = GPS_STATUS_UNAVAILABLE;
            break;

        default:
            printf_info("GPS STATUS NONE\n");
            break;
    }

    if (providerInstance->mGPSStatus != status) {
        providerInstance->mGPSStatus = status;
        providerInstance->getCallback()->getGpsStatusCb(status);
    }
}

void GPSNyxInterface::gpsSvStatusCb(nyx_gps_sv_status_t *sat_data, void *user_data) {
    printf_debug("enter gpsSvStatusCb...\n");

    GPSNyxInterface *gpsNyxInterface = (GPSNyxInterface*) user_data;
    GPSPositionProvider *providerInstance =
            (GPSPositionProvider *) gpsNyxInterface->gpsProviderInstance;

    if (!providerInstance || !sat_data) {
        printf_warning("Invalid input parameters\n");
        return;
    }

    memcpy(&gpsNyxInterface->mSatellite, sat_data, sizeof(nyx_gps_sv_status_t));

    {
        int index = DEFAULT_VALUE;
        Satellite *sat = NULL;
        sat = satellite_create(sat_data->num_svs);

        sat->visible_satellites_count = sat_data->num_svs;
        LS_LOG_DEBUG(" number of satellite %d : \n", sat_data->num_svs);

        for (index = DEFAULT_VALUE; index < sat_data->num_svs; index++) {
            bool used = FALSE;
            bool hasephemeris = FALSE;
            bool hasalmanac = FALSE;

            gint prn = (gint)sat_data->sv_list[index].prn;
            gdouble snr = (gdouble)sat_data->sv_list[index].snr;
            gdouble elev = (gdouble)sat_data->sv_list[index].elevation;
            gdouble azim = (gdouble)sat_data->sv_list[index].azimuth;

            ((sat_data->used_in_fix_mask & (1 << (prn - 1)))) == DEFAULT_VALUE ?
                    (used = FALSE) : (used = TRUE);
            ((sat_data->ephemeris_mask & (1 << (prn - 1)))) == DEFAULT_VALUE ?
                    (hasephemeris = FALSE) : (hasephemeris = TRUE);
            ((sat_data->almanac_mask & (1 << (prn - 1)))) == DEFAULT_VALUE ?
                    (hasalmanac = FALSE) : (hasalmanac = TRUE);

            set_satellite_details(sat, index, snr, prn, elev, azim, used,
                    hasalmanac, hasephemeris);
        }

        //call satellite cb
        if (sat_data->num_svs > DEFAULT_VALUE) {
            if (providerInstance->mAPIProgressFlag & SATELLITE_GET_DATA_ON)
                providerInstance->getCallback()->getGpsSatelliteDataCb(sat);
        }

        satellite_free(sat);
    }
}

void GPSNyxInterface::nfwNotifyCb(nyx_gps_nfw_notification_t *notification,
                                  void *user_data) {
    GPSNyxInterface *self = (GPSNyxInterface *) user_data;

    if (nullptr == notification || nullptr == self)
        return;

    GPSPositionProvider *providerInstance =
            (GPSPositionProvider *) self->gpsProviderInstance;

    if (nullptr == providerInstance || nullptr == providerInstance->getCallback())
        return;

    providerInstance->getCallback()->nfwNotifyCb(notification);
}

void GPSNyxInterface::gpsNmeaCb(int64_t timestamp, const char *nmea, int length, void *user_data) {
    GPSNyxInterface *gpsNyxInterface = (GPSNyxInterface *) user_data;
    GPSPositionProvider *providerInstance =
            (GPSPositionProvider *) gpsNyxInterface->gpsProviderInstance;
    printf_debug("enter gpsNmeaCb\n");

    if (!providerInstance || !nmea) {
        printf_warning("Invalid input parameters\n");
        return;
    }

    if (providerInstance->mNMEALogEnable)
        loc_logger_feed_data(&providerInstance->mNMEALogger, (char *) nmea,
                             length);

    if (providerInstance->mAPIProgressFlag & NMEA_GET_DATA_ON)
        providerInstance->getCallback()->getNmeaDataCb(timestamp, (char*) nmea, length);
}

void GPSNyxInterface::gpsSetCapabilitiesCb(uint32_t capabilities, void *user_data) {
    GPSNyxInterface *gpsNyxInterface = (GPSNyxInterface *) user_data;
    GPSPositionProvider *providerInstance =
            (GPSPositionProvider *) gpsNyxInterface->gpsProviderInstance;

    printf_debug("enter gpsSetCapabilitiesCb\n");
    printf_debug("Capabilities: 0x%x\n", capabilities);

    providerInstance->mGEngineCapabilities = capabilities;
}

void GPSNyxInterface::gpsAcquireWakelockCb(void *user_data) {
    printf_debug("enter gpsAcquireWakelockCb\n");
}

void GPSNyxInterface::gpsReleaseWakelockCb(void *user_data) {
    printf_debug("enter gpsReleaseWakelockCb\n");
}

void GPSNyxInterface::gpsRequestUtcTimeCb(void *user_data) {
    GPSNyxInterface *gpsNyxInterface = (GPSNyxInterface *) user_data;
    GPSPositionProvider *providerInstance =
            (GPSPositionProvider *) gpsNyxInterface->gpsProviderInstance;
    printf_debug("enter gpsRequestUtcTimeCb\n");

    if (DOWNLOADING == gpsNyxInterface->mDownloadNtpDataStatus)
        return;

    if (gpsNyxInterface->mXtraDefault) {
        gpsNyxInterface->mDownloadNtpDataStatus = DOWNLOADING;
        gpsNyxInterface->client.start(&providerInstance->mGPSConf, gpsNyxInterface);

    } else {
        gpsNyxInterface->mDownloadNtpDataStatus = DOWNLOADING;

        if (NYX_ERROR_NONE != nyx_gps_download_ntp_time(gpsNyxInterface->mNyxGpsSystem))
            printf_warning("failed to request downloading ntp time\n");
    }
}

void GPSNyxInterface::gpsXtraDownloadRequestCb(void *user_data) {
    GPSNyxInterface *gpsNyxInterface = (GPSNyxInterface *) user_data;
    GPSPositionProvider *providerInstance =
            (GPSPositionProvider *) gpsNyxInterface->gpsProviderInstance;

    printf_debug("enter gpsXtraDownloadRequestCb\n");

    if (DOWNLOADING == providerInstance->mDownloadXtraDataStatus)
        return;

    if (gpsNyxInterface->mXtraDefault) {
	    GThread *downloadThread = g_thread_new("download xtra", (GThreadFunc)xtraDataDownloadThread, user_data);
	    if (!downloadThread) {
		    printf_warning("failed to create xtra download thread\n");
	    }
	    else
	    {
                   g_thread_unref(downloadThread);
            }
     }
    else {
        providerInstance->mDownloadXtraDataStatus = DOWNLOADING;

        if (NYX_ERROR_NONE != nyx_gps_download_xtra_data(gpsNyxInterface->mNyxGpsSystem))
            printf_warning("failed to request downloading xtra\n");
    }
}

bool GPSNyxInterface::geofenceRemove(int32_t geofenceId) {
    if (NYX_ERROR_NONE
        != nyx_gps_remove_geofence_area(mNyxGpsSystem, geofenceId))
        return false;

    return true;
}

bool GPSNyxInterface::geofencePause(int32_t geofenceId, GError **error) {
    if (NYX_ERROR_NONE != nyx_gps_pause_geofence(mNyxGpsSystem, geofenceId))
        return false;

    return true;
}

bool GPSNyxInterface::geofenceResume(int32_t geofenceId,
                                     int32_t monitorTransition, GError **error) {
    if (NYX_ERROR_NONE
        != nyx_gps_resume_geofence(mNyxGpsSystem, geofenceId,
                                   NYX_GEOFENCER_ENTERED | NYX_GEOFENCER_EXITED
                                   | NYX_GEOFENCER_UNCERTAIN))
        return false;

    return true;
}

bool GPSNyxInterface::geofenceAdd(int32_t geofenceId, double latitude,
                                  double longitude, double radius) {
    printf_info("geofenceAdd = %d\n", geofenceId);

    if (NYX_ERROR_NONE
        != nyx_gps_add_geofence_area(mNyxGpsSystem, geofenceId, latitude,
                                     longitude, radius, NYX_GEOFENCER_UNCERTAIN,
                                     NYX_GEOFENCER_ENTERED | NYX_GEOFENCER_EXITED
                                     | NYX_GEOFENCER_UNCERTAIN, 60000, 0))
        return false;

    return true;
}

/*The callback called twhen there is a transition to report for the specific geofence*/
void GPSNyxInterface::geofenceBreachCb(int32_t geofenceId,nyx_gps_location_t *location,
                                             int32_t transition, int64_t timestamp,
                                             void *userData)
{
    GPSNyxInterface *gpsNyxInterface = (GPSNyxInterface*) userData;
    GPSPositionProvider *providerInstance =(GPSPositionProvider *) gpsNyxInterface->gpsProviderInstance;
    int whichWay = 0;

    printf_debug("enter geofenceBreachCb\n");

    if (!providerInstance) {
        printf_warning("Invalid input parameters\n");
        return;
    }

    switch (transition) {
        case NYX_GEOFENCER_ENTERED:
            printf_info("GPS GEOFENCE ENTERED\n");
            whichWay = 0;
            break;
        case NYX_GEOFENCER_EXITED:
            printf_info("GPS GEOFENCE EXITED\n");
            whichWay = 1;
            break;
        case NYX_GEOFENCER_UNCERTAIN:
            printf_info("GPS GEOFENCE UNCERTAIN\n");
            whichWay = 2;
            break;
        default:
            printf_warning("unknown transition\n");
            break;
    }
    if (providerInstance->getCallback())
        providerInstance->getCallback()->geofenceBreachCb(geofenceId, whichWay, timestamp, location->latitude, location->longitude, userData);
}

/*The callback called to notify the success or failure of the status call*/
void GPSNyxInterface::geofenceStatusCb(int32_t status,nyx_gps_location_t *last_location, void *userData)
{
    GPSNyxInterface *gpsNyxInterface = (GPSNyxInterface*) userData;
    GPSPositionProvider *providerInstance = (GPSPositionProvider *) gpsNyxInterface->gpsProviderInstance;

    LS_LOG_DEBUG("enter geofenceStatusCb\n");

    if (!providerInstance) {
        LS_LOG_DEBUG("Invalid input parameters\n");
        return;
    }

    LS_LOG_DEBUG("geofenceStatusCb emitting\n %d", status);

    providerInstance->mGeofenceStatus = status;
}

/*The callback called to notify the success or failure of the add call*/
void GPSNyxInterface::geofenceAddCb(int32_t geofenceId, int32_t status,void *userData)
{
    GPSNyxInterface *gpsNyxInterface = (GPSNyxInterface*) userData;
    GPSPositionProvider *providerInstance =(GPSPositionProvider *) gpsNyxInterface->gpsProviderInstance;
    LS_LOG_DEBUG("enter geofenceAddCb\n");

    if (!providerInstance) {
        LS_LOG_DEBUG("Invalid input parameters\n");
        return;
    }

    LS_LOG_DEBUG("geofenceAddCb emitting %d\n", geofenceId);

    if (providerInstance->getCallback()) {
         providerInstance->getCallback()->geofenceAddCb(geofenceId, status, userData);
    }
}

/*The callback called to notify the success or failure of the remove call*/
void GPSNyxInterface::geofenceRemoveCb (int32_t geofenceId, int32_t status, void *userData)
{
    GPSNyxInterface *gpsNyxInterface = (GPSNyxInterface*) userData;
    GPSPositionProvider *providerInstance =(GPSPositionProvider *)gpsNyxInterface->gpsProviderInstance;

    LS_LOG_DEBUG("enter geofenceRemoveCb\n");

    if (!providerInstance) {
        LS_LOG_DEBUG("Invalid input parameters\n");
        return;
    }

    if (providerInstance->getCallback())
        providerInstance->getCallback()->geofenceRemoveCb(geofenceId, status, userData);
}

/*The callback called to notify the success or failure of the pause call*/
void GPSNyxInterface::geofencePauseCb(int32_t geofenceId, int32_t status,void *userData)
{
    GPSNyxInterface *gpsNyxInterface = (GPSNyxInterface*) userData;
    GPSPositionProvider *providerInstance = (GPSPositionProvider *) gpsNyxInterface->gpsProviderInstance;

    LS_LOG_DEBUG("enter geofencePauseCb\n");

    if (!providerInstance) {
        LS_LOG_DEBUG("Invalid input parameters\n");
        return;
    }

    LS_LOG_DEBUG("geofencePauseCb emitting\n");
}

/*The callback called to notify the success or failure of the resume call*/
void GPSNyxInterface::geofenceResumeCb(int32_t geofenceId, int32_t status,void *userData)
{
    GPSNyxInterface *gpsNyxInterface = (GPSNyxInterface*) userData;
    GPSPositionProvider *providerInstance = (GPSPositionProvider *) gpsNyxInterface->gpsProviderInstance;

    LS_LOG_DEBUG("enter geofenceResumeCb\n");

    if (!providerInstance) {
        printf_warning("Invalid input parameters\n");
        return;
    }

    printf_debug("geofenceResumeCb emitting\n");
}

void GPSNyxInterface::xtraDataCb(char *data, int length) {
    GPSPositionProvider *gpsService = GPSPositionProvider::getInstance();

    printf_debug("enter xtraDataCb\n");

    pthread_mutex_lock(&gpsService->mGPSThreadMutex);
    
    if(data != nullptr) {
    	gpsService->mXtraData.data = data;
    }

    gpsService->mGPSThreadAction = ACTION_XTRA_DATA;

    // Signal data to gps_thread that data is ready
    pthread_cond_signal(&gpsService->mGPSThreadCond);

    pthread_mutex_unlock(&gpsService->mGPSThreadMutex);
}

void GPSNyxInterface::xtraTimeCb(int64_t utcTime, int64_t timeReference,
                                 int uncertainty) {
    GPSPositionProvider *gpsService = GPSPositionProvider::getInstance();

    printf_debug("enter xtraTimeCb\n");

    pthread_mutex_lock(&gpsService->mGPSThreadMutex);

    gpsService->mXtraTime.utcTime = utcTime;
    gpsService->mXtraTime.timeReference = timeReference;
    gpsService->mXtraTime.uncertainty = uncertainty;

    gpsService->mGPSThreadAction = ACTION_XTRA_TIME;

    // Signal data to gps_thread that data is ready
    pthread_cond_signal(&gpsService->mGPSThreadCond);
    pthread_mutex_unlock(&gpsService->mGPSThreadMutex);
}

void GPSNyxInterface::xtraDataDownloadThread(void *arg) {
    int nextserverindex = 0;
    int noofservers = 3;
    int count = 0;
    char *xtraservers[noofservers];
    const char *ACCEPT =
            "Accept:, application/vnd.wap.mms-message, application/vnd.wap.sic";
    const char *XWAP_PROFILE =
            "x-wap-profile: http://www.openmobilealliance.org/tech/profiles/UAPROF/ccppschema-20021212#";
    const char *headers[2];
    GPSNyxInterface *gpsNyxInterface = (GPSNyxInterface *) arg;
    GPSPositionProvider *providerInstance =
            (GPSPositionProvider *) gpsNyxInterface->gpsProviderInstance;

    /* Perform the request, res will get the return code */
    xtraservers[count++] = providerInstance->mGPSConf.mXtraServer1;

    xtraservers[count++] = providerInstance->mGPSConf.mXtraServer2;

    xtraservers[count++] = providerInstance->mGPSConf.mXtraServer3;

    providerInstance->mDownloadXtraDataStatus = DOWNLOADING;

    headers[0] = ACCEPT;
    headers[1] = XWAP_PROFILE;

    if (!providerInstance->mHttpReqTask) {
        providerInstance->mHttpReqTask = loc_http_task_create(headers, 2);
        providerInstance->mHttpReqTask->responseData = nullptr;
    }

    while (!providerInstance->mHttpReqTask->responseData) {

        if (loc_http_task_prepare_connection(&providerInstance->mHttpReqTask,
                                             xtraservers[nextserverindex])) {

            if (loc_http_add_request(providerInstance->mHttpReqTask, true)) {

                if (!providerInstance->mHttpReqTask->curlDesc.curlResultCode) {
                    printf_debug("curl connection failed: %d\n",
                                 providerInstance->mHttpReqTask->curlDesc.curlResultCode);
                }

                if (providerInstance->mHttpReqTask->curlDesc.httpResponseCode
                    != HTTP_SUCESS_STATUS_CODE) {
                    printf_info(
                            "curl connection succeded: but http status code:%ld\n",
                            providerInstance->mHttpReqTask->curlDesc.httpResponseCode);
                }
            }

            // increment NextServerIndex and wrap around if necessary
            nextserverindex++;

            if (nextserverindex == count)
                break; //tried all the servers breaking
        }
    }

    if (NYX_ERROR_NONE
        != nyx_gps_inject_xtra_data(gpsNyxInterface->mNyxGpsSystem,
                                    providerInstance->mHttpReqTask->responseData,
                                    providerInstance->mHttpReqTask->responseSize))
        printf_warning("failed to inject xtra data\n");

    if (providerInstance->mHttpReqTask->responseData) {
        g_free(providerInstance->mHttpReqTask->responseData);
        providerInstance->mHttpReqTask->responseData = nullptr;
    }

    providerInstance->mDownloadXtraDataStatus = IDLE;
}

void GPSNyxInterface::onRequestCompleted(NtpErrors error, const NTPData *data) {

    if(error==NtpErrors::NONE && data!= nullptr)
    if (injectExtraTime(
            data->getNtpTime(),
            data->getNtpTimeReference(),
            data->getRoundTripTime()) != NYX_ERROR_NONE)
        printf_warning("failed to inject time\n");
    mDownloadNtpDataStatus = IDLE;

}
