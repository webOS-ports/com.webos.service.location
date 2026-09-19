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


#ifndef _ICONNECTIVITY_LISTENER_H_
#define _ICONNECTIVITY_LISTENER_H_


class IConnectivityListener {
public:
    virtual void Handle_WifiNotification(bool) = 0;

    virtual void Handle_ConnectivityNotification(bool) = 0;

    virtual void Handle_TelephonyNotification(bool) = 0;

    virtual void Handle_WifiInternetNotification(bool) = 0;

    /* The device really slept and the kernel woke it up again (sleepd
     * "resume" with resumetype kernel). Never called for refused or
     * abandoned suspend attempts. */
    virtual void Handle_KernelResumeNotification() = 0;
};

#endif
