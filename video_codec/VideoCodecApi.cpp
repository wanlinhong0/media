/*
 * 版权所有 (c) 华为技术有限公司 2021-2021
 * 功能说明: 视频编解码器对外接口实现
 */

#define LOG_TAG "VideoCodecApi"
#include "VideoCodecApi.h"
#include "VideoEncoderOpenH264.h"
#include "VideoEncoderNetint.h"
#include "VideoEncoderVpe.h"
#include "MediaLog.h"

void RegisterMediaLogCallback(const MediaLogCallbackFunc logCallback)
{
    SetMediaLogCallback(logCallback);
}

EncoderRetCode CreateVideoEncoder(uint32_t encType, VideoEncoder** encoder)
{
    switch (encType) {
        case ENCODER_TYPE_OPENH264:
            *encoder = new (std::nothrow) VideoEncoderOpenH264();
            break;
        case ENCODER_TYPE_NETINTH264:
            *encoder = new (std::nothrow) VideoEncoderNetint(NI_CODEC_TYPE_H264);
            break;
        case ENCODER_TYPE_NETINTH265:
            *encoder = new (std::nothrow) VideoEncoderNetint(NI_CODEC_TYPE_H265);
            break;
        case ENCODER_TYPE_VPEH264:
            *encoder = new (std::nothrow) VideoEncoderVpe(CODEC_ID_H264);
            break;
        case ENCODER_TYPE_VPEH265:
            *encoder = new (std::nothrow) VideoEncoderVpe(CODEC_ID_HEVC);
            break;
        default:
            ERR("create video encoder failed: unknown encoder type %u", encType);
            return VIDEO_ENCODER_CREATE_FAIL;
    }
    if (*encoder == nullptr) {
        ERR("create video encoder failed: encoder type %u", encType);
        return VIDEO_ENCODER_CREATE_FAIL;
    }
    return VIDEO_ENCODER_SUCCESS;
}


EncoderRetCode DestroyVideoEncoder(uint32_t encType, VideoEncoder* encoder)
{
    if (encoder == nullptr) {
        WARN("input encoder is null");
        return VIDEO_ENCODER_SUCCESS;
    }
    VideoEncoderOpenH264 *openH264 = nullptr;
    VideoEncoderNetint *netint = nullptr;
    VideoEncoderVpe *vpe = nullptr;
    switch (encType) {
        case ENCODER_TYPE_OPENH264:
            openH264 = static_cast<VideoEncoderOpenH264 *>(encoder);
            delete openH264;
            openH264 = nullptr;
            break;
        case ENCODER_TYPE_NETINTH264:
        case ENCODER_TYPE_NETINTH265:
            netint = static_cast<VideoEncoderNetint *>(encoder);
            delete netint;
            netint = nullptr;
            break;
        case ENCODER_TYPE_VPEH264:
        case ENCODER_TYPE_VPEH265:
            vpe = static_cast<VideoEncoderVpe *>(encoder);
            delete vpe;
            vpe = nullptr;
            break;
        default:
            ERR("destroy video encoder failed: unknown encoder type %u", encType);
            return VIDEO_ENCODER_DESTROY_FAIL;
    }
    return VIDEO_ENCODER_SUCCESS;
}
