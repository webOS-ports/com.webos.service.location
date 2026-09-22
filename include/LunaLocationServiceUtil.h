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


#ifndef _LUNA_LOCATION_SERVICE_UTIL_H_
#define _LUNA_LOCATION_SERVICE_UTIL_H_

#include <luna-service2/lunaservice.h>
#include "JsonUtility.h"
// temproray so keeping hardcoded value

#define SECURE_PAYLOAD_NW_GET      "{\"keyname\":\"com.webos.service.location.geolocation\"}"
#define SECURE_PAYLOAD_LBS_GET     "{\"keyname\":\"com.webos.service.location.geocode\"}"
#define SECURE_STORAGE_KEY_STORAGE_GET "luna://com.webos.service.securestorage/keystorageget"

/*
 * JSON SCHEMA: getAllLocationHandlers ()
 */
#define JSCHEMA_GET_ALL_LOCATION_HANDLERS                   STRICT_SCHEMA(\
        PROPS_1(PROP(subscribe, boolean)))

/*
 * JSON SCHEMA: getGeoCodeLocation (string address) - syntax 1
 */
#define JSCHEMA_GET_GEOCODE_LOCATION_1                      STRICT_SCHEMA(\
        PROPS_1(PROP(address, string)) \
        REQUIRED_1(address))

/*
 * JSON SCHEMA: getGeoCodeLocation ([string street], [string locality],
 *                                  [string area], [string region],
 *                                  [string country], [string countrycode],
 *                                  [string postcode]) - syntax 2
 */
#define JSCHEMA_GET_GEOCODE_LOCATION_2                      STRICT_SCHEMA(\
        PROPS_7(PROP(street, string), PROP(locality, string), \
        PROP(area, string), PROP(region, string), \
        PROP(country, string), PROP(countrycode, string), \
        PROP(postcode, string)))

/*
 * JSON SCHEMA: getGeoCodeLocation (string address) - syntax 1
 */

#define JSCHEMA_GET_GOOGLE_GEOCODE_LOCATION                 STRICT_SCHEMA(\
        PROPS_5(\
            PROP(address, string), \
            OBJECT(components, STRICT_SCHEMA(\
                PROPS_5(\
                    PROP(route, string), \
                    PROP(locality, string), \
                    PROP(administrative_area, string), \
                    PROP(postal_code, string), \
                    PROP(country, string)\
                )\
            )), \
            OBJECT(bounds, STRICT_SCHEMA(\
                PROPS_4(\
                    PROP_WITH_OPT(southwestLat, number, "minimum":-90, "maximum":90), \
                    PROP_WITH_OPT(southwestLon, number, "minimum":-180, "maximum":180), \
                    PROP_WITH_OPT(northeastLat, number, "minimum":-90, "maximum":90), \
                    PROP_WITH_OPT(northeastLon, number, "minimum":-180, "maximum":180)\
                ) \
                REQUIRED_4(southwestLat, southwestLon, northeastLat, northeastLon)\
            )), \
            PROP(language, string), \
            PROP(region, string)\
        ))

/*
* JSON SCHEMA: getReverseLocation
*/
#define JSCHEMA_GET_GOOGLE_REVERSE_LOCATION                 STRICT_SCHEMA(\
        PROPS_5(\
            PROP_WITH_OPT(latitude, number, "minimum":-90, "maximum":90), \
            PROP_WITH_OPT(longitude, number, "minimum":-180, "maximum":180), \
            PROP(language, string), \
            STRICT_ENUM_ARRAY(result_type, string, \
                "street_address", \
                "route", \
                "intersection", \
                "political", \
                "country", \
                "administrative_area_level_1", \
                "administrative_area_level_2", \
                "administrative_area_level_3", \
                "administrative_area_level_4", \
                "administrative_area_level_5", \
                "colloquial_area", \
                "locality", \
                "ward", \
                "sublocality", \
                "neighborhood", \
                "premise", \
                "subpremise", \
                "postal_code", \
                "natural_feature", \
                "airport", \
                "park", \
                "point_of_interest", \
                "floor", \
                "establishment", \
                "parking", \
                "post_box", \
                "postal_town", \
                "room", \
                "street_number", \
                "bus_station", \
                "train_station", \
                "transit_station"\
            ), \
            STRICT_ENUM_ARRAY(location_type, string, \
                "ROOFTOP", \
                "RANGE_INTERPOLATED", \
                "GEOMETRIC_CENTER", \
                "APPROXIMATE"\
            )\
        ) \
        REQUIRED_2(latitude, longitude))

