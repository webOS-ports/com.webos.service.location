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


#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "db_util.h"

#include "Gps_stored_data.h"

#define MAX_LEN 64

/**
 * <Funciton >   gps_service_get_stored_position
 * <Description>   will be called for getting the stored position
 *      .
 * @param     <last_pos> <In> <Stored the position data>
 * @throws
 * @return     Void
 */

void set_store_position(int64_t timestamp, gdouble latitude,
                        gdouble longitude, gdouble altitude, gdouble speed,
                        gdouble direction, gdouble hor_accuracy,
                        gdouble ver_accuracy, const char *path) {
  DBHandle handle;
  char input[MAX_LEN];

  if (path == NULL) {
    return;
  }

  memset(&handle, 0, sizeof(handle));

  if (createPreference(path, &handle, "Location", FALSE) != SUCCESS) {
    return;
  }

  /*
   * snprintf, not sprintf: "%.7f" of a double can run to 318 characters and
   * this buffer is 50.  The values reach here straight from the GNSS HAL and
   * from /mock/setLocation, so an out-of-range fix is an attacker-reachable
   * stack smash, not a theoretical one.  PRId64 keeps the timestamp correct on
   * both 32- and 64-bit targets, where int64_t is long long and long
   * respectively.
   */
  snprintf(input, sizeof(input), "%" PRId64, timestamp);
  put(&handle, "timestamp", input);
  snprintf(input, sizeof(input), "%.7f", latitude);
  put(&handle, "latitude", input);
  snprintf(input, sizeof(input), "%.7f", longitude);
  put(&handle, "longitude", input);
  snprintf(input, sizeof(input), "%.7f", altitude);
  put(&handle, "altitude", input);
  snprintf(input, sizeof(input), "%f", speed);
  put(&handle, "speed", input);
  snprintf(input, sizeof(input), "%.7f", direction);
  put(&handle, "direction", input);
  snprintf(input, sizeof(input), "%f", hor_accuracy);
  put(&handle, "hor_accuracy", input);
  snprintf(input, sizeof(input), "%f", ver_accuracy);
  put(&handle, "ver_accuracy", input);

  commit(&handle);
  closePreference(&handle);
}

/*
 * Read one numeric key.  A key that is missing, empty or unparseable is an
 * error rather than a silently-zero coordinate: get() used to return SUCCESS
 * with *result still NULL, and every caller fed that straight to atof().
 */
static int read_double(DBHandle *handle, const char *key, gdouble *out) {
  xmlChar *result = NULL;

  if (get(handle, key, &result) != SUCCESS || result == NULL) {
    return ERROR_NOT_AVAILABLE;
  }

  *out = g_ascii_strtod((const char *) result, NULL);
  xmlFree(result);

  return ERROR_NONE;
}

static int read_int64(DBHandle *handle, const char *key, int64_t *out) {
  xmlChar *result = NULL;

  if (get(handle, key, &result) != SUCCESS || result == NULL) {
    return ERROR_NOT_AVAILABLE;
  }

  *out = g_ascii_strtoll((const char *) result, NULL, 10);
  xmlFree(result);

  return ERROR_NONE;
}

int get_stored_position(Position *position, Accuracy *accuracy, const char *path) {
  DBHandle handle;
  int error = ERROR_NONE;

  if (position == NULL || accuracy == NULL || path == NULL) {
    return ERROR_NOT_AVAILABLE;
  }

  memset(&handle, 0, sizeof(handle));

  /*
   * Parse once and read every key out of that one document.  The previous code
   * called access() and then re-parsed the file for each of the eight keys,
   * which both raced the file away between the check and the first parse and
   * cost eight parses per cached-position read.
   */
  if (openPreference(path, &handle) != SUCCESS) {
    return ERROR_NOT_AVAILABLE;
  }

  if (read_int64(&handle, "timestamp", &position->timestamp) != ERROR_NONE ||
      read_double(&handle, "latitude", &position->latitude) != ERROR_NONE ||
      read_double(&handle, "longitude", &position->longitude) != ERROR_NONE ||
      read_double(&handle, "altitude", &position->altitude) != ERROR_NONE ||
      read_double(&handle, "hor_accuracy", &accuracy->horizAccuracy) != ERROR_NONE ||
      read_double(&handle, "ver_accuracy", &accuracy->vertAccuracy) != ERROR_NONE ||
      read_double(&handle, "speed", &position->speed) != ERROR_NONE ||
      read_double(&handle, "direction", &position->direction) != ERROR_NONE) {
    error = ERROR_NOT_AVAILABLE;
  }

  closePreference(&handle);

  return error;
}
