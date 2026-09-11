// Copyright (c) 2024 LG Electronics, Inc.
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


#include "NtpClient.h"

#include <inttypes.h>

#include <glib.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <loc_log.h>
#include <unistd.h>

#define SOCK_DEFAULT_PORT               123
#define SOCK_MODE                       (3 << 3) | 3
// NTP Time
// NTP time stamp of 1 Jan 1970 = 2,208,988,800
#define NTP_EPOCH                       (86400U * (365U * 70U + 17U))
#define NTP_REPLY_TIMEOUT               6000
#define OFFSET                          1000

#define NANO_SEC_FRAQ                   1000000000LL

#define MILLI_SEC_FRAQ                  1000000


NtpClient::NtpClient() : mDownloadNtpDataStatus(NtpDownloadState::NTPIDLE), mConfig(nullptr), mCallback(nullptr) { }

bool NtpClient::start(GPSServiceConfig *config, INtpClinetCallback *callback) {
    LS_LOG_DEBUG("enter gpsRequestUtcTimeCb\n");

    if (config == nullptr || callback == nullptr)
        return false;
    if (NtpDownloadState::NTPDOWNLOADING == mDownloadNtpDataStatus) {
        return true;
    }
    mCallback = callback;
    mConfig = config;
    mDownloadNtpDataStatus = NtpDownloadState::NTPDOWNLOADING;
    GThread *ntpThread = g_thread_new("download ntp time", NtpClient::ntpDownloadThread, this);

    if (!ntpThread) {
        LS_LOG_ERROR("failed to create ntp download thread\n");
        /* Nothing is downloading: leaving the state latched here wedged every
         * future time-injection request for the life of the process. */
        mDownloadNtpDataStatus = NtpDownloadState::NTPIDLE;
        mCallback->onRequestCompleted(NtpErrors::CONNECTION_PROBLEM, nullptr);
        return false;
    }

    g_thread_unref(ntpThread);
    return true;
}

