/*
 * 版权所有 (c) 华为技术有限公司 2021-2021
 * 功能说明: 适配OpenH264软件视频编码器，包括编码器初始化、启动、编码、停止、销毁等
 */

#define LOG_TAG "VideoEncoderOpenH264"
#include "VideoEncoderOpenH264.h"
#include <string>
#include <dlfcn.h>
#include <cerrno>
#include <cstring>
#include <atomic>
#include "MediaLog.h"

namespace {
    constexpr uint32_t WH_MIN = 16;
    constexpr uint32_t WH_MAX = 4096;
    constexpr uint32_t FRAMERATE_MIN = 30;
    constexpr uint32_t FRAMERATE_MAX = 60;
    constexpr uint32_t BITRATE_MIN = 1000000;
    constexpr uint32_t BITRATE_MAX = 10000000;
    constexpr uint32_t GOPSIZE_MIN = 30;
    constexpr uint32_t GOPSIZE_MAX = 3000;
    constexpr uint32_t COMPRESS_RATIO = 2;
    constexpr uint32_t PRIMARY_COLOURS = 3;


    const std::string WELS_CREATE_SVC_ENCODER = "WelsCreateSVCEncoder";
    const std::string WELS_DESTROY_SVC_ENCODER = "WelsDestroySVCEncoder";
    /**
     * @功能描述: 创建编码器实例
     * @参数 [out] encoder: 编码器实例
     * @返回值: 0为成功；其他为失败
     */
    using WelsCreateSVCEncoderFuncPtr = int (*)(ISVCEncoder** encoder);

    /**
     * @功能描述: 销毁编码器实例
     * @参数 [in] encoder: 编码器实例
     */
    using WelsDestroySVCEncoderFuncPtr = void (*)(ISVCEncoder* encoder);
    WelsCreateSVCEncoderFuncPtr g_welsCreateSVCEncoder = nullptr;
    WelsDestroySVCEncoderFuncPtr g_welsDestroySVCEncoder = nullptr;
    const std::string SHARED_LIB_NAME = "libopenh264.so";
    std::atomic<bool> g_openH264Loaded = { false };
    void *g_libHandle = nullptr;
}

VideoEncoderOpenH264::VideoEncoderOpenH264()
{
    INFO("VideoEncoderOpenH264 constructor");
}

VideoEncoderOpenH264::~VideoEncoderOpenH264()
{
    Release();
    INFO("VideoEncoderOpenH264 destructor");
}

EncoderRetCode VideoEncoderOpenH264::InitEncoder(const EncodeParams &encParams)
{
    if (!VerifyEncodeParams(encParams)) {
        ERR("init encoder failed: encoder params is not supported");
        return VIDEO_ENCODER_INIT_FAIL;
    }
    m_encParams = encParams;
    if (!LoadOpenH264SharedLib()) {
        ERR("init encoder failed: load openh264 shared lib failed");
        return VIDEO_ENCODER_INIT_FAIL;
    }
    int rc = (*g_welsCreateSVCEncoder)(&m_encoder);
    if (rc != 0) {
        ERR("init encoder failed: create encoder failed, rc = %d", rc);
        return VIDEO_ENCODER_INIT_FAIL;
    }
    m_frameSize = m_encParams.width * m_encParams.height * PRIMARY_COLOURS / COMPRESS_RATIO;
    (void) memset(&m_paramExt, 0, sizeof(SEncParamExt));
    (void) memset(&m_srcPic, 0, sizeof(SSourcePicture));
    (void) memset(&m_frameBSInfo, 0, sizeof(SFrameBSInfo));
    if (!InitParams()) {
        ERR("init encoder failed: init params failed");
        return VIDEO_ENCODER_INIT_FAIL;
    }
    INFO("init encoder success");
    return VIDEO_ENCODER_SUCCESS;
}

bool VideoEncoderOpenH264::VerifyEncodeParams(const EncodeParams &encParams)
{
    if (encParams.width > WH_MAX || encParams.height > WH_MAX ||
        encParams.width < WH_MIN || encParams.height < WH_MIN) {
        ERR("resolution [%ux%u] is not supported", encParams.width, encParams.height);
        return false;
    }
    if (encParams.frameRate != FRAMERATE_MIN && encParams.frameRate != FRAMERATE_MAX) {
        ERR("framerate [%u] is not supported", encParams.frameRate);
        return false;
    }
    if (encParams.bitrate < BITRATE_MIN || encParams.bitrate > BITRATE_MAX) {
        ERR("bitrate [%u] is not supported", encParams.bitrate);
        return false;
    }
    if (encParams.gopSize < GOPSIZE_MIN || encParams.gopSize > GOPSIZE_MAX) {
        ERR("gopsize [%u] is not supported", encParams.gopSize);
        return false;
    }
    if (encParams.profile != ENCODE_PROFILE_BASELINE &&
        encParams.profile != ENCODE_PROFILE_MAIN &&
        encParams.profile != ENCODE_PROFILE_HIGH) {
        ERR("profile [%u] is not supported", encParams.profile);
        return false;
    }
    INFO("width:%u, height:%u, framerate:%u, bitrate:%u, gopsize:%u, profile:%u", encParams.width, encParams.height,
        encParams.frameRate, encParams.bitrate, encParams.gopSize, encParams.profile);
    return true;
}