/*
 * JSON SCHEMA: getGpsSatelliteData ([bool subscribe])
 */
#define JSCHEMA_GET_GPS_SATELLITE_DATA                      STRICT_SCHEMA(\
        PROPS_1(PROP(subscribe, boolean)))

/*
 * JSON SCHEMA: getGpsStatus ([bool subscribe])
 */
#define JSCHEMA_GET_GPS_STATUS                              STRICT_SCHEMA(\
        PROPS_1(PROP(subscribe, boolean)))

/*
 * JSON SCHEMA: getLocationHandlerDetails (string Handler)
 */
#define JSCHEMA_GET_LOCATION_HANDLER_DETAILS                STRICT_SCHEMA(\
        PROPS_1(ENUM_PROP(Handler, string, "gps", "network", "passive", "hybrid")) \
        REQUIRED_1(Handler))

/*
 * JSON SCHEMA: getNmeaData ([bool subscribe])
 */
#define JSCHEMA_GET_NMEA_DATA                               STRICT_SCHEMA(\
        PROPS_1(PROP(subscribe, boolean)))

/*
 * JSON SCHEMA: getReverseLocation (double latitude, double longitude)
 */
#define JSCHEMA_GET_REVERSE_LOCATION                        STRICT_SCHEMA(\
        PROPS_2(PROP_WITH_OPT(latitude, number, "minimum":-90, "maximum":90), \
        PROP_WITH_OPT(longitude, number, "minimum":-180, "maximum":180)) \
        REQUIRED_2(latitude, longitude))

/*
 * JSON SCHEMA: getNfwNotifications ([bool subscribe])
 */
#define JSCHEMA_GET_NFW_NOTIFICATIONS                       STRICT_SCHEMA(\
        PROPS_1(PROP(subscribe, boolean)))

/*
 * JSON SCHEMA: getGpsDebugData ()
 */
#define JSCHEMA_GET_GPS_DEBUG_DATA                          SCHEMA_ANY

/*
 * JSON SCHEMA: getState (string Handler, [bool subscribe])
 */
#define JSCHEMA_GET_STATE                                   STRICT_SCHEMA(\
        PROPS_2(ENUM_PROP(Handler, string, "gps", "network", "passive", "hybrid"), PROP(subscribe, boolean)) \
        REQUIRED_1(Handler))

/*
 * JSON SCHEMA: getTimeToFirstFix ()
 */
#define JSCHEMA_GET_TIME_TO_FIRST_FIX                       SCHEMA_ANY

/*
 * JSON SCHEMA: sendExtraCommand (string command)
 */
#define JSCHEMA_SEND_EXTRA_COMMAND                          STRICT_SCHEMA(\
        PROPS_1(PROP(command, string)) \
        REQUIRED_1(command))

/*
 * JSON SCHEMA: setGPSParameters ([int posmode], [string supladdress],
 *                                [int suplport], [int fixinterval])
 */
#define JSCHEMA_SET_GPS_PARAMETERS                          STRICT_SCHEMA(\
        PROPS_4(ENUM_PROP(posmode, integer, 0, 1, 2), PROP(supladdress, string), \
        PROP(suplport, integer), PROP(fixinterval, integer)))

/*
 * JSON SCHEMA: setState (string Handler, bool state)
 */
#define JSCHEMA_SET_STATE                                   STRICT_SCHEMA(\
        PROPS_2(ENUM_PROP(Handler, string, "gps", "network", "passive", "hybrid"), PROP(state, boolean)) \
        REQUIRED_2(Handler, state))

