#define LOG_CLASS "GstPluginUtils"
#include "GstPlugin.h"

gboolean setGstTags(GQuark fieldId, const GValue* value, gpointer userData)
{
    STATUS retStatus = STATUS_SUCCESS;
    PGstTag pGstTag = (PGstTag) userData;
    PTag pTag;

    CHK_ERR(pGstTag != NULL, STATUS_NULL_ARG, "Iterator supplies NULL user data");
    CHK_ERR(pGstTag->tagCount < MAX_TAG_COUNT, STATUS_INVALID_ARG_LEN, "Max tag count reached");
    CHK_ERR(G_VALUE_HOLDS_STRING(value), STATUS_INVALID_ARG, "Tag value should be of a string type");

    pTag = &pGstTag->tags[pGstTag->tagCount];
    pTag->version = TAG_CURRENT_VERSION;
    STRNCPY(pTag->name, g_quark_to_string(fieldId), MAX_TAG_NAME_LEN);
    STRNCPY(pTag->value, g_value_get_string(value), MAX_TAG_VALUE_LEN);

    pGstTag->tagCount++;

CleanUp:

    CHK_LOG_ERR(retStatus);

    return STATUS_SUCCEEDED(retStatus);
}

STATUS gstStructToTags(GstStructure* pGstStruct, PGstTag pGstTag)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pGstTag != NULL, STATUS_NULL_ARG);

    MEMSET(pGstTag, 0x00, SIZEOF(GstTags));

    // Tags are optional
    CHK(pGstStruct != NULL, retStatus);

    // Iterate each field and process the tags
    CHK(gst_structure_foreach(pGstStruct, setGstTags, pGstTag), STATUS_INVALID_ARG);

CleanUp:

    return retStatus;
}

gboolean setGstIotInfo(GQuark fieldId, const GValue* value, gpointer userData)
{
    STATUS retStatus = STATUS_SUCCESS;
    PCHAR pFieldName;
    PIotInfo pIotInfo = (PIotInfo) userData;

    CHK_ERR(pIotInfo != NULL, STATUS_NULL_ARG, "Iterator supplies NULL user data");
    CHK_ERR(G_VALUE_HOLDS_STRING(value), STATUS_INVALID_ARG, "Tag value should be of a string type");

    pFieldName = (PCHAR) g_quark_to_string(fieldId);
    if (0 == STRCMP(pFieldName, IOT_GET_CREDENTIAL_ENDPOINT)) {
        STRNCPY(pIotInfo->endPoint, g_value_get_string(value), MAX_URI_CHAR_LEN);
    } else if (0 == STRCMP(pFieldName, CERTIFICATE_PATH)) {
        STRNCPY(pIotInfo->certPath, g_value_get_string(value), MAX_PATH_LEN);
    } else if (0 == STRCMP(pFieldName, PRIVATE_KEY_PATH)) {
        STRNCPY(pIotInfo->privateKeyPath, g_value_get_string(value), MAX_PATH_LEN);
    } else if (0 == STRCMP(pFieldName, CA_CERT_PATH)) {
        STRNCPY(pIotInfo->caCertPath, g_value_get_string(value), MAX_PATH_LEN);
    } else if (0 == STRCMP(pFieldName, ROLE_ALIASES)) {
        STRNCPY(pIotInfo->roleAlias, g_value_get_string(value), MAX_ROLE_ALIAS_LEN);
    }

CleanUp:

    CHK_LOG_ERR(retStatus);

    return STATUS_SUCCEEDED(retStatus);
}

STATUS gstStructToIotInfo(GstStructure* pGstStruct, PIotInfo pIotInfo)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pGstStruct != NULL && pIotInfo != NULL, STATUS_NULL_ARG);
    MEMSET(pIotInfo, 0x00, SIZEOF(IotInfo));
    CHK(gst_structure_foreach(pGstStruct, setGstIotInfo, pIotInfo), STATUS_INVALID_ARG);

CleanUp:

    return retStatus;
}

STATUS traverseDirectoryPemFileScan(UINT64 customData, DIR_ENTRY_TYPES entryType, PCHAR fullPath, PCHAR fileName)
{
    UNUSED_PARAM(entryType);
    UNUSED_PARAM(fullPath);

    PCHAR certName = (PCHAR) customData;
    UINT32 fileNameLen = STRLEN(fileName);

    if (fileNameLen > ARRAY_SIZE(CA_CERT_PEM_FILE_EXTENSION) + 1 &&
        (STRCMPI(CA_CERT_PEM_FILE_EXTENSION, &fileName[fileNameLen - ARRAY_SIZE(CA_CERT_PEM_FILE_EXTENSION) + 1]) == 0)) {
        certName[0] = FPATHSEPARATOR;
        certName++;
        STRCPY(certName, fileName);
    }

    return STATUS_SUCCESS;
}

