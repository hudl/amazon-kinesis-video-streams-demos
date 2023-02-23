#ifndef __VIRTCAM_CURL_H__
#define __VIRTCAM_CURL_H__

#include <curl/curl.h>
#include <stdbool.h>

#include "View.h"

#define VIRTCAM_WORLD_URL "http://localhost:9999/command/world"
#define JSON_DELIMITER    ",:\"{}"

// A data structure used to hold the result of the cURL request.
typedef struct {
    size_t size;
    char* memory;
} MemType;

size_t writeMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp);
char* extractValueByKey(char* sourceString, const char* keyString);
CURL* virtcamCurlInit();

bool curlMoveCamera(CameraView target);
int curlGetCameraId(CURL* curl_handle);

#endif  // __VIRTCAM_CURL_H__
