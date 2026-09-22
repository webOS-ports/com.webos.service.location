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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <loc_log.h>
#include "gps_cfg.h"

static void trim_spaces(char *org_string);

typedef struct _gps_param_v_type gps_param_v_type;

struct _gps_param_v_type {
  char *param_name;
  char *param_str_value;
  int param_int_value;
  double param_double_value;
};


/**
 * <Funciton>       gps_set_config_entry
 * <Description>    Setting the config values to the parameters being read from the code
 *
 * @param           <config_entry> <In> <Config Entry>
 * @param           <config_value> <In> <Config Value>
 * @throws
 * @return          void
 */
void gps_set_config_entry(gps_param_s_type *config_entry, gps_param_v_type *config_value) {
  if (NULL == config_entry || NULL == config_value) {
    LS_LOG_ERROR("%s: INVALID config entry or parameter\n", __FUNCTION__);
    return;
  }

  if (strcmp(config_entry->param_name, config_value->param_name) == 0 && config_entry->param_ptr) {
    switch (config_entry->param_type) {
      case 's':
        if (strcmp(config_value->param_str_value, "NULL") == 0) {
          *((char *) config_entry->param_ptr) = '\0';
        } else {
          /*
           * The destinations are char[GPS_MAX_PARAM_STRING].  The old
           * GPS_MAX_PARAM_STRING + 1 size let g_strlcpy write the terminator
           * one byte past the field, into whatever struct member follows.
           */
          (void)g_strlcpy((char *) config_entry->param_ptr,
                    config_value->param_str_value,
                    GPS_MAX_PARAM_STRING);
        }
        /* Log INI values */
        LS_LOG_DEBUG("%s: PARAM %s = %s\n", __FUNCTION__, config_entry->param_name,
                     (char *) config_entry->param_ptr);

        if (NULL != config_entry->param_set) {
          *(config_entry->param_set) = 1;
        }
        break;

      case 'n':
        *((int *) config_entry->param_ptr) = config_value->param_int_value;
        /* Log INI values */
        LS_LOG_DEBUG("%s: PARAM %s = %d\n", __FUNCTION__, config_entry->param_name,
                     config_value->param_int_value);

        if (NULL != config_entry->param_set) {
          *(config_entry->param_set) = 1;
        }
        break;

      case 'f':
        *((double *) config_entry->param_ptr) = config_value->param_double_value;
        /* Log INI values */
        LS_LOG_DEBUG("%s: PARAM %s = %f\n", __FUNCTION__, config_entry->param_name,
                     config_value->param_double_value);

        if (NULL != config_entry->param_set) {
          *(config_entry->param_set) = 1;
        }
        break;

      default:
        LS_LOG_ERROR("%s: PARAM %s parameter type must be n, f, or s\n", __FUNCTION__,
                     config_entry->param_name);
        break;
    }
  }
}

/**
 * <Funciton>       gps_read_conf
 * <Description>    Setting the config values to the parameters being read from the code
 *
 * @param           <conf_file_name> <In> <Config File Name>
 * @param           <config_table> <In> <Parameter Table>
 * @param           <table_length> <In> <Length of the Table>
 * @throws
 * @return          void
 */
void gps_read_conf(const char *conf_file_name, gps_param_s_type *config_table, uint32_t table_length) {
  FILE *gps_conf_fp = NULL;
  char input_buf[GPS_MAX_PARAM_LINE];  /* declare a char array */
  char *lasts;
  gps_param_v_type config_value;
  uint32_t i;

  if ((gps_conf_fp = fopen(conf_file_name, "r")) != NULL) {
    LS_LOG_DEBUG("%s: using %s\n", __FUNCTION__, conf_file_name);
  } else {
    LS_LOG_WARNING("%s: no %s file found\n", __FUNCTION__, conf_file_name);
    return; /* no parameter file */
  }

  /* Clear all validity bits */
  for (i = 0; NULL != config_table && i < table_length; i++) {
    if (NULL != config_table[i].param_set) {
      *(config_table[i].param_set) = 0;
    }
  }

  while (fgets(input_buf, GPS_MAX_PARAM_LINE, gps_conf_fp) != NULL) {
    memset(&config_value, 0, sizeof(config_value));

    /* Separate variable and value */
    config_value.param_name = strtok_r(input_buf, "=", &lasts);
    if (config_value.param_name == NULL)
      continue; /* skip lines that do not contain "=" */

    config_value.param_str_value = strtok_r(NULL, "=", &lasts);
    if (config_value.param_str_value == NULL)
      continue; /* skip lines that do not contain two operands */

    /* Trim leading and trailing spaces */
    trim_spaces(config_value.param_name);
    trim_spaces(config_value.param_str_value);

    /* Parse numerical value */
    if (config_value.param_str_value[0] == '0' && tolower(config_value.param_str_value[1]) == 'x') {
      /* parsing hex */
      config_value.param_int_value = (int) strtol(&config_value.param_str_value[2], (char **) NULL, 16);
    } else {
      config_value.param_double_value = atof(config_value.param_str_value); /* float */
      config_value.param_int_value = atoi(config_value.param_str_value); /* dec */
    }

    for (i = 0; NULL != config_table && i < table_length; i++) {
      gps_set_config_entry(&config_table[i], &config_value);
    }
  }

  fclose(gps_conf_fp);
}

/**
 * <Funciton>       trim_space
 * <Description>    Removing leading and trailing spaces the Config Entries
 *
 * @param           <org_string> <In> <Config Entry>
 * @throws
 * @return          void
 */
static void trim_spaces(char *org_string) {
  char *scan_ptr = NULL;
  char *write_ptr = NULL;
  char *first_nonspace = NULL;
  char *last_nonspace = NULL;

  scan_ptr = org_string;
  write_ptr = org_string;

  while (*scan_ptr) {
    if (!isspace(*scan_ptr) && first_nonspace == NULL) {
      first_nonspace = scan_ptr;
    }

    if (first_nonspace != NULL) {
      *(write_ptr++) = *scan_ptr;

      if (!isspace(*scan_ptr)) {
        last_nonspace = write_ptr;
      }
    }

    scan_ptr++;
  }

  if (last_nonspace) {
    *last_nonspace = '\0';
  }
}
