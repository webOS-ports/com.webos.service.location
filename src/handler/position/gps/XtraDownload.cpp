// Copyright (c) 2026 LuneOS project
//
// SPDX-License-Identifier: Apache-2.0

#include <glib.h>
#include <loc_log.h>
#include <XtraDownload.h>

static void dropResponse(HttpReqTask *task) {
    g_free(task->responseData);
    task->responseData = nullptr;
    task->responseSize = 0;
}

static bool isCompleteXtraResponse(const HttpReqTask *task) {
    if (task->curlDesc.curlResultCode != CURLE_OK) {
        LS_LOG_INFO("xtra download failed: curl result %d\n", task->curlDesc.curlResultCode);
        return false;
    }

    if (task->curlDesc.httpResponseCode != HTTP_STATUS_CODE_SUCCESS) {
        LS_LOG_INFO("xtra download failed: http status code %ld\n", task->curlDesc.httpResponseCode);
        return false;
    }

    if (!task->responseData || task->responseSize == 0) {
        LS_LOG_INFO("xtra download failed: empty response\n");
        return false;
    }

    return true;
}

bool fetchXtraData(HttpReqTask **task, char *const *servers, int count) {
    if (!task || !*task)
        return false;

    for (int i = 0; i < count; i++) {
        if (loc_http_task_prepare_connection(task, servers[i])
            && loc_http_add_request(*task, TRUE)
            && isCompleteXtraResponse(*task)) {
            return true;
        }

        dropResponse(*task);
    }

    return false;
}
