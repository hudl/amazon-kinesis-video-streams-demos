// Copyright 2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// SPDX-License-Identifier: Apache-2.0
//
// Portions Copyright
/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2017 <<user@hostname.org>>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * SECTION:element-kvs
 *
 * GStrteamer plugin for AWS KVS service
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 *   gst-launch-1.0
 *     autovideosrc
 *   ! videoconvert
 *   ! video/x-raw,format=I420,width=1280,height=720,framerate=30/1
 *   ! vtenc_h264_hw allow-frame-reordering=FALSE realtime=TRUE max-keyframe-interval=45 bitrate=512
 *   ! h264parse
 *   ! video/x-h264,stream-format=avc, alignment=au,width=1280,height=720,framerate=30/1
 *   ! kvsplugin stream-name="plugin-stream" max-latency=30
 * ]|
 * </refsect2>
 */

#define LOG_CLASS "GstPlugin"
#include "GstPlugin.h"

GST_DEBUG_CATEGORY_STATIC(gst_kvs_plugin_debug);
#define GST_CAT_DEFAULT gst_kvs_plugin_debug
#define GST_TYPE_KVS_PLUGIN_WEBRTC_CONNECTION_MODE (gst_kvs_plugin_connection_mode_get_type())
GType gst_kvs_plugin_connection_mode_get_type(VOID)
{
    // Need to use static. Could have used a global as well
    static GType kvsPluginWebRtcMode = 0;
    static GEnumValue enumType[] = {
        {WEBRTC_CONNECTION_MODE_DEFAULT, "Default connection mode allowing both P2P and TURN", "default"},
        {WEBRTC_CONNECTION_MODE_TURN_ONLY, "TURN only connection mode", "turn"},
        {WEBRTC_CONNECTION_MODE_P2P_ONLY, "P2P only connection mode", "p2p"},
        {0, NULL, NULL},
    };

    if (kvsPluginWebRtcMode == 0) {
        kvsPluginWebRtcMode = g_enum_register_static("WEBRTC_CONNECTION_MODE", enumType);
    }

    return kvsPluginWebRtcMode;
}

GstStaticPadTemplate audiosink_templ = GST_STATIC_PAD_TEMPLATE(
    "audio_%u", GST_PAD_SINK, GST_PAD_REQUEST,
    GST_STATIC_CAPS("audio/mpeg, mpegversion = (int) { 2, 4 }, stream-format = (string) raw, channels = (int) [ 1, MAX ], rate = (int) [ 1, MAX ] ; "
                    "audio/x-alaw, channels = (int) { 1, 2 }, rate = (int) [ 8000, 192000 ] ; "
                    "audio/x-mulaw, channels = (int) { 1, 2 }, rate = (int) [ 8000, 192000 ] ; "
                    "audio/x-opus"));

GstStaticPadTemplate videosink_templ = GST_STATIC_PAD_TEMPLATE(
    "video_%u", GST_PAD_SINK, GST_PAD_REQUEST,
    GST_STATIC_CAPS("video/x-h264, stream-format = (string) avc, alignment = (string) au, width = (int) [ 16, MAX ], height = (int) [ 16, MAX ] ; "
                    "video/x-h265, alignment = (string) au, width = (int) [ 16, MAX ], height = (int) [ 16, MAX ] ;"));

#define _init_kvs_plugin GST_DEBUG_CATEGORY_INIT(gst_kvs_plugin_debug, "kvsgstplugin", 0, "KVS GStreamer plug-in");

#define gst_kvs_plugin_parent_class parent_class

G_DEFINE_TYPE_WITH_CODE(GstKvsPlugin, gst_kvs_plugin, GST_TYPE_ELEMENT, _init_kvs_plugin);

STATUS initKinesisVideoStructs(PGstKvsPlugin pGstPlugin)
{
    STATUS retStatus = STATUS_SUCCESS;
    PCHAR pAccessKey = NULL, pSecretKey = NULL, pSessionToken = NULL;
    IotInfo iotInfo;

    CHK(pGstPlugin != NULL, STATUS_NULL_ARG);

    CHK_STATUS(initKvsWebRtc());

    // Zero out the kvs sub-structures for proper cleanup later
    MEMSET(&pGstPlugin->kvsContext, 0x00, SIZEOF(KvsContext));

    // Load the CA cert path
    lookForSslCert(pGstPlugin);

    pSessionToken = GETENV(SESSION_TOKEN_ENV_VAR);
    if (0 == STRCMP(pGstPlugin->gstParams.accessKey, DEFAULT_ACCESS_KEY)) { // if no static credential is available in plugin property.
        if (NULL == (pAccessKey = GETENV(ACCESS_KEY_ENV_VAR)) ||
            NULL == (pSecretKey = GETENV(SECRET_KEY_ENV_VAR))) { // if no static credential is available in env var.
        }
    } else {
        pAccessKey = pGstPlugin->gstParams.accessKey;
        pSecretKey = pGstPlugin->gstParams.secretKey;
    }

    if (NULL == (pGstPlugin->pRegion = GETENV(DEFAULT_REGION_ENV_VAR))) {
        pGstPlugin->pRegion = pGstPlugin->gstParams.awsRegion;
    }

    if (NULL == pGstPlugin->pRegion) {
        // Use the default
        pGstPlugin->pRegion = DEFAULT_AWS_REGION;
    }

    if (0 != STRCMP(pGstPlugin->gstParams.fileLogPath, DEFAULT_FILE_LOG_PATH)) {
        CHK_STATUS(
            createFileLogger(FILE_LOGGING_BUFFER_SIZE, MAX_NUMBER_OF_LOG_FILES, (PCHAR) FILE_LOGGER_LOG_FILE_DIRECTORY_PATH, TRUE, TRUE, NULL));
    }

    // Create the Credential Provider which will be used by both the producer and the signaling client
    // Check if we have access key then use static credential provider.
    // If we have IoT struct then use IoT credential provider.
    // If we have File then we use file credential provider.
    // We also need to set the appropriate free function pointer.
    if (pAccessKey != NULL) {
        CHK_STATUS(
            createStaticCredentialProvider(pAccessKey, 0, pSecretKey, 0, pSessionToken, 0, MAX_UINT64, &pGstPlugin->kvsContext.pCredentialProvider));
        pGstPlugin->kvsContext.freeCredentialProviderFn = freeStaticCredentialProvider;
    } else if (pGstPlugin->gstParams.iotCertificate != NULL) {
        CHK_STATUS(gstStructToIotInfo(pGstPlugin->gstParams.iotCertificate, &iotInfo));
        CHK_STATUS(createCurlIotCredentialProvider(iotInfo.endPoint, iotInfo.certPath, iotInfo.privateKeyPath, iotInfo.caCertPath, iotInfo.roleAlias,
                                                   pGstPlugin->gstParams.channelName, &pGstPlugin->kvsContext.pCredentialProvider));
        pGstPlugin->kvsContext.freeCredentialProviderFn = freeIotCredentialProvider;
    }

CleanUp:

    CHK_LOG_ERR(retStatus);

    return retStatus;
}

