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

#ifndef _DB_UTIL_H_
#define _DB_UTIL_H_

#include <libxml/parser.h>
#include <libxml/xmlmemory.h>
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>
#include <glib-object.h>



#define PREF_PATH   "."             //Path of all prefence to store in system
#define MY_ENCODING "ISO-8859-1"    //Not used now
typedef struct _DBHandle DBHandle;
/*
 * DB handle has neccasry info to process request
 */
struct _DBHandle {
    xmlDocPtr doc;
  const char *fileName;
};

/*
 * Error codes
 */
typedef enum {
    SUCCESS = 0,
    IO_ERROR,
    NULL_VALUE,
    KEY_NOT_FOUND,
    PREFERENCE_NOT_FOUND,
    INVALID_KEY,
    INIT_ERROR,
    FILE_EXIST_ERROR,
    UNKNOWN_ERROR,
} DbErrorCodes;

/**
 * <Funciton>       createPreference
 * <Description>    create a new preference xml file
 * @param           <filename> <In> <name of the file to create>
 * @param           <DBHandle> <In> <Intialized DBHandle it will be populated later>
 * @param           <title> <In> <the Root elemnet in XML file>
 * @return          int
 */
int createPreference(const char *filename, DBHandle *handle, const char *title, int enablecheck);

/**
 * <Funciton>       openPreference
 * <Description>    Parse an existing preference file into the handle once, so a
 *                  caller reading several keys pays for one parse rather than
 *                  one per key, and so no existence check has to race the read.
 *                  The handle owns the document until closePreference().
 * @param           <filename> <In> <name of the file to read>
 * @param           <DBHandle> <In> <handle to populate>
 * @return          int SUCCESS, NULL_VALUE or IO_ERROR
 */
int openPreference(const char *filename, DBHandle *handle);

/**
 * <Funciton>       closePreference
 * <Description>    Release the document held by the handle.  Safe on a handle
 *                  that holds none; leaves handle->doc NULL either way.
 * @param           <DBHandle> <In> <handle to release>
 * @return          void
 */
void closePreference(DBHandle *handle);

/**
 * <Funciton>       put
 * <Description>    Get the position from GPS
 * @param           <DBHandle> <In> <DBHandled intialized in create>
 * @param           <key> <In> <key to the value will be mapped>
 * @param           <value> <In> <value will be mapped with the given key>
 * @return          int
 */
int put(DBHandle *handle, const char *key, const char *value);

/**
 * <Funciton>       get
 * <Description>    Get teh Value of the given key
 * @param           <DBHandle> <In> <DBHandled intialized in create>
 * @param           <key> <In> <key to get the value>
 * @param           <result> <In> <result will be copied here>
 * @return          int
 */
int get(DBHandle *handle, const char *key, xmlChar **result);

/**
 * <Funciton>       deleteKey
 * <Description>    Delete the given key from xml
 * @param           <DBHandle> <In> <DBHandled intialized in create>
 * @param           <key> <In> <key to delete>
 * @return          int
 */
int deleteKey(DBHandle *handle, const char *key);

/**
 * <Funciton>       commit
 * <Description>    Flush the modified xml to the file
 * @param           <DBHandle> <In> <DBHandled intialized in create>
 * @return          int
 */
int commit(DBHandle *handle);

/**
 * <Funciton>       isFileExists
 * <Description>    Check if the file exists
 * @param           <fname> <In> <Name of the file to check>
 * @return          int
 */
int isFileExists(const char *fname);

/**
 * <Funciton>       deletePreference
 * <Description>    Delete the gicen preference xml file
 * @param           <filename> <In> <file name to delete>
 * @return          int
 */
int deletePreference(const char *filename);



#endif /* _DB_UTIL_H_ */
