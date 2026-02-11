/*
 * Poisson Blend OFX Plugin
 * 
 * A complete OpenFX plugin that performs Poisson blending (seamless cloning)
 * using OpenCV's cv::seamlessClone, while preserving full float precision
 * via a residual-based approach.
 * 
 * This plugin uses ONLY the raw OpenFX C API (no C++ support library).
 */

#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

// OpenFX C headers (raw API)
#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxProperty.h"
#include "ofxParam.h"

// OpenCV
#include <opencv2/opencv.hpp>
#include <opencv2/photo.hpp>

// Global pointers to OFX suites (fetched from host)
static OfxHost* gHost = nullptr;
static OfxImageEffectSuiteV1* gEffectSuite = nullptr;
static OfxPropertySuiteV1* gPropertySuite = nullptr;
static OfxParameterSuiteV1* gParameterSuite = nullptr;

// Plugin identifier
#define kPluginIdentifier "com.example.PoissonBlend"
#define kPluginVersionMajor 1
#define kPluginVersionMinor 0

// Clip names
#define kSourceClip "Source"
#define kForegroundClip "Foreground"
#define kMaskClip "Mask"
#define kOutputClip "Output"

// Parameter names
#define kParamBlendCenter "blendCenter"
#define kParamBlendMode "blendMode"
#define kParamMaskThreshold "maskThreshold"

// Blend mode choices
#define kBlendModeNormal 0
#define kBlendModeMixed 1
#define kBlendModeMonochrome 2

// Helper function to get property value
template<typename T>
static OfxStatus getProp(OfxPropertySetHandle propSet, const char* property, int index, T* value);

template<>
OfxStatus getProp<int>(OfxPropertySetHandle propSet, const char* property, int index, int* value) {
    return gPropertySuite->propGetInt(propSet, property, index, value);
}

template<>
OfxStatus getProp<double>(OfxPropertySetHandle propSet, const char* property, int index, double* value) {
    return gPropertySuite->propGetDouble(propSet, property, index, value);
}

template<>
OfxStatus getProp<char*>(OfxPropertySetHandle propSet, const char* property, int index, char** value) {
    return gPropertySuite->propGetString(propSet, property, index, value);
}

template<>
OfxStatus getProp<void*>(OfxPropertySetHandle propSet, const char* property, int index, void** value) {
    return gPropertySuite->propGetPointer(propSet, property, index, value);
}

// Helper function to set property value
template<typename T>
static OfxStatus setProp(OfxPropertySetHandle propSet, const char* property, int index, T value);

template<>
OfxStatus setProp<int>(OfxPropertySetHandle propSet, const char* property, int index, int value) {
    return gPropertySuite->propSetInt(propSet, property, index, value);
}

template<>
OfxStatus setProp<double>(OfxPropertySetHandle propSet, const char* property, int index, double value) {
    return gPropertySuite->propSetDouble(propSet, property, index, value);
}

template<>
OfxStatus setProp<const char*>(OfxPropertySetHandle propSet, const char* property, int index, const char* value) {
    return gPropertySuite->propSetString(propSet, property, index, value);
}

// Convert float RGBA image to 8-bit BGR cv::Mat
static cv::Mat floatRGBAToBGR8(float* data, int width, int height, int rowBytes) {
    cv::Mat result(height, width, CV_8UC3);
    
    for (int y = 0; y < height; y++) {
        float* srcRow = (float*)((char*)data + y * rowBytes);
        unsigned char* dstRow = result.ptr<unsigned char>(y);
        
        for (int x = 0; x < width; x++) {
            float r = srcRow[x * 4 + 0];
            float g = srcRow[x * 4 + 1];
            float b = srcRow[x * 4 + 2];
            
            // Clamp and convert to 8-bit
            dstRow[x * 3 + 0] = (unsigned char)(std::min(std::max(b * 255.0f, 0.0f), 255.0f));
            dstRow[x * 3 + 1] = (unsigned char)(std::min(std::max(g * 255.0f, 0.0f), 255.0f));
            dstRow[x * 3 + 2] = (unsigned char)(std::min(std::max(r * 255.0f, 0.0f), 255.0f));
        }
    }
    
    return result;
}