VOID gst_kvs_plugin_class_init(GstKvsPluginClass* klass)
{
    GObjectClass* gobject_class;
    GstElementClass* gstelement_class;

    gobject_class = G_OBJECT_CLASS(klass);
    gstelement_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = gst_kvs_plugin_set_property;
    gobject_class->get_property = gst_kvs_plugin_get_property;
    gobject_class->finalize = gst_kvs_plugin_finalize;

    g_object_class_install_property(gobject_class, PROP_CHANNEL_NAME,
                                    g_param_spec_string("channel-name", "Channel Name", "Name of the signaling channel", DEFAULT_CHANNEL_NAME,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_WEBRTC_CONNECTION_MODE,
                                    g_param_spec_enum("webrtc-connection-mode", "WebRTC connection mode",
                                                      "WebRTC connection mode - Default, Turn only, P2P only",
                                                      GST_TYPE_KVS_PLUGIN_WEBRTC_CONNECTION_MODE,
                                                      DEFAULT_WEBRTC_CONNECTION_MODE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_CONTENT_TYPE,
                                    g_param_spec_string("content-type", "Content Type", "content type", MKV_H264_CONTENT_TYPE,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_ADAPT_CPD_NALS_TO_AVC,
                                    g_param_spec_boolean("adapt-cpd-nals", "Whether to adapt CPD NALs from Annex-B to AvCC format", "Adapt CPD NALs",
                                                         DEFAULT_ADAPT_CPD_NALS, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_ADAPT_FRAME_NALS_TO_AVC,
                                    g_param_spec_boolean("adapt-frame-nals", "Whether to adapt Frame NALs from Annex-B to AvCC format",
                                                         "Adapt Frame NALs", DEFAULT_ADAPT_FRAME_NALS,
                                                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_CODEC_ID,
                                    g_param_spec_string("codec-id", "Codec ID", "Codec ID",
                                                        DEFAULT_CODEC_ID_H264,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_ACCESS_KEY,
                                    g_param_spec_string("access-key", "Access Key", "AWS Access Key", DEFAULT_ACCESS_KEY,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_SECRET_KEY,
                                    g_param_spec_string("secret-key", "Secret Key", "AWS Secret Key", DEFAULT_SECRET_KEY,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_AWS_REGION,
                                    g_param_spec_string("aws-region", "AWS Region", "AWS Region",
                                                        DEFAULT_REGION,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_FILE_LOG_PATH,
                                    g_param_spec_string("log-path", "Log path",
                                                        "Specifying the directory where the file-based logger will store the files. ",
                                                        DEFAULT_FILE_LOG_PATH, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_IOT_CERTIFICATE,
                                    g_param_spec_boxed("iot-certificate", "Iot Certificate", "Use aws iot certificate to obtain credentials",
                                                       GST_TYPE_STRUCTURE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_DISABLE_BUFFER_CLIPPING,
                                    g_param_spec_boolean("disable-buffer-clipping", "Disable Buffer Clipping",
                                                         "Set to true only if your src/mux elements produce GST_CLOCK_TIME_NONE for segment start times.  It is non-standard "
                                                         "behavior to set this to true, only use if there are known issues with your src/mux segment start/stop times.",
                                                         DEFAULT_DISABLE_BUFFER_CLIPPING,
                                                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_TRICKLE_ICE,
                                    g_param_spec_boolean("trickle-ice", "Enable Trickle ICE", "Whether to use tricle ICE mode",
                                                         DEFAULT_TRICKLE_ICE_MODE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_WEBRTC_CONNECT,
                                    g_param_spec_boolean("connect-webrtc", "WebRTC Connect", "Whether to connect to WebRTC signaling channel",
                                                         DEFAULT_WEBRTC_CONNECT, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(gstelement_class, "KVS Plugin", "Sink/Video/Network", "GStreamer AWS KVS plugin",
                                          "AWS KVS <kinesis-video-support@amazon.com>");

    gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&audiosink_templ));
    gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&videosink_templ));

    gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_kvs_plugin_change_state);
    gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_kvs_plugin_request_new_pad);
    gstelement_class->release_pad = GST_DEBUG_FUNCPTR(gst_kvs_plugin_release_pad);
}

VOID gst_kvs_plugin_init(PGstKvsPlugin pGstKvsPlugin)
{
    pGstKvsPlugin->collect = gst_collect_pads_new();
    gst_collect_pads_set_buffer_function(pGstKvsPlugin->collect, GST_DEBUG_FUNCPTR(gst_kvs_plugin_handle_buffer), pGstKvsPlugin);
    gst_collect_pads_set_event_function(pGstKvsPlugin->collect, GST_DEBUG_FUNCPTR(gst_kvs_plugin_handle_plugin_event), pGstKvsPlugin);

    pGstKvsPlugin->numStreams = 0;
    pGstKvsPlugin->numAudioStreams = 0;
    pGstKvsPlugin->numVideoStreams = 0;

  
    pGstKvsPlugin->gstParams.channelName = g_strdup(DEFAULT_CHANNEL_NAME);
    pGstKvsPlugin->gstParams.disableBufferClipping = DEFAULT_DISABLE_BUFFER_CLIPPING;
    pGstKvsPlugin->gstParams.codecId = g_strdup(DEFAULT_CODEC_ID_H264);
    pGstKvsPlugin->gstParams.accessKey = g_strdup(DEFAULT_ACCESS_KEY);
    pGstKvsPlugin->gstParams.secretKey = g_strdup(DEFAULT_SECRET_KEY);
    pGstKvsPlugin->gstParams.awsRegion = g_strdup(DEFAULT_REGION);
    pGstKvsPlugin->gstParams.fileLogPath = g_strdup(DEFAULT_FILE_LOG_PATH);
    pGstKvsPlugin->audioCodecId = g_strdup(DEFAULT_AUDIO_CODEC_ID_AAC);
    pGstKvsPlugin->gstParams.trickleIce = DEFAULT_TRICKLE_ICE_MODE;
    pGstKvsPlugin->gstParams.webRtcConnect = DEFAULT_WEBRTC_CONNECT;

    ATOMIC_STORE_BOOL(&pGstKvsPlugin->connectWebRtc, pGstKvsPlugin->gstParams.webRtcConnect);

    pGstKvsPlugin->adaptedFrameBufSize = 0;
    pGstKvsPlugin->pAdaptedFrameBuf = NULL;

    // Mark plugin as sink
    GST_OBJECT_FLAG_SET(pGstKvsPlugin, GST_ELEMENT_FLAG_SINK);
}

VOID gst_kvs_plugin_finalize(GObject* object)
{
    PGstKvsPlugin pGstKvsPlugin = GST_KVS_PLUGIN(object);

    if (pGstKvsPlugin == NULL) {
        return;
    }
    
    freeGstKvsWebRtcPlugin(pGstKvsPlugin);

    // Last object to be freed
    if (pGstKvsPlugin->kvsContext.pCredentialProvider != NULL) {
        pGstKvsPlugin->kvsContext.freeCredentialProviderFn(&pGstKvsPlugin->kvsContext.pCredentialProvider);
    }

    gst_object_unref(pGstKvsPlugin->collect);
    g_free(pGstKvsPlugin->gstParams.channelName);
    g_free(pGstKvsPlugin->gstParams.contentType);
    g_free(pGstKvsPlugin->gstParams.codecId);
    g_free(pGstKvsPlugin->gstParams.secretKey);
    g_free(pGstKvsPlugin->gstParams.accessKey);
    g_free(pGstKvsPlugin->audioCodecId);
    g_free(pGstKvsPlugin->gstParams.fileLogPath);

    if (pGstKvsPlugin->gstParams.iotCertificate != NULL) {
        gst_structure_free(pGstKvsPlugin->gstParams.iotCertificate);
        pGstKvsPlugin->gstParams.iotCertificate = NULL;
    }

    SAFE_MEMFREE(pGstKvsPlugin->pAdaptedFrameBuf);

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

VOID gst_kvs_plugin_set_property(GObject* object, guint propId, const GValue* value, GParamSpec* pspec)
{
    PGstKvsPlugin pGstKvsPlugin = GST_KVS_PLUGIN(object);

    if (pGstKvsPlugin == NULL) {
        return;
    }

    switch (propId) {
        case PROP_CHANNEL_NAME:
            g_free(pGstKvsPlugin->gstParams.channelName);
            pGstKvsPlugin->gstParams.channelName = g_strdup(g_value_get_string(value));
            break;
        case PROP_WEBRTC_CONNECTION_MODE:
            pGstKvsPlugin->gstParams.connectionMode = (WEBRTC_CONNECTION_MODE) g_value_get_enum(value);
            break;
        case PROP_CONTENT_TYPE:
            g_free(pGstKvsPlugin->gstParams.contentType);
            pGstKvsPlugin->gstParams.contentType = g_strdup(g_value_get_string(value));
            break;
        case PROP_ADAPT_CPD_NALS_TO_AVC:
            pGstKvsPlugin->gstParams.adaptCpdNals = g_value_get_boolean(value);
            break;
        case PROP_ADAPT_FRAME_NALS_TO_AVC:
            pGstKvsPlugin->gstParams.adaptFrameNals = g_value_get_boolean(value);
            break;
        case PROP_CODEC_ID:
            g_free(pGstKvsPlugin->gstParams.codecId);
            pGstKvsPlugin->gstParams.codecId = g_strdup(g_value_get_string(value));
            break;
        case PROP_ACCESS_KEY:
            g_free(pGstKvsPlugin->gstParams.accessKey);
            pGstKvsPlugin->gstParams.accessKey = g_strdup(g_value_get_string(value));
            break;
        case PROP_SECRET_KEY:
            g_free(pGstKvsPlugin->gstParams.secretKey);
            pGstKvsPlugin->gstParams.secretKey = g_strdup(g_value_get_string(value));
            break;
        case PROP_AWS_REGION:
            g_free(pGstKvsPlugin->gstParams.awsRegion);
            pGstKvsPlugin->gstParams.awsRegion = g_strdup(g_value_get_string(value));
            break;
        case PROP_FILE_LOG_PATH:
            g_free(pGstKvsPlugin->gstParams.fileLogPath);
            pGstKvsPlugin->gstParams.fileLogPath = g_strdup(g_value_get_string(value));
            break;
        case PROP_IOT_CERTIFICATE: {
            const GstStructure* iotStruct = gst_value_get_structure(value);

            if (pGstKvsPlugin->gstParams.iotCertificate != NULL) {
                gst_structure_free(pGstKvsPlugin->gstParams.iotCertificate);
            }

            pGstKvsPlugin->gstParams.iotCertificate = (iotStruct != NULL) ? gst_structure_copy(iotStruct) : NULL;
            break;
        }
        case PROP_DISABLE_BUFFER_CLIPPING:
            pGstKvsPlugin->gstParams.disableBufferClipping = g_value_get_boolean(value);
            break;
        case PROP_TRICKLE_ICE:
            pGstKvsPlugin->gstParams.trickleIce = g_value_get_boolean(value);
            break;
        case PROP_WEBRTC_CONNECT:
            pGstKvsPlugin->gstParams.webRtcConnect = g_value_get_boolean(value);
            ATOMIC_STORE_BOOL(&pGstKvsPlugin->connectWebRtc, pGstKvsPlugin->gstParams.webRtcConnect);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propId, pspec);
            break;
    }
}

VOID gst_kvs_plugin_get_property(GObject* object, guint propId, GValue* value, GParamSpec* pspec)
{
    PGstKvsPlugin pGstKvsPlugin = GST_KVS_PLUGIN(object);

    if (pGstKvsPlugin == NULL) {
        return;
    }

    switch (propId) {
        case PROP_CHANNEL_NAME:
            g_value_set_string(value, pGstKvsPlugin->gstParams.channelName);
            break;
        case PROP_WEBRTC_CONNECTION_MODE:
            g_value_set_enum(value, pGstKvsPlugin->gstParams.connectionMode);
            break;
        case PROP_CONTENT_TYPE:
            g_value_set_string(value, pGstKvsPlugin->gstParams.contentType);
            break;
        case PROP_ADAPT_CPD_NALS_TO_AVC:
            g_value_set_boolean(value, pGstKvsPlugin->gstParams.adaptCpdNals);
            break;
        case PROP_ADAPT_FRAME_NALS_TO_AVC:
            g_value_set_boolean(value, pGstKvsPlugin->gstParams.adaptFrameNals);
            break;
        case PROP_CODEC_ID:
            g_value_set_string(value, pGstKvsPlugin->gstParams.codecId);
            break;
        case PROP_ACCESS_KEY:
            g_value_set_string(value, pGstKvsPlugin->gstParams.accessKey);
            break;
        case PROP_SECRET_KEY:
            g_value_set_string(value, pGstKvsPlugin->gstParams.secretKey);
            break;
        case PROP_AWS_REGION:
            g_value_set_string(value, pGstKvsPlugin->gstParams.awsRegion);
            break;
        case PROP_FILE_LOG_PATH:
            g_value_set_string(value, pGstKvsPlugin->gstParams.fileLogPath);
            break;
        case PROP_IOT_CERTIFICATE:
            gst_value_set_structure(value, pGstKvsPlugin->gstParams.iotCertificate);
            break;
        case PROP_DISABLE_BUFFER_CLIPPING:
            g_value_set_boolean(value, pGstKvsPlugin->gstParams.disableBufferClipping);
            break;
        case PROP_TRICKLE_ICE:
            g_value_set_boolean(value, pGstKvsPlugin->gstParams.trickleIce);
            break;
        case PROP_WEBRTC_CONNECT:
            g_value_set_boolean(value, pGstKvsPlugin->gstParams.webRtcConnect);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propId, pspec);
            break;
    }
}

gboolean gst_kvs_plugin_handle_plugin_event(GstCollectPads* pads, GstCollectData* track_data, GstEvent* event, gpointer user_data)
{
    STATUS retStatus = STATUS_SUCCESS;
    PGstKvsPlugin pGstKvsPlugin = GST_KVS_PLUGIN(user_data);
    PGstKvsPluginTrackData pTrackData = (PGstKvsPluginTrackData) track_data;
    GstCaps* gstcaps = NULL;
    UINT64 trackId = pTrackData->trackId;
    BYTE cpd[GST_PLUGIN_MAX_CPD_SIZE];
    UINT32 cpdSize;
    gchar* gstCpd = NULL;
    gboolean persistent, connectWeRtc;
    const GstStructure* gstStruct;
    PCHAR pName, pVal;
    UINT32 nalFlags = NAL_ADAPTATION_FLAG_NONE;

    gint samplerate = 0, channels = 0;
    const gchar* mediaType;

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS:
            gst_event_parse_caps(event, &gstcaps);
            GstStructure* gststructforcaps = gst_caps_get_structure(gstcaps, 0);
            mediaType = gst_structure_get_name(gststructforcaps);

            if (0 == STRCMP(mediaType, GSTREAMER_MEDIA_TYPE_ALAW) || 0 == STRCMP(mediaType, GSTREAMER_MEDIA_TYPE_MULAW)) {
                KVS_PCM_FORMAT_CODE format = KVS_PCM_FORMAT_CODE_MULAW;

                gst_structure_get_int(gststructforcaps, "rate", &samplerate);
                gst_structure_get_int(gststructforcaps, "channels", &channels);

                if (samplerate == 0 || channels == 0) {
                    GST_ERROR_OBJECT(pGstKvsPlugin, "Missing channels/sample rate on caps");
                    CHK(FALSE, STATUS_INVALID_OPERATION);
                }

                if (0 == STRCMP(mediaType, GSTREAMER_MEDIA_TYPE_ALAW)) {
                    format = KVS_PCM_FORMAT_CODE_ALAW;
                } else {
                    format = KVS_PCM_FORMAT_CODE_MULAW;
                }

                if (STATUS_FAILED(mkvgenGeneratePcmCpd(format, (UINT32) samplerate, (UINT16) channels, (PBYTE) cpd, KVS_PCM_CPD_SIZE_BYTE))) {
                    GST_ERROR_OBJECT(pGstKvsPlugin, "Failed to generate pcm cpd");
                    CHK(FALSE, STATUS_INVALID_OPERATION);
                }
            
            } else if (!pGstKvsPlugin->trackCpdReceived[trackId] && gst_structure_has_field(gststructforcaps, "codec_data")) {
                const GValue* gstStreamFormat = gst_structure_get_value(gststructforcaps, "codec_data");
                gstCpd = gst_value_serialize(gstStreamFormat);

                // Convert hex cpd to byte array by getting the size, allocating and converting
                CHK_STATUS(hexDecode(gstCpd, 0, NULL, &cpdSize));
                CHK(cpdSize < GST_PLUGIN_MAX_CPD_SIZE, STATUS_INVALID_ARG_LEN);
                CHK_STATUS(hexDecode(gstCpd, 0, cpd, &cpdSize));

                // Need to detect the CPD format first time only for video
                if (trackId == DEFAULT_VIDEO_TRACK_ID && pGstKvsPlugin->detectedCpdFormat == ELEMENTARY_STREAM_NAL_FORMAT_UNKNOWN) {
                    CHK_STATUS(identifyCpdNalFormat(cpd, cpdSize, &pGstKvsPlugin->detectedCpdFormat));

                    // We should store the CPD as is if it's in Annex-B format and convert from AvCC/HEVC
                    // The stored CPD will be used for WebRTC RTP stream prefixing each I-frame if it's not
                    if (pGstKvsPlugin->detectedCpdFormat == ELEMENTARY_STREAM_NAL_FORMAT_AVCC) {
                        // Convert from AvCC to Annex-B format
                        // NOTE: This will also store the data
                        CHK_STATUS(convertCpdFromAvcToAnnexB(pGstKvsPlugin, cpd, cpdSize));
                    } else if (pGstKvsPlugin->detectedCpdFormat == ELEMENTARY_STREAM_NAL_FORMAT_HEVC) {
                        // Convert from HEVC to Annex-B format
                        // NOTE: This will also store the data
                        CHK_STATUS(convertCpdFromHevcToAnnexB(pGstKvsPlugin, cpd, cpdSize));
                    } else {
                        // Store it for use with WebRTC where we will pre-pend the Annex-B CPD to each I-frame
                        // if the Annex-B format I-frame doesn't have it already pre-pended
                        MEMCPY(pGstKvsPlugin->videoCpd, cpd, cpdSize);
                        pGstKvsPlugin->videoCpdSize = cpdSize;
                    }

                    // Prior to setting the CPD we need to set the flags
                    if (pGstKvsPlugin->gstParams.adaptCpdNals && pGstKvsPlugin->detectedCpdFormat == ELEMENTARY_STREAM_NAL_FORMAT_ANNEX_B) {
                        nalFlags |= NAL_ADAPTATION_ANNEXB_CPD_NALS;
                    }

                    if (pGstKvsPlugin->gstParams.adaptFrameNals && pGstKvsPlugin->detectedCpdFormat == ELEMENTARY_STREAM_NAL_FORMAT_ANNEX_B) {
                        nalFlags |= NAL_ADAPTATION_ANNEXB_NALS;
                    }
                }

                // Mark as received
                pGstKvsPlugin->trackCpdReceived[trackId] = TRUE;
            }

            gst_event_unref(event);
            event = NULL;

            break;

        case GST_EVENT_CUSTOM_DOWNSTREAM:
            gstStruct = gst_event_get_structure(event);

            if (gst_structure_has_name(gstStruct, KVS_ADD_METADATA_G_STRUCT_NAME) &&
                NULL != (pName = (PCHAR) gst_structure_get_string(gstStruct, KVS_ADD_METADATA_NAME)) &&
                NULL != (pVal = (PCHAR) gst_structure_get_string(gstStruct, KVS_ADD_METADATA_VALUE)) &&
                gst_structure_get_boolean(gstStruct, KVS_ADD_METADATA_PERSISTENT, &persistent)) {
                DLOGD("received " KVS_ADD_METADATA_G_STRUCT_NAME " event");

                gst_event_unref(event);
                event = NULL;
            } else if (gst_structure_has_name(gstStruct, KVS_CONNECT_WEBRTC_G_STRUCT_NAME) &&
                       gst_structure_get_boolean(gstStruct, KVS_CONNECT_WEBRTC_FIELD, &connectWeRtc)) {
                DLOGD("received " KVS_CONNECT_WEBRTC_G_STRUCT_NAME " event");

                ATOMIC_STORE_BOOL(&pGstKvsPlugin->connectWebRtc, connectWeRtc);

                gst_event_unref(event);
                event = NULL;
            }

            break;

        default:
            break;
    }

CleanUp:

    if (event != NULL) {
        gst_collect_pads_event_default(pads, track_data, event, FALSE);
    }

    if (gstCpd != NULL) {
        g_free(gstCpd);
    }

    if (STATUS_FAILED(retStatus)) {
        GST_ELEMENT_ERROR(pGstKvsPlugin, STREAM, FAILED, (NULL), ("Failed to handle event"));
    }

    return STATUS_SUCCEEDED(retStatus);
}

BOOL print_droppable_reason(BOOL isDroppable, GstBuffer* buffer) {
    if (isDroppable) {
        static int droppable_count = 0;
        char text[256];
        int pos = sprintf(text, "Droppable frame no: %d. Reason: ", ++droppable_count);

        if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_CORRUPTED)) {
            sprintf(text + pos, "GST_BUFFER_FLAG_CORRUPTED");
        } else if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DECODE_ONLY)) {
            sprintf(text + pos, "GST_BUFFER_FLAG_DECODE_ONLY");
        } else if (GST_BUFFER_FLAGS(buffer) == GST_BUFFER_FLAG_DISCONT) {
            sprintf(text + pos, "GST_BUFFER_FLAG_DISCONT");
        } else if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT) &&
                   GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
            sprintf(text + pos, "GST_BUFFER_FLAG_DISCONT && GST_BUFFER_FLAG_DELTA_UNIT");
        } else if (!GST_BUFFER_PTS_IS_VALID(buffer)) {
            sprintf(text + pos, "!GST_BUFFER_PTS_IS_VALID");
        } else {
            sprintf(text + pos, "UNKNOWN");
        }
        DLOGW("%s\n", text);
    }
}

GstFlowReturn gst_kvs_plugin_handle_buffer(GstCollectPads* pads, GstCollectData* track_data, GstBuffer* buf, gpointer user_data)
{
    PGstKvsPlugin pGstKvsPlugin = GST_KVS_PLUGIN(user_data);
    GstFlowReturn ret = GST_FLOW_OK;
    PGstKvsPluginTrackData pTrackData = (PGstKvsPluginTrackData) track_data;

    BOOL isDroppable, delta;
    GstMessage* message;
    UINT64 trackId;
    FRAME_FLAGS frameFlags = FRAME_FLAG_NONE;
    GstMapInfo info;
    STATUS status;
    Frame frame;

    info.data = NULL;

    // eos reached
    if (buf == NULL && pTrackData == NULL) {

        // send out eos message to gstreamer bus
        message = gst_message_new_eos(GST_OBJECT_CAST(pGstKvsPlugin));
        gst_element_post_message(GST_ELEMENT_CAST(pGstKvsPlugin), message);

        ret = GST_FLOW_EOS;
        goto CleanUp;
    }

    isDroppable = GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_CORRUPTED) || GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DECODE_ONLY) ||
        (GST_BUFFER_FLAGS(buf) == GST_BUFFER_FLAG_DISCONT) ||
        (GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DISCONT) && GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT)) ||
        // drop if buffer contains header and has invalid timestamp
        (GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_HEADER) && (!GST_BUFFER_PTS_IS_VALID(buf) || !GST_BUFFER_DTS_IS_VALID(buf)));

    if (isDroppable) {
        print_droppable_reason(isDroppable, buf);
        goto CleanUp;
    }

    pGstKvsPlugin->lastDts = buf->dts;
    trackId = pTrackData->trackId;

    if (!gst_buffer_map(buf, &info, GST_MAP_READ)) {
        goto CleanUp;
    }

    delta = GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT);

    switch (pGstKvsPlugin->mediaType) {
        case GST_PLUGIN_MEDIA_TYPE_AUDIO_ONLY:
        case GST_PLUGIN_MEDIA_TYPE_VIDEO_ONLY:
            if (!delta) {
                frameFlags = FRAME_FLAG_KEY_FRAME;
            }
            break;
        case GST_PLUGIN_MEDIA_TYPE_AUDIO_VIDEO:
            if (!delta && pTrackData->trackType == MKV_TRACK_INFO_TYPE_VIDEO) {
                frameFlags = FRAME_FLAG_KEY_FRAME;
            }
            break;
    }

    if (pGstKvsPlugin->firstPts == GST_CLOCK_TIME_NONE) {
        pGstKvsPlugin->firstPts = buf->pts;
    }

    if (pGstKvsPlugin->startTime == GST_CLOCK_TIME_NONE) {
        pGstKvsPlugin->startTime = GETTIME() * DEFAULT_TIME_UNIT_IN_NANOS;
    }

    buf->pts += pGstKvsPlugin->startTime - pGstKvsPlugin->firstPts;
    buf->pts -= pGstKvsPlugin->firstPts;


    frame.version = FRAME_CURRENT_VERSION;
    frame.flags = frameFlags;
    frame.index = pGstKvsPlugin->frameCount;
    frame.decodingTs = buf->dts / DEFAULT_TIME_UNIT_IN_NANOS;
    frame.presentationTs = buf->pts / DEFAULT_TIME_UNIT_IN_NANOS;
    frame.trackId = trackId;
    frame.size = info.size;
    frame.frameData = info.data;
    frame.duration = 0;

    // Need to produce the frame into peer connections
    // Check whether the frame is in AvCC/HEVC and set the flag to adapt the
    // bits to Annex-B format for RTP
    if (STATUS_FAILED(status = putFrameToWebRtcPeers(pGstKvsPlugin, &frame, pGstKvsPlugin->detectedCpdFormat))) {
        DLOGW("Failed to put frame to peer connections with 0x%08x", status);
    }

    pGstKvsPlugin->frameCount++;

