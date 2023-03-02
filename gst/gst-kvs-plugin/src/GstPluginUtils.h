#ifndef __KVS_GST_PLUGIN_UTILS_H__
#define __KVS_GST_PLUGIN_UTILS_H__

#define IOT_GET_CREDENTIAL_ENDPOINT "endpoint"
#define CERTIFICATE_PATH            "cert-path"
#define PRIVATE_KEY_PATH            "key-path"
#define CA_CERT_PATH                "ca-path"
#define ROLE_ALIASES                "role-aliases"

#define DEFAULT_ADAPT_CPD_NALS                 FALSE
#define DEFAULT_ADAPT_FRAME_NALS               FALSE
#define DEFAULT_DISABLE_BUFFER_CLIPPING        FALSE
#define DEFAULT_CODEC_ID_H264                  "V_MPEG4/ISO/AVC"
#define DEFAULT_CODEC_ID_H265                  "V_MPEGH/ISO/HEVC"
#define DEFAULT_ACCESS_KEY                     "access_key"
#define DEFAULT_SECRET_KEY                     "secret_key"
#define DEFAULT_REGION                         "us-east-1"
#define DEFAULT_FILE_LOG_PATH                  ""
#define DEFAULT_API_CACHE_PERIOD               (24 * HUNDREDS_OF_NANOS_IN_AN_HOUR)
#define DEFAULT_LOG_LEVEL                      LOG_LEVEL_WARN

#define CA_CERT_PEM_FILE_EXTENSION ".pem"

#define FILE_LOGGING_BUFFER_SIZE (100 * 1024)
#define MAX_NUMBER_OF_LOG_FILES  5

#define GST_PLUGIN_MAX_CPD_SIZE (10 * 1024)

#define AVCC_VERSION_CODE       0x01
#define AVCC_NALU_LEN_MINUS_ONE 0xFF
#define AVCC_NUMBER_OF_SPS_ONE  0xE1
#define HEVC_CPD_HEADER_SIZE    23

typedef enum {
    ELEMENTARY_STREAM_NAL_FORMAT_UNKNOWN,
    ELEMENTARY_STREAM_NAL_FORMAT_ANNEX_B,
    ELEMENTARY_STREAM_NAL_FORMAT_AVCC,
    ELEMENTARY_STREAM_NAL_FORMAT_HEVC,
} ELEMENTARY_STREAM_NAL_FORMAT;

#define IS_AVCC_HEVC_CPD_NAL_FORMAT(f) (((f) == ELEMENTARY_STREAM_NAL_FORMAT_AVCC) || ((f) == ELEMENTARY_STREAM_NAL_FORMAT_HEVC))

typedef struct __GstTags GstTags;
struct __GstTags {
    UINT32 tagCount;
    Tag tags[MAX_TAG_COUNT];
};
typedef struct __GstTags* PGstTag;

typedef struct __IotInfo IotInfo;
struct __IotInfo {
    CHAR endPoint[MAX_URI_CHAR_LEN + 1];
    CHAR certPath[MAX_PATH_LEN + 1];
    CHAR privateKeyPath[MAX_PATH_LEN + 1];
    CHAR caCertPath[MAX_PATH_LEN + 1];
    CHAR roleAlias[MAX_ROLE_ALIAS_LEN + 1];
};
typedef struct __IotInfo* PIotInfo;

STATUS gstStructToTags(GstStructure*, PGstTag);
gboolean setGstTags(GQuark, const GValue*, gpointer);

STATUS gstStructToIotInfo(GstStructure*, PIotInfo);
gboolean setGstIotInfo(GQuark, const GValue*, gpointer);

STATUS traverseDirectoryPemFileScan(UINT64, DIR_ENTRY_TYPES, PCHAR, PCHAR);
STATUS lookForSslCert(PGstKvsPlugin);
STATUS initTrackData(PGstKvsPlugin);
STATUS identifyFrameNalFormat(PBYTE, UINT32, ELEMENTARY_STREAM_NAL_FORMAT*);
STATUS identifyCpdNalFormat(PBYTE, UINT32, ELEMENTARY_STREAM_NAL_FORMAT*);
STATUS convertCpdFromAvcToAnnexB(PGstKvsPlugin, PBYTE, UINT32);
STATUS convertCpdFromHevcToAnnexB(PGstKvsPlugin, PBYTE, UINT32);

#endif //__KVS_GST_PLUGIN_UTILS_H__