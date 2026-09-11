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


#include <MapServicesImpl.h>

#include <WSPConfigurationFileParser.h>

using namespace std;

#define GEOCODEMETHOD    "getGeoCodeLocation"
#define REVGEOMETHOD       "getReverseLocation"

void MapServicesImpl::handleResponse(HttpReqTask *task) {
    int error = ERROR_NONE;
    char *response = NULL;
    LSMessage *message = NULL;

    if (!task) {
        return;
    }

    message = (LSMessage *) task->message;

    if (HTTP_STATUS_CODE_SUCCESS == task->curlDesc.httpResponseCode) {
        response = g_strdup(task->responseData);
        LS_LOG_DEBUG("cbHttpResponsee %s", response);
    } else {
        LS_LOG_INFO("task->curlDesc.curlResultErrorStr %s", task->curlDesc.curlResultErrorStr);
        error = ERROR_NETWORK_ERROR;
        response = g_strdup("HTTP Connection failed");
    }

    NetworkRequestManager::getInstance()->clearTransaction(task);

    LS_LOG_INFO("cbHttpResponse : %d %p", error, message);

    if (!strcmp(LSMessageGetMethod(message), GEOCODEMETHOD)) {
        GeoLocation geolocation(response);
        mGeoCodeCb(geolocation, error, message);
    } else if (!strcmp(LSMessageGetMethod(message), REVGEOMETHOD)) {
        GeoAddress geoaddr(response);
        mRevGeoCodeCb(geoaddr, error, message);
    }

    if (response)
        free(response);

}

MapServicesImpl::MapServicesImpl(WSPConfigurationFileParser* confData)
    : configurationData(confData) {
    LS_LOG_DEBUG("===MapServicesImpl Ctor====");
}

MapServicesImpl::~MapServicesImpl() {
    LS_LOG_DEBUG("===MapServicesImpl Dtor====");
}

ErrorCodes MapServicesImpl::geoCode(GeoAddress& address, GeoCodeCb geoCodeCb, bool isSync, LSMessage *message) {
    LS_LOG_DEBUG("MapServicesImpl Geocode %s %p", address.toString().c_str(), message);

    if (nullptr == geoCodeCb) {
        LS_LOG_ERROR("geoCodeCb is NULL");
        return ERROR_NOT_AVAILABLE;
    }

    if (!configurationData || !configurationData->isSupported(GEO_CODE)) {
        LS_LOG_ERROR("Feature not supported");
        return ERROR_NOT_IMPLEMENTED;
    }

    if (configurationData->getKey().empty()) {
        LS_LOG_ERROR("License Key not present");
        return ERROR_LICENSE_KEY_INVALID;
    }

    this->mGeoCodeCb = geoCodeCb;

    string strFormattedUrl = formatUrl(address.toString(), configurationData->getUrl(GEO_CODE),  configurationData->getKey().c_str());
    return lbsPostQuery(strFormattedUrl, isSync, message);
}

ErrorCodes MapServicesImpl::reverseGeoCode(GeoLocation& geolocation, ReverseGeoCodeCb revGeocodeCallback, bool isSync,
                                         LSMessage *message) {
    LS_LOG_DEBUG("MapServicesImpl reverseGeoCode %s %p", geolocation.toString().c_str(), message);

    if (nullptr == revGeocodeCallback) {
        LS_LOG_ERROR("revGeocodeCallback is NULL");
        return ERROR_NOT_AVAILABLE;
    }

    if (!configurationData || !configurationData->isSupported(REVERSE_GEOCODE)) {
        LS_LOG_ERROR("Feature not supported");
        return ERROR_NOT_IMPLEMENTED;
    }

    if (configurationData->getKey().empty()) {
        LS_LOG_ERROR("License Key not present");
        return ERROR_LICENSE_KEY_INVALID;
    }

    this->mRevGeoCodeCb = revGeocodeCallback;

    string strFormattedUrl = formatUrl(geolocation.toString(), configurationData->getUrl(REVERSE_GEOCODE),  configurationData->getKey().c_str());
    return lbsPostQuery(strFormattedUrl, isSync, message);
}

string MapServicesImpl::formatUrl(string geoData, std::string url, const char *key) {
    guchar *decodedKey = NULL;
    gsize size = 0;
    gsize len;
    GHmac *hmac = NULL;
    gchar *encodedSignature = NULL;
    guint8 *buffer = NULL;
    string finalURL;
    string encodedSignatureStr;
    string urlToSign;
    std::string subUrlToSign;
    std::string stDecodeKey;
    std::size_t pos;

    if (url.empty()) {
        LS_LOG_ERROR("URL not valid");
        goto EXIT;
    }

    pos = url.find(".com/");
    if (pos ==std::string::npos) {
        LS_LOG_ERROR("URL not valid");
        goto EXIT;
    }
    subUrlToSign = url.substr (pos+strlen(".com"));

    if (subUrlToSign.empty()) {
        LS_LOG_ERROR("URL not valid");
        goto EXIT;
    }

    //Decode the private key
  /*retAPIKey = locSecurityBase64DecodeData(key, &decodedKey);

    if (LOC_SECURITY_ERROR_SUCCESS  != retAPIKey) {
        LS_LOG_ERROR("Decoding the private key failed");
        goto EXIT;
    }*/

     //Decode the private key
    decodedKey = g_base64_decode(key, &size);
    if (NULL == decodedKey || 0 == size) {
        goto EXIT;
    }

    /*
     * g_base64_decode returns raw bytes with no terminator; constructing a
     * string from the bare pointer scanned the heap for a NUL.  Give the
     * constructor the length instead.
     */
    stDecodeKey.assign(reinterpret_cast<const char *>(decodedKey), size);
    urlToSign = subUrlToSign + geoData + "&key=" + stDecodeKey;

    hmac = g_hmac_new(G_CHECKSUM_SHA1, decodedKey, size);
    if (NULL == hmac) {
        goto EXIT;
    }

    g_hmac_update(hmac, (const unsigned char *) urlToSign.c_str(), -1);
    buffer = (guint8 *) g_malloc(strlen(urlToSign.c_str())+1);
    if (NULL == buffer) {
        goto EXIT;
    }

    len = urlToSign.length();
    g_hmac_get_digest(hmac, buffer, &len);

    //Encode the binary signature into base64 for use within a URL
    encodedSignature = g_base64_encode(buffer, len);

    if (NULL == encodedSignature) {
        goto EXIT;
    }

    encodedSignatureStr = string((const char *) encodedSignature);

    //make url-safe by replacing '+' and '/' characters
    replace(encodedSignatureStr.begin(), encodedSignatureStr.end(), '/', '_');
    replace(encodedSignatureStr.begin(), encodedSignatureStr.end(), '+', '-');

    finalURL = url;
    finalURL.append(geoData);
    finalURL.append("&key=");
    finalURL.append(stDecodeKey);

    LS_LOG_DEBUG("url: formatted succesfully");

    EXIT:

    if (decodedKey != NULL)
        g_free(decodedKey);

    if (hmac != NULL)
        g_hmac_unref(hmac);

    if (encodedSignature != NULL)
        g_free(encodedSignature);

    if (buffer != NULL)
        g_free(buffer);

    return finalURL;
}

ErrorCodes MapServicesImpl::lbsPostQuery(string url, bool isSync, LSMessage *message) {
    LS_LOG_DEBUG("==lbsPostQuery==");
    return NetworkRequestManager::getInstance()->initiateTransaction(NULL, 0, url, isSync, message, this);
}