gpointer NtpClient::ntpDownloadThread(gpointer arg) {
    NtpClient *ntpClient = (NtpClient *) arg;

    GPSServiceConfig mGPSConf = *ntpClient->mConfig;
    struct sockaddr_in sock_addr;
    struct hostent *he;
    struct timeval tval;
    struct timeval recv_timeout;
    struct ntp_packet pkt;
    int usd = -1;
    int nextserverindex = 0;
    int count = 0;
    long int len = 0;
    char *ntpservers[3];

    LS_LOG_DEBUG("enter NtpClient::ntpDownloadThread\n");


    if (strlen(mGPSConf.mNTPServer1))
        ntpservers[count++] = mGPSConf.mNTPServer1;

    if (strlen(mGPSConf.mNTPServer2))
        ntpservers[count++] = mGPSConf.mNTPServer2;

    if (strlen(mGPSConf.mNTPServer3))
        ntpservers[count++] = mGPSConf.mNTPServer3;

    if (!count) {
        LS_LOG_ERROR("No NTP servers were specified in the GPS configuration\n");
        ntpClient->mDownloadNtpDataStatus = NtpDownloadState::NTPIDLE;
        ntpClient->mCallback->onRequestCompleted(NtpErrors::CONNECTION_PROBLEM, nullptr);
        return nullptr;
    }

    LS_LOG_DEBUG("ntp download thread started\n");


    while (nextserverindex < count) {

        if (-1 == (usd = socket(AF_INET, SOCK_DGRAM, 0))) {
            LS_LOG_ERROR("ntp download socket error\n");
            ntpClient->mDownloadNtpDataStatus = NtpDownloadState::NTPIDLE;
            ntpClient->mCallback->onRequestCompleted(NtpErrors::CONNECTION_PROBLEM, nullptr);
            return nullptr;
        }

        if ((he = gethostbyname(ntpservers[nextserverindex]))) {
            memset(&sock_addr, 0, sizeof(sock_addr));
            memcpy(&sock_addr.sin_addr, he->h_addr_list[0], sizeof(sock_addr.sin_addr));
            sock_addr.sin_port = htons(SOCK_DEFAULT_PORT);
            sock_addr.sin_family = AF_INET;

            if (-1 != connect(usd, (struct sockaddr *) &sock_addr, sizeof(sock_addr))) {
                memset(&pkt, 0, sizeof(pkt));
                pkt.modeVNli = SOCK_MODE;

                int64_t requestTicks = NtpClient::getElapsedRealtime();
                //TODO:time_t is not ime_t type as a signed integer (typically 32 or 64 bits wide)
                //Systems using a 32-bit time_t type are susceptible to the Year 2038 problem :-)
                //if system is using 64bit htonl takes  uint32_t then this passing is right ??
                pkt.originateTimeStampSecs = htonl(time(nullptr) + NTP_EPOCH);
                (void)send(usd, &pkt, sizeof(pkt), 0);
                /*
                 * SO_RCVTIMEO takes a struct timeval.  Passing an int holding
                 * milliseconds made the kernel read 6000 (and stack garbage)
                 * as seconds, so the recv timeout was 100 minutes, not 6s.
                 */
                recv_timeout.tv_sec = NTP_REPLY_TIMEOUT / 1000;
                recv_timeout.tv_usec = (NTP_REPLY_TIMEOUT % 1000) * 1000;
                (void)setsockopt(usd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));

                LS_LOG_DEBUG("connected to socket, call recv\n");

                len = recvfrom(usd, &pkt, sizeof(pkt), 0, nullptr, nullptr);
                int64_t responseTicks = NtpClient::getElapsedRealtime();
                //TODO:nthol uint32_t where as transmitTimeStampSecs unsigned long ?? is it ok.
                tval.tv_sec = ntohl(pkt.transmitTimeStampSecs) - NTP_EPOCH;

                LS_LOG_DEBUG("tval.tv_sec= %ld\n", tval.tv_sec);

                if (len > 0) {
                    int64_t NtpTime = (((int64_t)tval.tv_sec) * OFFSET);
                    int64_t NtpTimeReference = responseTicks;
                    //TODO :: RoundTripTime is int ??? should be unsigned long ?
                    int RoundTripTime = responseTicks - requestTicks -
                                        (pkt.transmitTimeStampSecs - pkt.receiveTimeStampSeqs);
                    LS_LOG_INFO("NtpTime = %" PRId64 " and NtpTimeReference = %" PRId64 " and RoundTripTime=%d\n tval.tv_sec= %ld\n",
                                 NtpTime,
                                 NtpTimeReference,
                                 RoundTripTime,
                                 tval.tv_sec);

                    NTPData ntpData(NtpTime, NtpTimeReference, RoundTripTime);
                    ntpClient->mCallback->onRequestCompleted(NtpErrors::NONE, &ntpData);

                    break; // break for success
                } else {
                    LS_LOG_INFO("Download Length 0 \n");
                    //TODO: on failure why should a new socket needs to be created ??
                }
            }
            else {
                LS_LOG_ERROR("ntp download Error in connect\n");
            }
        }
        else {
            LS_LOG_ERROR("ntp download Error gethostbyname\n");

        }
        if (usd >= 0) {
            close(usd);
            usd = -1;
        }
        nextserverindex++;
    }

    if (nextserverindex >= count)
        ntpClient->mCallback->onRequestCompleted(NtpErrors::CONNECTION_PROBLEM, nullptr);
    if (usd >= 0)
        close(usd);

    ntpClient->mDownloadNtpDataStatus = NtpDownloadState::NTPIDLE;
    return nullptr;
}

int64_t NtpClient::getElapsedRealtime() {
    struct timespec tp;

    tp.tv_sec = tp.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &tp);

    int64_t when = tp.tv_sec * NANO_SEC_FRAQ + tp.tv_nsec;

    return when / MILLI_SEC_FRAQ;
}