// Convert 8-bit BGR cv::Mat back to float
static cv::Mat bgr8ToFloat(const cv::Mat& src) {
    cv::Mat result(src.rows, src.cols, CV_32FC3);
    
    for (int y = 0; y < src.rows; y++) {
        const unsigned char* srcRow = src.ptr<unsigned char>(y);
        float* dstRow = result.ptr<float>(y);
        
        for (int x = 0; x < src.cols; x++) {
            dstRow[x * 3 + 0] = srcRow[x * 3 + 0] / 255.0f; // B
            dstRow[x * 3 + 1] = srcRow[x * 3 + 1] / 255.0f; // G
            dstRow[x * 3 + 2] = srcRow[x * 3 + 2] / 255.0f; // R
        }
    }
    
    return result;
}

// Convert float RGBA mask to 8-bit binary mask
static cv::Mat floatRGBAToMask8(float* data, int width, int height, int rowBytes, double threshold) {
    cv::Mat result(height, width, CV_8UC1);
    
    for (int y = 0; y < height; y++) {
        float* srcRow = (float*)((char*)data + y * rowBytes);
        unsigned char* dstRow = result.ptr<unsigned char>(y);
        
        for (int x = 0; x < width; x++) {
            // Use luminance from RGB
            float r = srcRow[x * 4 + 0];
            float g = srcRow[x * 4 + 1];
            float b = srcRow[x * 4 + 2];
            float luma = 0.299f * r + 0.587f * g + 0.114f * b;
            
            dstRow[x] = (luma >= threshold) ? 255 : 0;
        }
    }
    
    return result;
}

// Action: Load
static OfxStatus actionLoad() {
    // Fetch suites from host
    gEffectSuite = (OfxImageEffectSuiteV1*)gHost->fetchSuite(gHost->host, kOfxImageEffectSuite, 1);
    gPropertySuite = (OfxPropertySuiteV1*)gHost->fetchSuite(gHost->host, kOfxPropertySuite, 1);
    gParameterSuite = (OfxParameterSuiteV1*)gHost->fetchSuite(gHost->host, kOfxParameterSuite, 1);
    
    if (!gEffectSuite || !gPropertySuite || !gParameterSuite) {
        return kOfxStatErrMissingHostFeature;
    }
    
    return kOfxStatOK;
}

// Action: Unload
static OfxStatus actionUnload() {
    return kOfxStatOK;
}