CleanUp:

    if (info.data != NULL) {
        gst_buffer_unmap(buf, &info);
    }

    if (buf != NULL) {
        gst_buffer_unref(buf);
    }

    return ret;
}

GstPad* gst_kvs_plugin_request_new_pad(GstElement* element, GstPadTemplate* templ, const gchar* req_name, const GstCaps* caps)
{
    GstElementClass* klass = GST_ELEMENT_GET_CLASS(element);
    PGstKvsPlugin pGstKvsPlugin = GST_KVS_PLUGIN(element);

    gchar* name = NULL;
    GstPad* newpad = NULL;
    const gchar* padName = NULL;
    MKV_TRACK_INFO_TYPE trackType = MKV_TRACK_INFO_TYPE_VIDEO;
    gboolean locked = TRUE;
    PGstKvsPluginTrackData pTrackData;

    if (req_name != NULL) {
        GST_WARNING_OBJECT(pGstKvsPlugin, "Custom pad name not supported");
    }

    // Check if the pad template is supported
    if (templ == gst_element_class_get_pad_template(klass, "audio_%u")) {
        if (pGstKvsPlugin->numAudioStreams == 1) {
            GST_ERROR_OBJECT(pGstKvsPlugin, "Can not have more than one audio stream.");
            goto CleanUp;
        }

        name = g_strdup_printf("audio_%u", pGstKvsPlugin->numAudioStreams++);
        padName = name;
        trackType = MKV_TRACK_INFO_TYPE_AUDIO;

    } else if (templ == gst_element_class_get_pad_template(klass, "video_%u")) {
        if (pGstKvsPlugin->numVideoStreams == 1) {
            GST_ERROR_OBJECT(pGstKvsPlugin, "Can not have more than one video stream.");
            goto CleanUp;
        }

        name = g_strdup_printf("video_%u", pGstKvsPlugin->numVideoStreams++);
        padName = name;
        trackType = MKV_TRACK_INFO_TYPE_VIDEO;

    } else {
        GST_WARNING_OBJECT(pGstKvsPlugin, "Invalid template!");
        goto CleanUp;
    }

    if (pGstKvsPlugin->numVideoStreams > 0 && pGstKvsPlugin->numAudioStreams > 0) {
        pGstKvsPlugin->mediaType = GST_PLUGIN_MEDIA_TYPE_AUDIO_VIDEO;
    } else if (pGstKvsPlugin->numVideoStreams > 0) {
        pGstKvsPlugin->mediaType = GST_PLUGIN_MEDIA_TYPE_VIDEO_ONLY;
    } else {
        pGstKvsPlugin->mediaType = GST_PLUGIN_MEDIA_TYPE_AUDIO_ONLY;
    }

    pGstKvsPlugin->onDataChannel = onDataChannel;

    newpad = GST_PAD_CAST(g_object_new(GST_TYPE_PAD, "name", padName, "direction", templ->direction, "template", templ, NULL));

    pTrackData =
        (PGstKvsPluginTrackData) gst_collect_pads_add_pad(pGstKvsPlugin->collect, GST_PAD(newpad), SIZEOF(GstKvsPluginTrackData), NULL, locked);

    pTrackData->pGstKvsPlugin = pGstKvsPlugin;
    pTrackData->trackType = trackType;
    pTrackData->trackId = DEFAULT_VIDEO_TRACK_ID;

    if (!gst_element_add_pad(element, GST_PAD(newpad))) {
        gst_object_unref(newpad);
        newpad = NULL;
        GST_WARNING_OBJECT(pGstKvsPlugin, "Adding the new pad '%s' failed", padName);
        goto CleanUp;
    }

    pGstKvsPlugin->numStreams++;


CleanUp:

    g_free(name);
    return newpad;
}

