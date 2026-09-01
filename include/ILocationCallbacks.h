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


#ifndef ILOCATIONCALLBACKS_H_
#define ILOCATIONCALLBACKS_H_

#include <glib.h>
#include <GeoLocation.h>
#include <location_errors.h>
#include <Location.h>
#include <nyx/common/nyx_gps_common.h>

class ILocationCallbacks {
public:
    virtual void getLocationUpdateCb(GeoLocation& location, ErrorCodes errCode,
            HandlerTypes type)=0;

    virtual void getNmeaDataCb(long long timestamp, char *data, int length)=0;

    virtual void getGpsStatusCb(int state)=0;

    virtual void getGpsSatelliteDataCb(Satellite *)=0;

    /*
     * A non-framework entity - carrier, modem, or GNSS chip vendor - asked for
     * the device's location. This reports what already happened and takes no
     * response, unlike a network-initiated request.
     */
    virtual void nfwNotifyCb(nyx_gps_nfw_notification_t *notification)=0;

    virtual void geofenceAddCb(int32_t geofence_id, int32_t status,
            gpointer user_data)=0;

    virtual void geofenceRemoveCb(int32_t geofence_id, int32_t status,
            gpointer user_data)=0;

    virtual void geofencePauseCb(int32_t geofence_id, int32_t status,
            gpointer user_data)=0;

    virtual void geofenceResumeCb(int32_t geofence_id, int32_t status,
            gpointer user_data)=0;

    virtual void geofenceBreachCb(int32_t geofence_id, int32_t status,
            int64_t timestamp, double latitude, double longitude,
            gpointer user_data)=0;

    virtual void geofenceStatusCb(int32_t status, Position *last_position,
            Accuracy *accuracy, gpointer user_data)=0;
    virtual ~ILocationCallbacks() {
    }
};
#endif /* ILOCATIONCALLBACKS_H_ */
