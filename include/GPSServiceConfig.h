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


#ifndef GPSSERVICECONFIG_H_
#define GPSSERVICECONFIG_H_

using namespace std;

#include <string>
#include <gps_cfg.h>

#define    GPS_CONF_FILE    "/etc/location/gpsConfig.conf"

class GPSServiceConfig {
public:
    GPSServiceConfig() {
        loadDefaults();
        loadConfigFile(GPS_CONF_FILE);
    };

    GPSServiceConfig(std::string configFileName) {
        loadDefaults();
        loadConfigFile(configFileName);
    };

    void loadDefaults();

private:
    void loadConfigFile(std::string ConfigFileName);

public:
    unsigned long mSUPLVer;
    char mSUPLHost[GPS_MAX_PARAM_STRING];
    unsigned long mSUPLPort;
    char mXtraServer1[GPS_MAX_PARAM_STRING];
    char mXtraServer2[GPS_MAX_PARAM_STRING];
    char mXtraServer3[GPS_MAX_PARAM_STRING];
    char mNTPServer1[GPS_MAX_PARAM_STRING];
    char mNTPServer2[GPS_MAX_PARAM_STRING];
    char mNTPServer3[GPS_MAX_PARAM_STRING];
    char mVENDOR[GPS_MAX_PARAM_STRING];
    unsigned long mLgeTlsMode;
    unsigned long mLgeGPSPositionMode;
    char mChipsetID[GPS_MAX_PARAM_STRING];
};

#endif /* GPSSERVICECONFIG_H_ */
