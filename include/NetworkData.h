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


#ifndef NETWORKDATA_H_
#define NETWORKDATA_H_

#include <luna-service2/lunaservice.h>
#include <glib.h>
#include <loc_log.h>

class NetworkDataClient {

public:

    NetworkDataClient() {
        LS_LOG_DEBUG("Ctor NetworkDataClient");
    }

    virtual ~NetworkDataClient() {
        LS_LOG_DEBUG("Dtor NetworkDataClient");
    }

    virtual void onUpdateCellData(const char *cellData) = 0;

    virtual void onUpdateWifiData(GHashTable *wifiAccessPoints) = 0;
};

class NetworkData {

public:

    NetworkData(LSHandle *sh);

    ~NetworkData();

    void setNetworkDataClient(NetworkDataClient *client);

    bool registerForCellInfo();

    bool registerForWifiAccessPoints();

    bool unregisterForCellInfo();

    bool unregisterForWifiAccessPoints();

private:

    static bool cellDataCb(LSHandle *sh, LSMessage *message, void *ctx);

    static bool wifiAccessPointsCb(LSHandle *sh, LSMessage *message, void *ctx);

    bool cellDataCb(LSHandle *sh, LSMessage *message);

    bool wifiAccessPointsCb(LSHandle *sh, LSMessage *message);

private:

    friend class NetworkDataTest;

    LSHandle *mPrvHandle;
    LSMessageToken mCellInfoReq;
    LSMessageToken mWifiInfoReq;
    NetworkDataClient *mClient;
};

#endif // NETWORKDATA_H_
