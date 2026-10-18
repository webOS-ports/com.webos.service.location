// Copyright (c) 2026 LuneOS project
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

#ifndef _CONNMGR_STATUS_H_
#define _CONNMGR_STATUS_H_

/*
 * The data network as com.webos.service.connectionmanager/getStatus reports
 * it, in the terms the GNSS HAL takes it in (IAGnssRil::updateNetworkState).
 *
 * The types are the values of NYX_AGPS_RIL_NETWORK_TYPE_MOBILE and _WIFI,
 * which are Android's ConnectivityManager types. The HAL has no type for a
 * wired link; what a GNSS stack does with the type is decide whether a
 * download is cheap, so a wired link counts as Wi-Fi.
 */
enum ConnmgrNetworkType {
    CONNMGR_NETWORK_MOBILE = 0,
    CONNMGR_NETWORK_WIFI = 1,
};

/*
 * Fills connected from "isInternetConnectionAvailable" and type from the
 * link carrying it: a connected "wifi" or "wired" is WIFI, otherwise MOBILE
 * (the cellular "wan", or nothing). False, leaving both untouched, when the
 * payload is not a successful getStatus reply with that boolean.
 */
bool connmgrParseDataNetwork(const char *payload, bool *connected, int *type);

#endif
