// Copyright (c) 2026 LuneOS project
//
// SPDX-License-Identifier: Apache-2.0
//
// Layout test for the SNTP wire packet.  RFC 4330 fixes the packet at 48
// bytes of 32-bit words; the pre-fix struct used unsigned long and ballooned
// to 104 bytes on LP64, which corrupted every NTP exchange on 64-bit targets.

#include <stdio.h>
#include <stddef.h>

#include <NtpClient.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond) do { \
    tests_run++; \
    if (!(cond)) { \
        tests_failed++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

int main(void) {
    CHECK(sizeof(struct ntp_packet) == 48);
    CHECK(offsetof(struct ntp_packet, rootDelay) == 4);
    CHECK(offsetof(struct ntp_packet, originateTimeStampSecs) == 24);
    CHECK(offsetof(struct ntp_packet, receiveTimeStampSeqs) == 32);
    CHECK(offsetof(struct ntp_packet, transmitTimeStampSecs) == 40);

    printf("%s: %d checks, %d failed\n",
           tests_failed ? "FAIL" : "PASS", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
