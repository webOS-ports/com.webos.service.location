// Copyright (c) 2026 LuneOS project
//
// SPDX-License-Identifier: Apache-2.0

#ifndef XTRADOWNLOAD_H_
#define XTRADOWNLOAD_H_

#include <loc_http.h>

// Tries each server once, in order, until one answers a complete 200 with a
// body. On true (*task)->responseData and responseSize hold that body; on
// false there is no body left in the task, and nothing to inject.
bool fetchXtraData(HttpReqTask **task, char *const *servers, int count);

#endif /* XTRADOWNLOAD_H_ */