// Action: Describe
static OfxStatus actionDescribe(OfxImageEffectHandle descriptor) {
    OfxPropertySetHandle props;
    gEffectSuite->getPropertySet(descriptor, &props);
    
    // Set plugin properties
    setProp(props, kOfxPropLabel, 0, "Poisson Blend");
    setProp(props, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
    setProp(props, kOfxImageEffectPropSupportedContexts, 1, kOfxImageEffectContextGeneral);
    
    // Supported pixel depths
    setProp(props, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthFloat);
    
    // Plugin capabilities
    setProp(props, kOfxImageEffectPluginPropSingleInstance, 0, (int)0);
    setProp(props, kOfxImageEffectPluginRenderThreadSafety, 0, kOfxImageEffectRenderFullySafe);
    setProp(props, kOfxImageEffectPropSupportsMultiResolution, 0, (int)1);
    setProp(props, kOfxImageEffectPropSupportsTiles, 0, (int)0);
    setProp(props, kOfxImageEffectPropTemporalClipAccess, 0, (int)0);
    
    return kOfxStatOK;
}

// Action: DescribeInContext
static OfxStatus actionDescribeInContext(OfxImageEffectHandle descriptor, OfxPropertySetHandle inArgs) {
    // Get context
    char* context = nullptr;
    getProp(inArgs, kOfxImageEffectPropContext, 0, &context);
    
    // Define clips
    OfxPropertySetHandle clipProps;
    
    // Output clip
    gEffectSuite->clipDefine(descriptor, kOutputClip, &clipProps);
    setProp(clipProps, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
    
    // Source clip (target/background)
    gEffectSuite->clipDefine(descriptor, kSourceClip, &clipProps);
    setProp(clipProps, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
    
    // Foreground clip
    gEffectSuite->clipDefine(descriptor, kForegroundClip, &clipProps);
    setProp(clipProps, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
    setProp(clipProps, kOfxImageClipPropOptional, 0, (int)0);
    
    // Mask clip
    gEffectSuite->clipDefine(descriptor, kMaskClip, &clipProps);
    setProp(clipProps, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
    setProp(clipProps, kOfxImageClipPropOptional, 0, (int)1);
    
    // Define parameters
    OfxParamSetHandle paramSet;
    gEffectSuite->getParamSet(descriptor, &paramSet);
    
    OfxPropertySetHandle paramProps;
    
    // Blend Center parameter
    gParameterSuite->paramDefine(paramSet, kOfxParamTypeDouble2D, kParamBlendCenter, &paramProps);
    setProp(paramProps, kOfxPropLabel, 0, "Blend Center");
    setProp(paramProps, kOfxParamPropDefault, 0, 0.5);
    setProp(paramProps, kOfxParamPropDefault, 1, 0.5);
    setProp(paramProps, kOfxParamPropDoubleType, 0, kOfxParamDoubleTypeXYAbsolute);
    
    // Blend Mode parameter
    gParameterSuite->paramDefine(paramSet, kOfxParamTypeChoice, kParamBlendMode, &paramProps);
    setProp(paramProps, kOfxPropLabel, 0, "Blend Mode");
    setProp(paramProps, kOfxParamPropChoiceOption, kBlendModeNormal, "Normal Clone");
    setProp(paramProps, kOfxParamPropChoiceOption, kBlendModeMixed, "Mixed Clone");
    setProp(paramProps, kOfxParamPropChoiceOption, kBlendModeMonochrome, "Monochrome Transfer");
    setProp(paramProps, kOfxParamPropDefault, 0, kBlendModeNormal);
    
    // Mask Threshold parameter
    gParameterSuite->paramDefine(paramSet, kOfxParamTypeDouble, kParamMaskThreshold, &paramProps);
    setProp(paramProps, kOfxPropLabel, 0, "Mask Threshold");
    setProp(paramProps, kOfxParamPropDefault, 0, 0.5);
    setProp(paramProps, kOfxParamPropMin, 0, 0.0);
    setProp(paramProps, kOfxParamPropMax, 0, 1.0);
    setProp(paramProps, kOfxParamPropDisplayMin, 0, 0.0);
    setProp(paramProps, kOfxParamPropDisplayMax, 0, 1.0);
    
    return kOfxStatOK;
}

// Action: CreateInstance
static OfxStatus actionCreateInstance(OfxImageEffectHandle instance) {
    return kOfxStatOK;
}

// Action: DestroyInstance
static OfxStatus actionDestroyInstance(OfxImageEffectHandle instance) {
    return kOfxStatOK;
}

// Action: Render
static OfxStatus actionRender(OfxImageEffectHandle instance, OfxPropertySetHandle inArgs) {
    // Get time
    double time;
    getProp(inArgs, kOfxPropTime, 0, &time);
    
    // Get render window
    OfxRectI renderWindow;
    getProp(inArgs, kOfxImageEffectPropRenderWindow, 0, &renderWindow.x1);
    getProp(inArgs, kOfxImageEffectPropRenderWindow, 1, &renderWindow.y1);
    getProp(inArgs, kOfxImageEffectPropRenderWindow, 2, &renderWindow.x2);
    getProp(inArgs, kOfxImageEffectPropRenderWindow, 3, &renderWindow.y2);
    
    // Get clips
    OfxImageClipHandle sourceClip, foregroundClip, maskClip, outputClip;
    gEffectSuite->clipGetHandle(instance, kSourceClip, &sourceClip, nullptr);
    gEffectSuite->clipGetHandle(instance, kForegroundClip, &foregroundClip, nullptr);
    gEffectSuite->clipGetHandle(instance, kMaskClip, &maskClip, nullptr);
    gEffectSuite->clipGetHandle(instance, kOutputClip, &outputClip, nullptr);
    
    // Get images
    OfxPropertySetHandle sourceImg = nullptr, foregroundImg = nullptr, maskImg = nullptr, outputImg = nullptr;
    gEffectSuite->clipGetImage(sourceClip, time, nullptr, &sourceImg);
    gEffectSuite->clipGetImage(foregroundClip, time, nullptr, &foregroundImg);
    gEffectSuite->clipGetImage(outputClip, time, nullptr, &outputImg);
    
    if (!sourceImg || !foregroundImg || !outputImg) {
        if (sourceImg) gEffectSuite->clipReleaseImage(sourceImg);
        if (foregroundImg) gEffectSuite->clipReleaseImage(foregroundImg);
        if (outputImg) gEffectSuite->clipReleaseImage(outputImg);
        return kOfxStatFailed;
    }
    
    // Try to get mask image (optional)
    gEffectSuite->clipGetImage(maskClip, time, nullptr, &maskImg);
    
    // Get image properties
    void* srcData, *fgData, *maskData, *outData;
    int srcWidth, srcHeight, srcRowBytes;
    int fgWidth, fgHeight, fgRowBytes;
    int maskWidth, maskHeight, maskRowBytes;
    int outWidth, outHeight, outRowBytes;
    
    getProp(sourceImg, kOfxImagePropData, 0, &srcData);
    getProp(sourceImg, kOfxImagePropBounds, 2, &srcWidth);
    getProp(sourceImg, kOfxImagePropBounds, 3, &srcHeight);
    getProp(sourceImg, kOfxImagePropRowBytes, 0, &srcRowBytes);
    
    getProp(foregroundImg, kOfxImagePropData, 0, &fgData);
    getProp(foregroundImg, kOfxImagePropBounds, 2, &fgWidth);
    getProp(foregroundImg, kOfxImagePropBounds, 3, &fgHeight);
    getProp(foregroundImg, kOfxImagePropRowBytes, 0, &fgRowBytes);
    
    getProp(outputImg, kOfxImagePropData, 0, &outData);
    getProp(outputImg, kOfxImagePropBounds, 2, &outWidth);
    getProp(outputImg, kOfxImagePropBounds, 3, &outHeight);
    getProp(outputImg, kOfxImagePropRowBytes, 0, &outRowBytes);
    
    // Get parameters
    OfxParamSetHandle paramSet;
    gEffectSuite->getParamSet(instance, &paramSet);
    
    OfxParamHandle blendCenterParam, blendModeParam, maskThresholdParam;
    gParameterSuite->paramGetHandle(paramSet, kParamBlendCenter, &blendCenterParam, nullptr);
    gParameterSuite->paramGetHandle(paramSet, kParamBlendMode, &blendModeParam, nullptr);
    gParameterSuite->paramGetHandle(paramSet, kParamMaskThreshold, &maskThresholdParam, nullptr);
    
    double blendCenterX, blendCenterY;
    int blendMode;
    double maskThreshold;
    
    gParameterSuite->paramGetValueAtTime(blendCenterParam, time, &blendCenterX, &blendCenterY);
    gParameterSuite->paramGetValueAtTime(blendModeParam, time, &blendMode);
    gParameterSuite->paramGetValueAtTime(maskThresholdParam, time, &maskThreshold);
    
    // Convert images to 8-bit for OpenCV
    cv::Mat target8 = floatRGBAToBGR8((float*)srcData, srcWidth, srcHeight, srcRowBytes);
    cv::Mat fg8 = floatRGBAToBGR8((float*)fgData, fgWidth, fgHeight, fgRowBytes);
    
    // Build mask
    cv::Mat mask8;
    if (maskImg) {
        getProp(maskImg, kOfxImagePropData, 0, &maskData);
        getProp(maskImg, kOfxImagePropBounds, 2, &maskWidth);
        getProp(maskImg, kOfxImagePropBounds, 3, &maskHeight);
        getProp(maskImg, kOfxImagePropRowBytes, 0, &maskRowBytes);
        
        mask8 = floatRGBAToMask8((float*)maskData, maskWidth, maskHeight, maskRowBytes, maskThreshold);
    } else {
        // Create white mask covering full foreground
        mask8 = cv::Mat(fgHeight, fgWidth, CV_8UC1, cv::Scalar(255));
    }
    
    // Ensure mask matches foreground size
    if (mask8.cols != fgWidth || mask8.rows != fgHeight) {
        cv::resize(mask8, mask8, cv::Size(fgWidth, fgHeight));
    }
    
    // Convert blend center from normalized coordinates to pixel coordinates
    cv::Point center((int)(blendCenterX * srcWidth), (int)(blendCenterY * srcHeight));
    
    // Map blend mode to OpenCV constant
    int cvBlendMode;
    switch (blendMode) {
        case kBlendModeMixed:
            cvBlendMode = cv::MIXED_CLONE;
            break;
        case kBlendModeMonochrome:
            cvBlendMode = cv::MONOCHROME_TRANSFER;
            break;
        case kBlendModeNormal:
        default:
            cvBlendMode = cv::NORMAL_CLONE;
            break;
    }
    
    // Perform seamless clone
    cv::Mat result8;
    try {
        cv::seamlessClone(fg8, target8, mask8, center, result8, cvBlendMode);
    } catch (const cv::Exception& e) {
        fprintf(stderr, "OpenCV seamlessClone error: %s\n", e.what());
        if (sourceImg) gEffectSuite->clipReleaseImage(sourceImg);
        if (foregroundImg) gEffectSuite->clipReleaseImage(foregroundImg);
        if (maskImg) gEffectSuite->clipReleaseImage(maskImg);
        if (outputImg) gEffectSuite->clipReleaseImage(outputImg);
        return kOfxStatFailed;
    }
    
    // Convert back to float and compute residual
    cv::Mat resultFloat = bgr8ToFloat(result8);
    cv::Mat target8Float = bgr8ToFloat(target8);
    cv::Mat residual = resultFloat - target8Float;
    
    // Apply residual to original float target
    for (int y = 0; y < outHeight; y++) {
        float* srcRow = (float*)((char*)srcData + y * srcRowBytes);
        float* outRow = (float*)((char*)outData + y * outRowBytes);
        
        for (int x = 0; x < outWidth; x++) {
            if (y < residual.rows && x < residual.cols) {
                float* res = residual.ptr<float>(y, x);
                // Apply residual in BGR order, write as RGBA
                outRow[x * 4 + 0] = srcRow[x * 4 + 0] + res[2]; // R
                outRow[x * 4 + 1] = srcRow[x * 4 + 1] + res[1]; // G
                outRow[x * 4 + 2] = srcRow[x * 4 + 2] + res[0]; // B
                outRow[x * 4 + 3] = srcRow[x * 4 + 3];          // A (preserve)
            } else {
                // Outside residual bounds, copy original
                outRow[x * 4 + 0] = srcRow[x * 4 + 0];
                outRow[x * 4 + 1] = srcRow[x * 4 + 1];
                outRow[x * 4 + 2] = srcRow[x * 4 + 2];
                outRow[x * 4 + 3] = srcRow[x * 4 + 3];
            }
        }
    }
    
    // Release images
    gEffectSuite->clipReleaseImage(sourceImg);
    gEffectSuite->clipReleaseImage(foregroundImg);
    if (maskImg) gEffectSuite->clipReleaseImage(maskImg);
    gEffectSuite->clipReleaseImage(outputImg);
    
    return kOfxStatOK;
}

// Action: GetRegionOfDefinition
static OfxStatus actionGetRegionOfDefinition(OfxImageEffectHandle instance, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
    // Get time
    double time;
    getProp(inArgs, kOfxPropTime, 0, &time);
    
    // Get source clip
    OfxImageClipHandle sourceClip;
    gEffectSuite->clipGetHandle(instance, kSourceClip, &sourceClip, nullptr);
    
    // Get source RoD
    OfxRectD rod;
    gEffectSuite->clipGetRegionOfDefinition(sourceClip, time, &rod);
    
    // Set output RoD
    setProp(outArgs, kOfxImageEffectPropRegionOfDefinition, 0, rod.x1);
    setProp(outArgs, kOfxImageEffectPropRegionOfDefinition, 1, rod.y1);
    setProp(outArgs, kOfxImageEffectPropRegionOfDefinition, 2, rod.x2);
    setProp(outArgs, kOfxImageEffectPropRegionOfDefinition, 3, rod.y2);
    
    return kOfxStatOK;
}

// Action: GetRegionsOfInterest
static OfxStatus actionGetRegionsOfInterest(OfxImageEffectHandle instance, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
    // Get time
    double time;
    getProp(inArgs, kOfxPropTime, 0, &time);
    
    // Get render RoI
    OfxRectD roi;
    getProp(inArgs, kOfxImageEffectPropRegionOfInterest, 0, &roi.x1);
    getProp(inArgs, kOfxImageEffectPropRegionOfInterest, 1, &roi.y1);
    getProp(inArgs, kOfxImageEffectPropRegionOfInterest, 2, &roi.x2);
    getProp(inArgs, kOfxImageEffectPropRegionOfInterest, 3, &roi.y2);
    
    // Request full images from all inputs
    OfxImageClipHandle sourceClip, foregroundClip, maskClip;
    gEffectSuite->clipGetHandle(instance, kSourceClip, &sourceClip, nullptr);
    gEffectSuite->clipGetHandle(instance, kForegroundClip, &foregroundClip, nullptr);
    gEffectSuite->clipGetHandle(instance, kMaskClip, &maskClip, nullptr);
    
    OfxRectD sourceRod, foregroundRod, maskRod;
    gEffectSuite->clipGetRegionOfDefinition(sourceClip, time, &sourceRod);
    gEffectSuite->clipGetRegionOfDefinition(foregroundClip, time, &foregroundRod);
    
    // Set RoIs for each clip
    OfxPropertySetHandle sourceRoiProps, foregroundRoiProps, maskRoiProps;
    gPropertySuite->propSetDouble(outArgs, "OfxImageClipPropRoI_Source", 0, sourceRod.x1);
    gPropertySuite->propSetDouble(outArgs, "OfxImageClipPropRoI_Source", 1, sourceRod.y1);
    gPropertySuite->propSetDouble(outArgs, "OfxImageClipPropRoI_Source", 2, sourceRod.x2);
    gPropertySuite->propSetDouble(outArgs, "OfxImageClipPropRoI_Source", 3, sourceRod.y2);
    
    gPropertySuite->propSetDouble(outArgs, "OfxImageClipPropRoI_Foreground", 0, foregroundRod.x1);
    gPropertySuite->propSetDouble(outArgs, "OfxImageClipPropRoI_Foreground", 1, foregroundRod.y1);
    gPropertySuite->propSetDouble(outArgs, "OfxImageClipPropRoI_Foreground", 2, foregroundRod.x2);
    gPropertySuite->propSetDouble(outArgs, "OfxImageClipPropRoI_Foreground", 3, foregroundRod.y2);
    
    // Try to get mask RoD if clip exists
    OfxStatus maskStatus = gEffectSuite->clipGetRegionOfDefinition(maskClip, time, &maskRod);
    if (maskStatus == kOfxStatOK) {
        gPropertySuite->propSetDouble(outArgs, "OfxImageClipPropRoI_Mask", 0, maskRod.x1);
        gPropertySuite->propSetDouble(outArgs, "OfxImageClipPropRoI_Mask", 1, maskRod.y1);
        gPropertySuite->propSetDouble(outArgs, "OfxImageClipPropRoI_Mask", 2, maskRod.x2);
        gPropertySuite->propSetDouble(outArgs, "OfxImageClipPropRoI_Mask", 3, maskRod.y2);
    }
    
    return kOfxStatOK;
}

// Action: IsIdentity
static OfxStatus actionIsIdentity(OfxImageEffectHandle instance, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
    // Never identity - always render
    return kOfxStatReplyDefault;
}

// Main entry point
static OfxStatus pluginMain(const char* action,
                            const void* handle,
                            OfxPropertySetHandle inArgs,
                            OfxPropertySetHandle outArgs) {
    try {
        if (strcmp(action, kOfxActionLoad) == 0) {
            return actionLoad();
        }
        else if (strcmp(action, kOfxActionUnload) == 0) {
            return actionUnload();
        }
        else if (strcmp(action, kOfxActionDescribe) == 0) {
            return actionDescribe((OfxImageEffectHandle)handle);
        }
        else if (strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
            return actionDescribeInContext((OfxImageEffectHandle)handle, inArgs);
        }
        else if (strcmp(action, kOfxActionCreateInstance) == 0) {
            return actionCreateInstance((OfxImageEffectHandle)handle);
        }
        else if (strcmp(action, kOfxActionDestroyInstance) == 0) {
            return actionDestroyInstance((OfxImageEffectHandle)handle);
        }
        else if (strcmp(action, kOfxImageEffectActionRender) == 0) {
            return actionRender((OfxImageEffectHandle)handle, inArgs);
        }
        else if (strcmp(action, kOfxImageEffectActionGetRegionOfDefinition) == 0) {
            return actionGetRegionOfDefinition((OfxImageEffectHandle)handle, inArgs, outArgs);
        }
        else if (strcmp(action, kOfxImageEffectActionGetRegionsOfInterest) == 0) {
            return actionGetRegionsOfInterest((OfxImageEffectHandle)handle, inArgs, outArgs);
        }
        else if (strcmp(action, kOfxImageEffectActionIsIdentity) == 0) {
            return actionIsIdentity((OfxImageEffectHandle)handle, inArgs, outArgs);
        }
        
        return kOfxStatReplyDefault;
    }
    catch (...) {
        return kOfxStatFailed;
    }
}

// setHost callback
static void setHost(OfxHost* host) {
    gHost = host;
}

// Plugin struct
static OfxPlugin gPlugin = {
    kOfxImageEffectPluginApi,
    1,
    kPluginIdentifier,
    kPluginVersionMajor,
    kPluginVersionMinor,
    setHost,
    pluginMain
};

// OFX entry points
extern "C" {

OfxExport int OfxGetNumberOfPlugins(void) {
    return 1;
}

OfxExport OfxPlugin* OfxGetPlugin(int nth) {
    if (nth == 0) {
        return &gPlugin;
    }
    return nullptr;
}

} // extern "C"