bool VideoEncoderOpenH264::LoadOpenH264SharedLib()
{
    if (g_openH264Loaded) {
        return true;
    }
    INFO("load %s", SHARED_LIB_NAME.c_str());
    g_libHandle = dlopen(SHARED_LIB_NAME.c_str(), RTLD_LAZY);
    if (g_libHandle == nullptr) {
        const char *errStr = dlerror();
        ERR("load %s error:%s", SHARED_LIB_NAME.c_str(), (errStr != nullptr) ? errStr : "unknown");
        return false;
    }

    g_welsCreateSVCEncoder = reinterpret_cast<WelsCreateSVCEncoderFuncPtr>(
        dlsym(g_libHandle, WELS_CREATE_SVC_ENCODER.c_str()));
    if (g_welsCreateSVCEncoder == nullptr) {
        ERR("failed to load WelsCreateSVCEncoder");
        return false;
    }

    g_welsDestroySVCEncoder = reinterpret_cast<WelsDestroySVCEncoderFuncPtr>(
        dlsym(g_libHandle, WELS_DESTROY_SVC_ENCODER.c_str()));
    if (g_welsDestroySVCEncoder == nullptr) {
        ERR("failed to load WelsDestroySVCEncoder");
        g_welsCreateSVCEncoder = nullptr;
        return false;
    }
    g_openH264Loaded = true;
    return true;
}

bool VideoEncoderOpenH264::InitParams()
{
    int rc = m_encoder->GetDefaultParams(&m_paramExt);
    if (rc != 0) {
        ERR("encoder get default params failed, rc = %d", rc);
        return false;
    }
    m_paramExt.iPicWidth = m_encParams.width;
    m_paramExt.iPicHeight = m_encParams.height;
    m_yLength = m_encParams.width * m_encParams.height;
    InitParamExt();
    m_paramExt.iTargetBitrate = m_encParams.bitrate;
    m_paramExt.iMaxBitrate = m_encParams.bitrate;
    m_paramExt.fMaxFrameRate = m_encParams.frameRate;
    m_paramExt.uiIntraPeriod = m_encParams.gopSize;
    m_paramExt.sSpatialLayers[0].iVideoWidth = m_encParams.width;
    m_paramExt.sSpatialLayers[0].iVideoHeight = m_encParams.height;
    m_paramExt.sSpatialLayers[0].fFrameRate = m_encParams.frameRate;
    m_paramExt.sSpatialLayers[0].iSpatialBitrate = m_paramExt.iTargetBitrate;
    m_paramExt.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
    if (m_encParams.profile == ENCODE_PROFILE_HIGH) {
        m_paramExt.sSpatialLayers[0].uiProfileIdc = EProfileIdc::PRO_HIGH;
    } else if (m_encParams.profile == ENCODE_PROFILE_MAIN) {
        m_paramExt.sSpatialLayers[0].uiProfileIdc = EProfileIdc::PRO_MAIN;
    } else {
        m_paramExt.sSpatialLayers[0].uiProfileIdc = EProfileIdc::PRO_BASELINE;
    }
    m_paramExt.sSpatialLayers[0].uiLevelIdc = ELevelIdc::LEVEL_3_2;
    auto videoFormat = EVideoFormatType::videoFormatI420;
    rc = m_encoder->InitializeExt(&m_paramExt);
    if (rc != 0) {
        ERR("encoder initialize ext failed, rc = %d", rc);
        return false;
    }
    rc = m_encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &videoFormat);
    if (rc != 0) {
        ERR("encoder set option dataformat failed, rc = %d", rc);
        return false;
    }
    return true;
}

void VideoEncoderOpenH264::InitParamExt()
{
    constexpr uint32_t ltrMarkPeriod = 30;
    m_paramExt.iUsageType = EUsageType::CAMERA_VIDEO_REAL_TIME;
    m_paramExt.iRCMode = RC_MODES::RC_BITRATE_MODE;
    m_paramExt.iPaddingFlag = 0;
    m_paramExt.iTemporalLayerNum = 1;
    m_paramExt.iSpatialLayerNum = 1;
    m_paramExt.eSpsPpsIdStrategy = EParameterSetStrategy::CONSTANT_ID;
    m_paramExt.bPrefixNalAddingCtrl = 0;
    m_paramExt.bSimulcastAVC = 0;
    m_paramExt.bEnableDenoise = 0;
    m_paramExt.bEnableBackgroundDetection = 1;
    m_paramExt.bEnableSceneChangeDetect = 1;
    m_paramExt.bEnableAdaptiveQuant = 0;
    m_paramExt.bEnableFrameSkip = 0;
    m_paramExt.bEnableLongTermReference = 0;
    m_paramExt.iLtrMarkPeriod = ltrMarkPeriod;
    m_paramExt.bIsLosslessLink = 0;
    m_paramExt.iComplexityMode = ECOMPLEXITY_MODE::HIGH_COMPLEXITY;
    m_paramExt.iNumRefFrame = 1;
    m_paramExt.iEntropyCodingModeFlag = 1;
    m_paramExt.uiMaxNalSize = 0;
    m_paramExt.iLTRRefNum = 0;
    m_paramExt.iMultipleThreadIdc = 1;
    m_paramExt.iLoopFilterDisableIdc = 0;
}