/*
 * JSON SCHEMA: stopGPS ()
 */
#define JSCHEMA_STOP_GPS                                    SCHEMA_ANY

/*
 * JSON SCHEMA: addGeofenceArea (double latitude, double longitude, double radius, [bool subscribe])
 */
#define JSCHEMA_ADD_GEOFENCE_AREA                           STRICT_SCHEMA(\
        PROPS_4(PROP_WITH_OPT(latitude, number, "minimum":-90, "maximum":90), \
                PROP_WITH_OPT(longitude, number, "minimum":-180, "maximum":180), \
                PROP_WITH_OPT(radius, number, "minimum":0, "exclusiveMinimum":true), \
                PROP(subscribe, boolean)) \
        REQUIRED_3(latitude, longitude, radius))

/*
 * JSON SCHEMA: removeGeofenceArea (integer geofenceid)
 */
#define JSCHEMA_REMOVE_GEOFENCE_AREA                        STRICT_SCHEMA(\
        PROPS_1(PROP_WITH_OPT(geofenceid, integer, "minimum":0, "exclusiveMinimum":true)) \
        REQUIRED_1(geofenceid))

/*
 * JSON SCHEMA: pauseGeofenceArea (integer geofenceid)
 */
#define JSCHEMA_PAUSE_GEOFENCE_AREA                         STRICT_SCHEMA(\
        PROPS_1(PROP(geofenceid, integer)) \
        REQUIRED_1(geofenceid))

/*
 * JSON SCHEMA: resumeGeofenceArea (integer geofenceid)
 */
#define JSCHEMA_RESUME_GEOFENCE_AREA                        STRICT_SCHEMA(\
        PROPS_1(PROP(geofenceid, integer)) \
        REQUIRED_1(geofenceid))

/*
 * JSON SCHEMA: getLocationUpdates ([bool wakelock],
 *                                  [bool subscribe],
 *                                  [integer minimumInterval],
 *                                  [integer minimumDistance],
 *                                  [integer responseTimeout],
 *                                  [string Handler])
 */

#define JSCEHMA_GET_LOCATION_UPDATES                        STRICT_SCHEMA(\
        PROPS_6(\
            PROP(wakelock, boolean), \
            PROP(subscribe, boolean), \
            PROP_WITH_OPT(minimumInterval, integer, "minimum":0, "maximum":3600000), \
            PROP_WITH_OPT(minimumDistance, integer, "minimum":0, "maximum":60000), \
            PROP_WITH_OPT(responseTimeout, integer, "minimum":0, "maximum":720), \
            ENUM_PROP(Handler, string, "gps", "network", "passive", "hybrid")\
        ))


/*
 * JSON SCHEMA: getCachedPosition ([integer maximumAge], [string Handler])
 */


#define JSCHEMA_GET_CACHED_POSITION                         STRICT_SCHEMA(\
        PROPS_2(\
            PROP_WITH_OPT(maximumAge, integer, "minimum":0, "exclusiveMinimum": true), \
            ENUM_PROP(Handler, string, "gps", "network", "passive", "hybrid")))


bool LSMessageInitErrorReply();

void LSMessageReleaseErrorReply();

char *LSMessageGetErrorReply(int errorCode);

void LSMessageReplyError(LSHandle *sh, LSMessage *message, int errorCode);
void LSMessageReplyCustomError(LSHandle *sh, LSMessage *message, int errorCode);
bool LSMessageReplySubscriptionSuccess(LSHandle *sh, LSMessage *message);
void LSMessageReplySuccess(LSHandle *sh, LSMessage *message);
bool LSMessageValidateSchemaReplyOnError(LSHandle *sh, LSMessage *message,const char *schema, jvalue_ref *parsedObj);

bool securestorage_get(LSHandle *sh, void *ptr);
Position *position_create(gint64 timestamp,
                          gdouble latitude,
                          gdouble longitude,
                          gdouble altitude,
                          gdouble speed,
                          gdouble direction,
                          gdouble climb,
                          int flags);

#endif
