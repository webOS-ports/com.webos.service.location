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


#include <NetworkRequestManager.h>
#include <loc_log.h>

using namespace std;

NetworkRequestManager::NetworkRequestManager() {
    LS_LOG_DEBUG("NetworkRequestManager ctor");
}

NetworkRequestManager::~NetworkRequestManager() {
    LS_LOG_DEBUG("NetworkRequestManager dtor");
}

void NetworkRequestManager::init() {
    loc_http_start();
    loc_http_set_callback(handleDataCb, this);

    _attributes.set(Network_Defines::ACTIVE);

    LS_LOG_DEBUG("NetworkRequestManager started");
}

void NetworkRequestManager::deInit() {
    if (!_attributes.test(Network_Defines::ACTIVE)) {
        LS_LOG_ERROR("Network Manager already Inactive!!");
        return;
    }

    _attributes.reset(Network_Defines::ACTIVE); // clear the active bit

    loc_http_stop();

    LS_LOG_DEBUG("NetworkRequestManager stopped");
}

void NetworkRequestManager::handleDataCb(HttpReqTask *task, void *user_data) {

    if (!task || !user_data) {
        LS_LOG_DEBUG("fatal!!..invalid data received in callback");
        return;
    }

    NetworkRequestManager *pThis = (NetworkRequestManager *) user_data;
    LS_LOG_DEBUG("handleDataCb task  %p", task);
    HttpInterface *client = pThis->httpRequestList[task];

    if (HTTP_STATUS_CODE_SUCCESS == task->curlDesc.httpResponseCode) {
        LS_LOG_DEBUG("cbHttpResponsee %s", task->responseData);
    } else {
        LS_LOG_DEBUG("task->curlDesc.curlResultErrorStr %s", task->curlDesc.curlResultErrorStr);
    }

    LS_LOG_DEBUG("received client address %p", client);

    if (client)
        client->handleResponse(task);
    else {
        //client is removed, clear task
        LS_LOG_INFO("No client to handleResponse");
        pThis->clearTransaction(task);
    }

}

ErrorCodes NetworkRequestManager::initiateTransaction(const char **headers, int size, string url, bool isSync,
                                                      LSMessage *message, HttpInterface *userdata, char *post_data) {

    if (!_attributes.test(Network_Defines::ACTIVE)) {
        LS_LOG_DEBUG("Network Manager Inactive");
        return ERROR_MUTLITHREAD;
    }

    // size, not 0: given headers and a size of 0, loc-utils sent no headers
    // at all, not even its defaults (which it uses only when headers is NULL).
    HttpReqTask *gHttpReqTask = loc_create_http_task(headers, size, message, userdata);

    if (NULL == gHttpReqTask) {
        LS_LOG_ERROR("Fatal error...loc_create_http failed!!");
        return ERROR_NETWORK_ERROR;
    }

    if (!loc_http_task_prepare_connection(&gHttpReqTask, (char *) url.c_str())) {
        loc_http_task_destroy(&gHttpReqTask);
        return ERROR_NETWORK_ERROR;
    }

    LS_LOG_INFO("initiate_Transaction succesful gHttpReqTask %p=\n", gHttpReqTask);

    if (gHttpReqTask->post_data) {     // free up any past data
        free(gHttpReqTask->post_data);
        gHttpReqTask->post_data = NULL;
    }

    if (post_data)
        gHttpReqTask->post_data = strdup(post_data);

    if (!loc_http_add_request(gHttpReqTask, isSync)) {
        loc_http_task_destroy(&gHttpReqTask);
        return ERROR_NETWORK_ERROR;
    }

    httpRequestList[gHttpReqTask] = userdata;
    return ERROR_NONE;

}

void NetworkRequestManager::cancelTransaction(HttpReqTask *) {
//TBD
}

void NetworkRequestManager::clearTransaction(HttpReqTask *task) {
    httpRequestList.erase(task);
    loc_http_remove_request(task);
    loc_http_task_destroy(&task);

    LS_LOG_DEBUG("transaction cleared");
}