STATUS lookForSslCert(PGstKvsPlugin pGstKvsPlugin)
{
    STATUS retStatus = STATUS_SUCCESS;
    struct stat pathStat;
    PCHAR pCaCertPath = NULL;
    CHAR certName[MAX_PATH_LEN];

    CHK(pGstKvsPlugin != NULL, STATUS_NULL_ARG);

    MEMSET(certName, 0x0, ARRAY_SIZE(certName));
    pCaCertPath = GETENV(CACERT_PATH_ENV_VAR);

    // if ca cert path is not set from the environment, try to use the one that cmake detected
    if (pCaCertPath == NULL) {
        CHK_ERR(STRNLEN(DEFAULT_KVS_CACERT_PATH, MAX_PATH_LEN) > 0, STATUS_INVALID_OPERATION, "No ca cert path given (error:%s)", strerror(errno));
        STRNCPY(pGstKvsPlugin->caCertPath, DEFAULT_KVS_CACERT_PATH, MAX_PATH_LEN);
    } else {
        // Copy the env path into local dir
        STRNCPY(pGstKvsPlugin->caCertPath, pCaCertPath, MAX_PATH_LEN);
        pCaCertPath = pGstKvsPlugin->caCertPath;

        // Check if the environment variable is a path
        CHK(0 == FSTAT(pCaCertPath, &pathStat), STATUS_DIRECTORY_ENTRY_STAT_ERROR);

        if (S_ISDIR(pathStat.st_mode)) {
            CHK_STATUS(traverseDirectory(pCaCertPath, (UINT64) &certName, /* iterate */ FALSE, traverseDirectoryPemFileScan));

            if (certName[0] != 0x0) {
                STRCAT(pCaCertPath, certName);
            } else {
                DLOGW("Cert not found in path set...checking if CMake detected a path\n");
                CHK_ERR(STRNLEN(DEFAULT_KVS_CACERT_PATH, MAX_PATH_LEN) > 0, STATUS_INVALID_OPERATION, "No ca cert path given (error:%s)",
                        strerror(errno));
                DLOGD("CMake detected cert path\n");
                pCaCertPath = DEFAULT_KVS_CACERT_PATH;
            }
        }
    }

CleanUp:

    CHK_LOG_ERR(retStatus);
    return retStatus;
}

STATUS identifyCpdNalFormat(PBYTE pData, UINT32 size, ELEMENTARY_STREAM_NAL_FORMAT* pFormat)
{
    STATUS retStatus = STATUS_SUCCESS;
    ELEMENTARY_STREAM_NAL_FORMAT format = ELEMENTARY_STREAM_NAL_FORMAT_UNKNOWN;
    BYTE start3ByteCode[] = {0x00, 0x00, 0x01};
    BYTE start4ByteCode[] = {0x00, 0x00, 0x00, 0x01};
    BYTE start5ByteCode[] = {0x00, 0x00, 0x00, 0x00, 0x01};

    CHK(pData != NULL && pFormat != NULL, STATUS_NULL_ARG);
    CHK(size > SIZEOF(start5ByteCode), STATUS_FORMAT_ERROR);

    // We really do very crude check for the Annex-B start code

    // First of all, we need to determine what format the CPD is in - Annex-B, Avcc or raw
    // NOTE: Some "bad" encoders encode an extra 0 at the end of the NALu resulting in
    // an extra zero interfering with the Annex-B start code so we check for 4 zeroes and 1
    if ((0 == MEMCMP(pData, start5ByteCode, SIZEOF(start5ByteCode))) || (0 == MEMCMP(pData, start4ByteCode, SIZEOF(start4ByteCode))) ||
        (0 == MEMCMP(pData, start3ByteCode, SIZEOF(start3ByteCode)))) {
        // Must be an Annex-B format
        format = ELEMENTARY_STREAM_NAL_FORMAT_ANNEX_B;

        // Early exit
        CHK(FALSE, retStatus);
    } else if (pData[0] == AVCC_VERSION_CODE && pData[4] == AVCC_NALU_LEN_MINUS_ONE && pData[5] == AVCC_NUMBER_OF_SPS_ONE) {
        // Looks like an AvCC format
        format = ELEMENTARY_STREAM_NAL_FORMAT_AVCC;
    } else if (size > HEVC_CPD_HEADER_SIZE && pData[0] == 1 && (pData[13] & 0xf0) == 0xf0 && (pData[15] & 0xfc) == 0xfc &&
               (pData[16] & 0xfc) != 0xfc && (pData[17] & 0xf8) != 0xf8 && (pData[18] & 0xf8) != 0xf8) {
        // Looks like an HEVC format
        format = ELEMENTARY_STREAM_NAL_FORMAT_HEVC;
    }

CleanUp:

    if (pFormat != NULL) {
        *pFormat = format;
    }

    return retStatus;
}

