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


#ifndef LOCATION_ERRORS_H_
#define LOCATION_ERRORS_H_

#include <stdint.h>

/*
 * Common Error codes of Location Framework
 */
//get rid of  LocationError_t later on
typedef uint32_t LocationError_t;
typedef enum {
    ERROR_NONE = 0,
    ERROR_NOT_IMPLEMENTED,
    ERROR_TIMEOUT,
    ERROR_NETWORK_ERROR,
    ERROR_NOT_APPLICABLE_TO_THIS_HANDLER,
    ERROR_NOT_AVAILABLE,
    ERROR_WRONG_PARAMETER,
    ERROR_DUPLICATE_REQUEST,
    ERROR_REQUEST_INPROGRESS,
    ERROR_NOT_STARTED,
    ERROR_LICENSE_KEY_INVALID,
    ERROR_MUTLITHREAD
} ErrorCodes;

enum {
    ERROR_NETWORK_NOT_FOUND = 0,
    ERROR_NO_MEMORY,
    ERROR_NO_RESPONSE,
    ERROR_NO_DATA
};

    typedef enum {
        REQUEST_NONE = 0,
        REQUEST_PROGRESS,
        REQUEST_COMPLETED
    } request_state_cellid_type;

#endif /* LOCATION_ERRORS_H_ */
