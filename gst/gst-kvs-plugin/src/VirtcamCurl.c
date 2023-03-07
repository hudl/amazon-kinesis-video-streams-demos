#include "VirtcamCurl.h"

#include <stdlib.h>
#include <string.h>

#include <com/amazonaws/kinesis/video/common/PlatformUtils.h>

size_t writeMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    if (!size || !nmemb) {
        return 0;
    }

    size_t real_size = size * nmemb;
    MemType* mem = (MemType*) userp;

    char* ptr = realloc(mem->memory, mem->size + real_size + 1);
    if (!ptr) {
        DLOGE("Not enough memory (realloc returned NULL)");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->memory[mem->size] = 0;

    return real_size;
}

char* extractValueByKey(char* sourceString, const char* keyString) {
    char* token = strtok(sourceString, JSON_DELIMITER);
    while (token != NULL) {
        if (strcmp(token, keyString) == 0) {
            return strtok(NULL, JSON_DELIMITER);
        }
        token = strtok(NULL, JSON_DELIMITER);
    }

    return NULL;
}

CURL* virtcamCurlInit() {
    CURL* curl_handle = curl_easy_init();
    if (!curl_handle) {
        curl_global_cleanup();
        DLOGE("curl_easy_init() failed");
        exit(EXIT_FAILURE);
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, VIRTCAM_WORLD_URL);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, writeMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    return curl_handle;
}

bool curlMoveCamera(const CameraView target) {
    if (!isViewValid(target))
        return false;

    bool success = false;
    MemType chunk = {0, NULL};

    CURL* curl_handle = curl_easy_init();
    if (curl_handle) {
        char moveCameras[256];
        sprintf(moveCameras,
                "{\"moveCameras\": [{\"cameraIdx\": %d, \"pan\": %f, \"tilt\": %f, \"focalLength\": %f}]}",
                target.cameraIdx, target.pan, target.tilt, target.zoom);

        curl_easy_setopt(curl_handle, CURLOPT_URL, VIRTCAM_WORLD_URL);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, moveCameras);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, writeMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*) &chunk);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        CURLcode res = curl_easy_perform(curl_handle);
        if (res == CURLE_OK) {
            char* value = extractValueByKey(chunk.memory, "success");
            success = (value == NULL) ? false : strcmp(value, "true") == 0;
        } else {
            DLOGE("curl_easy_perform() failed: %s", curl_easy_strerror(res));
        }
    }

    curl_easy_cleanup(curl_handle);
    free(chunk.memory);

    return success;
}

int curlGetCameraId(CURL* curl_handle) {
    int id = UNINITIALIZED_CAMERA_IDX;
    MemType chunk = {0, NULL};

    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, "{\"getCameras\": true}");
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*) &chunk);

    CURLcode res = curl_easy_perform(curl_handle);
    if (res == CURLE_OK) {
        char* value = extractValueByKey(chunk.memory, DEFAULT_CAMERA_NAME);
        id = (value == NULL) ? UNINITIALIZED_CAMERA_IDX : atoi(value);
    } else {
        DLOGE("curl_easy_perform() failed: %s", curl_easy_strerror(res));
    }

    free(chunk.memory);

    return id;
}