VOID gst_kvs_plugin_release_pad(GstElement* element, GstPad* pad)
{
    PGstKvsPlugin pGstKvsPlugin = GST_KVS_PLUGIN(GST_PAD_PARENT(pad));
    GSList* walk;

    if (pGstKvsPlugin == NULL) {
        return;
    }

    // when a pad is released, check whether it's audio or video and keep track of the stream count
    for (walk = pGstKvsPlugin->collect->data; walk != NULL; walk = g_slist_next(walk)) {
        GstCollectData* cData;
        cData = (GstCollectData*) walk->data;

        if (cData->pad == pad) {
            PGstKvsPluginTrackData trackData;
            trackData = (PGstKvsPluginTrackData) walk->data;
            if (trackData->trackType == MKV_TRACK_INFO_TYPE_VIDEO) {
                pGstKvsPlugin->numVideoStreams--;
            } else if (trackData->trackType == MKV_TRACK_INFO_TYPE_AUDIO) {
                pGstKvsPlugin->numAudioStreams--;
            }
        }
    }

    gst_collect_pads_remove_pad(pGstKvsPlugin->collect, pad);
    if (gst_element_remove_pad(element, pad)) {
        pGstKvsPlugin->numStreams--;
    }
}

GstStateChangeReturn gst_kvs_plugin_change_state(GstElement* element, GstStateChange transition)
{
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
    PGstKvsPlugin pGstKvsPlugin = GST_KVS_PLUGIN(element);
    STATUS status = STATUS_SUCCESS;

    switch (transition) {
        case GST_STATE_CHANGE_NULL_TO_READY:
            if (STATUS_FAILED(status = initKinesisVideoStructs(pGstKvsPlugin))) {
                DLOGE("Failed to initialize KVS structures with 0x%08x", status);
                ret = GST_STATE_CHANGE_FAILURE;
                goto CleanUp;
            }

            if (STATUS_FAILED(status = initTrackData(pGstKvsPlugin))) {
                DLOGE("Failed to initialize track with 0x%08x", status);
                ret = GST_STATE_CHANGE_FAILURE;
                goto CleanUp;
            }
            pGstKvsPlugin->firstPts = GST_CLOCK_TIME_NONE;
            pGstKvsPlugin->frameCount = 0;
            
            pGstKvsPlugin->detectedCpdFormat = ELEMENTARY_STREAM_NAL_FORMAT_UNKNOWN;

            // This needs to happen after we've read in ALL of the properties
            
            if (!pGstKvsPlugin->gstParams.disableBufferClipping) {
                gst_collect_pads_set_clip_function(pGstKvsPlugin->collect, GST_DEBUG_FUNCPTR(gst_collect_pads_clip_running_time), pGstKvsPlugin);
            }
            if (STATUS_FAILED(status = initKinesisVideoWebRtc(pGstKvsPlugin))) {
                DLOGE("Failed to initialize KVS signaling client with 0x%08x", status);
                ret = GST_STATE_CHANGE_FAILURE;
                goto CleanUp;
            }

            // Schedule the WebRTC master session servicing periodic routine
            if (STATUS_FAILED(status = timerQueueAddTimer(pGstKvsPlugin->kvsContext.timerQueueHandle, GST_PLUGIN_SERVICE_ROUTINE_START,
                                                          GST_PLUGIN_SERVICE_ROUTINE_PERIOD, sessionServiceHandler, (UINT64) pGstKvsPlugin,
                                                          &pGstKvsPlugin->serviceRoutineTimerId))) {
                DLOGE("Failed to schedule WebRTC service routine with 0x%08x", status);
                ret = GST_STATE_CHANGE_FAILURE;
                goto CleanUp;
            }

            break;
        case GST_STATE_CHANGE_READY_TO_PAUSED:
            gst_collect_pads_start(pGstKvsPlugin->collect);
            break;
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            gst_collect_pads_stop(pGstKvsPlugin->collect);
            break;
        default:
            break;
    }

    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

CleanUp:

    if (ret != GST_STATE_CHANGE_SUCCESS) {
        GST_ELEMENT_ERROR(pGstKvsPlugin, LIBRARY, INIT, (NULL), ("Failed to initialize with 0x%08x", status));
    }

    return ret;
}

