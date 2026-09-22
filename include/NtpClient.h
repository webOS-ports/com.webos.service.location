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


#ifndef LOCATION_NTPCLIENT_H
#define LOCATION_NTPCLIENT_H

#include <stdint.h>
#include <glib.h>
#include <GPSServiceConfig.h>


typedef int64_t ntpTime;
typedef int64_t ntpReferenceTime;

/*
 * SNTP wire format (RFC 4330): every word is exactly 32 bits.  These fields
 * were declared unsigned long, which is 8 bytes on aarch64/x86-64, so on every
 * 64-bit target the request packet was 104 bytes instead of 48 and the reply's
 * transmit timestamp was read from the wrong offset.  Only armv7 ever computed
 * a correct NTP time.
 */
struct ntp_packet {
    uint8_t modeVNli;
    uint8_t stratum;
    int8_t poll;
    int8_t precision;
    uint32_t rootDelay;
    uint32_t rootDispersion;
    uint32_t referenceIdentifier;
    uint32_t referenceTimeStampSecs;
    uint32_t referenceTimeStampFreq;
    uint32_t originateTimeStampSecs;
    uint32_t originateTimeStampFreq;
    uint32_t receiveTimeStampSeqs;
    uint32_t receiveTimeStampFreq;
    uint32_t transmitTimeStampSecs;
    uint32_t transmitTimeStampFreq;
};

typedef enum {
    NONE,
    ALREADY_DOWNLOADING,
    CONNECTION_PROBLEM
} NtpErrors;

class NTPData {
private:
public:
    NTPData(const ntpTime &NtpTime, const ntpReferenceTime &NtpTimeReference, int RoundTripTime) : NtpTime(NtpTime),
                                                                                                   NtpTimeReference(
                                                                                                           NtpTimeReference),
                                                                                                   RoundTripTime(
                                                                                                           RoundTripTime) { }

    ntpTime getNtpTime() const {
        return NtpTime;
    }

    void setNtpTime(ntpTime NtpTime) {
        NTPData::NtpTime = NtpTime;
    }

    ntpReferenceTime getNtpTimeReference() const {
        return NtpTimeReference;
    }

    void setNtpTimeReference(ntpReferenceTime NtpTimeReference) {
        NTPData::NtpTimeReference = NtpTimeReference;
    }

    int getRoundTripTime() const {
        return RoundTripTime;
    }

    void setRoundTripTime(int RoundTripTime) {
        NTPData::RoundTripTime = RoundTripTime;
    }

private:
    ntpTime NtpTime;
    ntpReferenceTime NtpTimeReference;
    //Round trip is init ?? or signed or unsigned
    int RoundTripTime;

};

typedef enum {
    NTPPENDINGNETWORK = 0,
    NTPDOWNLOADING,
    NTPIDLE
} NtpDownloadState;

class INtpClinetCallback {
public :
    virtual void onRequestCompleted(NtpErrors error, const NTPData *data) = 0;

    virtual ~INtpClinetCallback() { };
};

class NtpClient {
private :
    NtpDownloadState mDownloadNtpDataStatus;
    GPSServiceConfig *mConfig;
    INtpClinetCallback *mCallback;
public:
    NtpClient();

    bool start(GPSServiceConfig *config, INtpClinetCallback *callback);

    GPSServiceConfig *getConfig() const {
        return mConfig;
    }

    INtpClinetCallback *getCallback() const {
        return mCallback;
    }

    const NtpDownloadState &getMDownloadNtpDataStatus() const {
        return mDownloadNtpDataStatus;
    }

private:
    static gpointer ntpDownloadThread(gpointer arg);

    int64_t static getElapsedRealtime();
};


#endif //LOCATION_NTPCLIENT_H
