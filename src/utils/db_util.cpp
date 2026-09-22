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


#include <db_util.h>
#include <unistd.h>

/*
 * Parse handle->fileName into handle->doc once, so a caller reading several
 * keys does not re-parse (and re-validate) the file per key.  Returns SUCCESS
 * only when a document with a root element is available.
 */
int openPreference(const char *filename, DBHandle *handle) {
  if (!handle || !filename) {
    return NULL_VALUE;
  }

  handle->fileName = filename;
  handle->doc = xmlReadFile(filename, NULL, XML_PARSE_NONET | XML_PARSE_NOENT);

  if (handle->doc == NULL) {
    /*
     * Missing, unreadable or malformed.  Reported as IO_ERROR rather than
     * probed for with access() beforehand: the file lives in a writable state
     * directory, so a separate existence check would only open a window for it
     * to be swapped between the check and the parse.
     */
    return IO_ERROR;
  }

  if (xmlDocGetRootElement(handle->doc) == NULL) {
    xmlFreeDoc(handle->doc);
    handle->doc = NULL;
    return IO_ERROR;
  }

  return SUCCESS;
}

void closePreference(DBHandle *handle) {
  if (handle && handle->doc) {
    xmlFreeDoc(handle->doc);
    handle->doc = NULL;
  }
}

int get(DBHandle *handle, const char *keyVal, xmlChar **result) {
  int ownDoc = 0;
  xmlNodePtr root = NULL;
  xmlNodePtr cur = NULL;

  if (!keyVal || !handle || !result) {
    return NULL_VALUE;
  }

  *result = NULL;

  if (handle->doc == NULL) {
    int ret = openPreference(handle->fileName, handle);

    if (ret != SUCCESS) {
      return ret;
    }

    ownDoc = 1;
  }

  root = xmlDocGetRootElement(handle->doc);

  if (root == NULL) {
    if (ownDoc) {
      closePreference(handle);
    }

    return IO_ERROR;
  }

  for (cur = root->xmlChildrenNode; cur != NULL; cur = cur->next) {
    if ((!xmlStrcmp(cur->name, (const xmlChar *) keyVal))) {
      *result = xmlNodeListGetString(handle->doc, cur->xmlChildrenNode, 1);
      break;
    }
  }

  if (ownDoc) {
    closePreference(handle);
  }

  /*
   * An empty element parses to a NULL string, which is indistinguishable here
   * from a key that is simply absent; both mean "no usable value", and callers
   * must not be handed a NULL to atoi().
   */
  if (*result == NULL) {
    return KEY_NOT_FOUND;
  }

  return SUCCESS;
}

int put(DBHandle *handle, const char *key, const char *value) {
  xmlNodePtr root_node = NULL;

  if (!key) {
    return NULL_VALUE;
  }

  if (!handle || !handle->doc) {
    return INIT_ERROR;
  }

  root_node = xmlDocGetRootElement(handle->doc);

  if (root_node == NULL) {
    return INIT_ERROR;
  }

  if (xmlNewChild(root_node, NULL, BAD_CAST key, BAD_CAST value) == NULL) {
    return UNKNOWN_ERROR;
  }

  return SUCCESS;
}

int deleteKey(DBHandle *handle, const char *keyVal) {
  xmlNodePtr root = NULL;
  xmlNodePtr cur = NULL;
  xmlNodePtr next = NULL;
  int ret;

  if (!keyVal || !handle) {
    return NULL_VALUE;
  }

  closePreference(handle);
  ret = openPreference(handle->fileName, handle);

  if (ret != SUCCESS) {
    return ret;
  }

  root = xmlDocGetRootElement(handle->doc);

  /*
   * xmlUnlinkNode clears the node's sibling pointers, so the next node has to
   * be latched before unlinking, and the detached node freed - it is no longer
   * owned by the document and would otherwise leak.
   */
  for (cur = root->xmlChildrenNode; cur != NULL; cur = next) {
    next = cur->next;

    if ((!xmlStrcmp(cur->name, (const xmlChar *) keyVal))) {
      xmlUnlinkNode(cur);
      xmlFreeNode(cur);
    }
  }

  return commit(handle);
}

int commit(DBHandle *handle) {
  int written;

  if (!handle || !handle->fileName || !handle->doc) {
    return INIT_ERROR;
  }

  written = xmlSaveFormatFileEnc(handle->fileName, handle->doc, "UTF-8", 1);

  /*
   * The document is owned by the handle, not by commit().  Freeing it here and
   * leaving handle->doc dangling made a second commit() - or any later get() -
   * a use-after-free.
   */
  closePreference(handle);

  return (written < 0) ? IO_ERROR : SUCCESS;
}

int isFileExists(const char *fname) {
  if (!fname) {
    return 0;
  }

  return (access(fname, F_OK) == 0);
}

int createPreference(const char *filename, DBHandle *handle, const char *title, int enablecheck) {
  xmlDocPtr doc_ptr = NULL;
  xmlNodePtr root_node = NULL;

  if (!handle || !title || !filename) {
    return NULL_VALUE;
  }

  if (enablecheck && isFileExists(filename)) {
    return FILE_EXIST_ERROR;
  }

  doc_ptr = xmlNewDoc(BAD_CAST "1.0");

  if (doc_ptr == NULL) {
    return UNKNOWN_ERROR;
  }

  root_node = xmlNewNode(NULL, BAD_CAST title);

  if (root_node == NULL) {
    xmlFreeDoc(doc_ptr);
    return UNKNOWN_ERROR;
  }

  xmlDocSetRootElement(doc_ptr, root_node);

  /*
   * The handle is overwritten rather than closed first: callers are allowed to
   * hand in a fresh, uninitialised DBHandle, so handle->doc cannot be read here.
   */
  handle->doc = doc_ptr;
  handle->fileName = filename;

  return SUCCESS;
}

int deletePreference(const char *filename) {
  if (!filename) {
    return NULL_VALUE;
  }

  return remove(filename);
}