STATUS convertCpdFromAvcToAnnexB(PGstKvsPlugin pGstKvsPlugin, PBYTE pData, UINT32 size)
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 i, offset = 0;
    UINT16 spsSize, ppsSize;
    BYTE start4ByteCode[] = {0x00, 0x00, 0x00, 0x01};
    PBYTE pSrc = pData, pEnd = pData + size;

    CHK(pData != NULL && pGstKvsPlugin != NULL, STATUS_NULL_ARG);
    CHK(size > 8, STATUS_FORMAT_ERROR);

    // Skip to SPS size and read the nalu count
    pSrc += 6;
    spsSize = GET_UNALIGNED_BIG_ENDIAN((PUINT16) pSrc);
    pSrc += SIZEOF(UINT16);

    CHK(offset + SIZEOF(start4ByteCode) + spsSize < GST_PLUGIN_MAX_CPD_SIZE, STATUS_FORMAT_ERROR);
    CHK(pSrc + spsSize <= pEnd, STATUS_FORMAT_ERROR);

    // Output the Annex-B start code
    MEMCPY(pGstKvsPlugin->videoCpd + offset, start4ByteCode, SIZEOF(start4ByteCode));
    offset += SIZEOF(start4ByteCode);

    // Output the NALu
    MEMCPY(pGstKvsPlugin->videoCpd + offset, pSrc, spsSize);
    offset += spsSize;
    pSrc += spsSize;

    // Skip pps count
    pSrc++;

    // Read pps size
    CHK(pSrc + SIZEOF(UINT16) <= pEnd, STATUS_FORMAT_ERROR);
    ppsSize = GET_UNALIGNED_BIG_ENDIAN((PUINT16) pSrc);
    pSrc += SIZEOF(UINT16);

    CHK(offset + SIZEOF(start4ByteCode) + ppsSize < GST_PLUGIN_MAX_CPD_SIZE, STATUS_FORMAT_ERROR);
    CHK(pSrc + ppsSize <= pEnd, STATUS_FORMAT_ERROR);

    // Output the Annex-B start code
    MEMCPY(pGstKvsPlugin->videoCpd + offset, start4ByteCode, SIZEOF(start4ByteCode));
    offset += SIZEOF(start4ByteCode);

    // Output the NALu
    MEMCPY(pGstKvsPlugin->videoCpd + offset, pSrc, ppsSize);
    offset += ppsSize;

    pGstKvsPlugin->videoCpdSize = offset;

CleanUp:

    return retStatus;
}

STATUS convertCpdFromHevcToAnnexB(PGstKvsPlugin pGstKvsPlugin, PBYTE pData, UINT32 size)
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 i, naluCount, offset = 0;
    UINT16 naluUnitLen;
    BYTE start4ByteCode[] = {0x00, 0x00, 0x00, 0x01};
    PBYTE pSrc = pData, pEnd = pData + size;

    CHK(pData != NULL && pGstKvsPlugin != NULL, STATUS_NULL_ARG);
    CHK(size > 23, STATUS_FORMAT_ERROR);

    // Skip to numOfArrays and read the nalu count
    pSrc += 22;
    naluCount = *pSrc;
    pSrc++;

    for (i = 0; i < naluCount; i++) {
        // Skip array_completeness, reserved and NAL_unit_type
        pSrc += 3;

        CHK(pSrc + SIZEOF(UINT16) <= pEnd, STATUS_FORMAT_ERROR);

        // Read the naluUnitLength
        naluUnitLen = GET_UNALIGNED_BIG_ENDIAN((PUINT16) pSrc);

        pSrc += SIZEOF(UINT16);

        CHK(offset + SIZEOF(start4ByteCode) + naluUnitLen < GST_PLUGIN_MAX_CPD_SIZE, STATUS_FORMAT_ERROR);
        CHK(pSrc + naluUnitLen <= pEnd, STATUS_FORMAT_ERROR);

        // Output the Annex-B start code
        MEMCPY(pGstKvsPlugin->videoCpd + offset, start4ByteCode, SIZEOF(start4ByteCode));
        offset += SIZEOF(start4ByteCode);

        // Output the NALu
        MEMCPY(pGstKvsPlugin->videoCpd + offset, pSrc, naluUnitLen);
        offset += naluUnitLen;
        pSrc += naluUnitLen;
    }

    pGstKvsPlugin->videoCpdSize = offset;

CleanUp:

    return retStatus;
}
