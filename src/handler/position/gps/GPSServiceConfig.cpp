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


#include <nyx/common/nyx_gps_common.h>
#include <gps_cfg.h>
#include <string.h>
#include <glib.h>
#include <GPSServiceConfig.h>

using namespace std;

#define SUPL_VERSION    0x20000
#define    SUPL_HOST        "supl.google.com"
#define    SUPL_PORT        7276
#define    XTRASERVER1        "http://xtra1.gpsonextra.net/xtra2.bin"
#define    XTRASERVER2     "http://xtra2.gpsonextra.net/xtra2.bin"
#define    XTRASERVER3     "http://xtra3.gpsonextra.net/xtra2.bin"
#define    NTPSERVER1      "time.gpsonextra.net"
#define    NTPSERVER2        "asia.pool.ntp.org"
#define    NTPSERVER3        "0.kr.pool.ntp.org"
#define    VENDOR            "undefined"
#define    LGETLSMODE        0
#define    LGEPOSITIONMODE    NYX_GPS_POSITION_MODE_MS_BASED
#define    CHIPSETID        "Main"

void GPSServiceConfig::loadDefaults() {
    mSUPLVer = SUPL_VERSION;
    g_strlcpy(mSUPLHost, SUPL_HOST, sizeof(mSUPLHost));
    mSUPLPort = SUPL_PORT;

    g_strlcpy(mXtraServer1, XTRASERVER1, sizeof(mXtraServer1));
    g_strlcpy(mXtraServer2, XTRASERVER2, sizeof(mXtraServer2));
    g_strlcpy(mXtraServer3, XTRASERVER3, sizeof(mXtraServer3));
    g_strlcpy(mNTPServer1, NTPSERVER1, sizeof(mNTPServer1));
    g_strlcpy(mNTPServer2, NTPSERVER2, sizeof(mNTPServer2));
    g_strlcpy(mNTPServer3, NTPSERVER3, sizeof(mNTPServer3));
    g_strlcpy(mVENDOR, VENDOR, sizeof(mVENDOR));
    mLgeTlsMode = LGETLSMODE;
    mLgeGPSPositionMode = LGEPOSITIONMODE;
    g_strlcpy(mChipsetID, CHIPSETID, sizeof(mChipsetID));


}

void GPSServiceConfig::loadConfigFile(std::string configFileName) {
    gps_param_s_type gps_cfg_parameter_table[] = {
            {"SUPL_VER",              &mSUPLVer,            nullptr, 'n'},
            {"SUPL_HOST",             &mSUPLHost,           nullptr, 's'},
            {"SUPL_PORT",             &mSUPLPort,           nullptr, 'n'},
            {"XTRA_SERVER_1",         &mXtraServer1,        nullptr, 's'},
            {"XTRA_SERVER_2",         &mXtraServer2,        nullptr, 's'},
            {"XTRA_SERVER_3",         &mXtraServer3,        nullptr, 's'},
            {"NTP_SERVER_1",          &mNTPServer1,         nullptr, 's'},
            {"NTP_SERVER_2",          &mNTPServer2,         nullptr, 's'},
            {"NTP_SERVER_3",          &mNTPServer3,         nullptr, 's'},
            {"VENDOR",                &mVENDOR,             nullptr, 's'},
            {"LGE_TLS_MODE",          &mLgeTlsMode,         nullptr, 'n'},
            {"LGE_GPS_POSITION_MODE", &mLgeGPSPositionMode, nullptr, 'n'},
            {"CHIPSET_ID",            &mChipsetID,          nullptr, 's'}
    };

    GPS_READ_CONF(configFileName.c_str(), gps_cfg_parameter_table);
}
