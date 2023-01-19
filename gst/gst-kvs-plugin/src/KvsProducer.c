#define LOG_CLASS "KvsProducer"
#include "GstPlugin.h"

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