EncoderRetCode VideoEncoderOpenH264::StartEncoder()
{
    INFO("start encoder success");
    return VIDEO_ENCODER_SUCCESS;
}

EncoderRetCode VideoEncoderOpenH264::EncodeOneFrame(const uint8_t *inputData, uint32_t inputSize,
    uint8_t **outputData, uint32_t *outputSize)
{
    if (inputSize < static_cast<size_t>(m_frameSize)) {
        ERR("input size error: input size(%u) < frame size(%u)", inputSize, m_frameSize);
        return VIDEO_ENCODER_ENCODE_FAIL;
    }
    if (m_resetFlag) {
        if (ResetEncoder() != VIDEO_ENCODER_SUCCESS) {
            ERR("reset encoder failed while encoding");
            return VIDEO_ENCODER_ENCODE_FAIL;
        }
        m_resetFlag = false;
    }
    InitSrcPic(inputData);
    int rc = m_encoder->EncodeFrame(&m_srcPic, &m_frameBSInfo);
    if (rc != 0) {
        ERR("encoder encode frame failed, rc = %d", rc);
        return VIDEO_ENCODER_ENCODE_FAIL;
    }
    *outputData = m_frameBSInfo.sLayerInfo->pBsBuf;
    *outputSize = static_cast<uint32_t>(m_frameBSInfo.iFrameSizeInBytes);
    return VIDEO_ENCODER_SUCCESS;
}

void VideoEncoderOpenH264::InitSrcPic(const uint8_t *inputData)
{
    m_srcPic.iPicWidth = m_paramExt.iPicWidth;
    m_srcPic.iPicHeight = m_paramExt.iPicHeight;
    m_srcPic.iColorFormat = EVideoFormatType::videoFormatI420;
    m_srcPic.iStride[0] = m_srcPic.iPicWidth;
    m_srcPic.iStride[1] = m_srcPic.iPicWidth / COMPRESS_RATIO;
    m_srcPic.iStride[COMPRESS_RATIO] = m_srcPic.iPicWidth / COMPRESS_RATIO;
    m_srcPic.pData[0] = const_cast<uint8_t *>(inputData);
    m_srcPic.pData[1] = m_srcPic.pData[0] + m_yLength;
    m_srcPic.pData[COMPRESS_RATIO] = m_srcPic.pData[1] + (m_yLength >> COMPRESS_RATIO);
}

EncoderRetCode VideoEncoderOpenH264::StopEncoder()
{
    INFO("stop encoder success");
    return VIDEO_ENCODER_SUCCESS;
}

void VideoEncoderOpenH264::DestroyEncoder()
{
    Release();
    INFO("destroy encoder success");
}

void VideoEncoderOpenH264::Release()
{
    if (m_encoder != nullptr) {
        (void) m_encoder->Uninitialize();
        (*g_welsDestroySVCEncoder)(m_encoder);
        m_encoder = nullptr;
    }
}

EncoderRetCode VideoEncoderOpenH264::ResetEncoder()
{
    INFO("resetting encoder");
    DestroyEncoder();
    EncoderRetCode ret = InitEncoder(m_encParams);
    if (ret != VIDEO_ENCODER_SUCCESS) {
        ERR("init encoder failed %#x while resetting", ret);
        return VIDEO_ENCODER_RESET_FAIL;
    }
    ret = StartEncoder();
    if (ret != VIDEO_ENCODER_SUCCESS) {
        ERR("start encoder failed %#x while resetting", ret);
        return VIDEO_ENCODER_RESET_FAIL;
    }
    INFO("reset encoder success");
    return VIDEO_ENCODER_SUCCESS;
}

EncoderRetCode VideoEncoderOpenH264::ForceKeyFrame()
{
    int ret = m_encoder->ForceIntraFrame(true);
    if (ret != 0) {
        ERR("encoder force intra frame failed: %d", ret);
        return VIDEO_ENCODER_FORCE_KEY_FRAME_FAIL;
    }
    INFO("force key frame success");
    return VIDEO_ENCODER_SUCCESS;
}

EncoderRetCode VideoEncoderOpenH264::SetEncodeParams(const EncodeParams &encParams)
{
    if (encParams == m_encParams) {
        WARN("encode params are not changed");
        return VIDEO_ENCODER_SUCCESS;
    }
    if (!VerifyEncodeParams(encParams)) {
        ERR("encoder params is not supported");
        return VIDEO_ENCODER_SET_ENCODE_PARAMS_FAIL;
    }
    m_encParams = encParams;
    m_resetFlag = true;
    return VIDEO_ENCODER_SUCCESS;
}