GST_DEBUG_CATEGORY(kvs_debug);

static gboolean plugin_init(GstPlugin* plugin)
{
    if (!gst_element_register(plugin, "kvswebrtcplugin", GST_RANK_PRIMARY + 10, GST_TYPE_KVS_PLUGIN)) {
        return FALSE;
    }

    GST_DEBUG_CATEGORY_INIT(kvs_debug, "kvs", 0, "KVS plugin elements");
    return TRUE;
}

STATUS initTrackData(PGstKvsPlugin pGstKvsPlugin)
{
    STATUS retStatus = STATUS_SUCCESS;
    GSList* walk;
    GstCaps* caps;
    gchar* videoContentType = NULL;
    gchar* audioContentType = NULL;
    const gchar* mediaType;

    for (walk = pGstKvsPlugin->collect->data; walk != NULL; walk = g_slist_next(walk)) {
        PGstKvsPluginTrackData pTrackData = (PGstKvsPluginTrackData) walk->data;

        if (pTrackData->trackType == MKV_TRACK_INFO_TYPE_VIDEO) {
            if (pGstKvsPlugin->mediaType == GST_PLUGIN_MEDIA_TYPE_AUDIO_VIDEO) {
                pTrackData->trackId = DEFAULT_VIDEO_TRACK_ID;
            }

            GstCollectData* collect_data = (GstCollectData*) walk->data;

            // extract media type from GstCaps to check whether it's h264 or h265
            caps = gst_pad_get_allowed_caps(collect_data->pad);
            mediaType = gst_structure_get_name(gst_caps_get_structure(caps, 0));
            if (STRNCMP(mediaType, GSTREAMER_MEDIA_TYPE_H264, MAX_GSTREAMER_MEDIA_TYPE_LEN) == 0) {
                // default codec id is for h264 video.
                videoContentType = g_strdup(VIDEO_H264_CONTENT_TYPE);
            } else if (STRNCMP(mediaType, GSTREAMER_MEDIA_TYPE_H265, MAX_GSTREAMER_MEDIA_TYPE_LEN) == 0) {
                g_free(pGstKvsPlugin->gstParams.codecId);
                pGstKvsPlugin->gstParams.codecId = g_strdup(DEFAULT_CODEC_ID_H265);
                videoContentType = g_strdup(VIDEO_H265_CONTENT_TYPE);
            } else {
                // no-op, should result in a caps negotiation error before getting here.
                DLOGE("Error, media type %s not accepted by plugin", mediaType);
                CHK(FALSE, STATUS_INVALID_ARG);
            }
            gst_caps_unref(caps);

        } else if (pTrackData->trackType == MKV_TRACK_INFO_TYPE_AUDIO) {
            if (pGstKvsPlugin->mediaType == GST_PLUGIN_MEDIA_TYPE_AUDIO_VIDEO) {
                pTrackData->trackId = DEFAULT_AUDIO_TRACK_ID;
            }

            GstCollectData* collect_data = (GstCollectData*) walk->data;

            // extract media type from GstCaps to check whether it's h264 or h265
            caps = gst_pad_get_allowed_caps(collect_data->pad);
            mediaType = gst_structure_get_name(gst_caps_get_structure(caps, 0));
            if (STRNCMP(mediaType, GSTREAMER_MEDIA_TYPE_AAC, MAX_GSTREAMER_MEDIA_TYPE_LEN) == 0) {
                // default codec id is for aac audio.
                audioContentType = g_strdup(AUDIO_MULAW_CONTENT_TYPE);
            } else if (STRNCMP(mediaType, GSTREAMER_MEDIA_TYPE_ALAW, MAX_GSTREAMER_MEDIA_TYPE_LEN) == 0) {
                g_free(pGstKvsPlugin->audioCodecId);
                pGstKvsPlugin->audioCodecId = g_strdup(DEFAULT_AUDIO_CODEC_ID_PCM);
                audioContentType = g_strdup(AUDIO_ALAW_CONTENT_TYPE);
            } else if (STRNCMP(mediaType, GSTREAMER_MEDIA_TYPE_MULAW, MAX_GSTREAMER_MEDIA_TYPE_LEN) == 0) {
                g_free(pGstKvsPlugin->audioCodecId);
                pGstKvsPlugin->audioCodecId = g_strdup(DEFAULT_AUDIO_CODEC_ID_PCM);
                audioContentType = g_strdup(AUDIO_MULAW_CONTENT_TYPE);
            } else if (STRNCMP(mediaType, GSTREAMER_MEDIA_TYPE_OPUS, MAX_GSTREAMER_MEDIA_TYPE_LEN) == 0) {
                DLOGI("Opus is only supported for WebRTC");
                g_free(pGstKvsPlugin->audioCodecId);
                pGstKvsPlugin->audioCodecId = g_strdup(DEFAULT_AUDIO_CODEC_ID_OPUS);
                audioContentType = g_strdup(AUDIO_OPUS_CONTENT_TYPE);
            } else {
                // no-op, should result in a caps negotiation error before getting here.
                DLOGE("Error, media type %s not accepted by plugin", mediaType);
                CHK(FALSE, STATUS_INVALID_ARG);
            }

            gst_caps_unref(caps);
        }
    }

    switch (pGstKvsPlugin->mediaType) {
        case GST_PLUGIN_MEDIA_TYPE_AUDIO_VIDEO:
            pGstKvsPlugin->gstParams.audioContentType = g_strdup(audioContentType);
            pGstKvsPlugin->gstParams.videoContentType = g_strdup(videoContentType);
            pGstKvsPlugin->gstParams.contentType = g_strjoin(",", videoContentType, audioContentType, NULL);
            break;
        case GST_PLUGIN_MEDIA_TYPE_AUDIO_ONLY:
            pGstKvsPlugin->gstParams.audioContentType = g_strdup(audioContentType);
            pGstKvsPlugin->gstParams.contentType = g_strdup(audioContentType);
            break;
        case GST_PLUGIN_MEDIA_TYPE_VIDEO_ONLY:
            pGstKvsPlugin->gstParams.contentType = g_strdup(videoContentType);
            pGstKvsPlugin->gstParams.videoContentType = g_strdup(videoContentType);
            break;
    }

CleanUp:

    g_free(videoContentType);
    g_free(audioContentType);

    return retStatus;
}

#define PACKAGE "kvswebrtcpluginpackage"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, kvsplugin, "GStreamer AWS KVS plugin", plugin_init, "1.0", "Proprietary", "GStreamer",
                  "http://gstreamer.net/")
