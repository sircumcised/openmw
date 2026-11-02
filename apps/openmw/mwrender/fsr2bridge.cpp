#include "fsr2bridge.hpp"

#include <ffx_fsr2.h>
#include <gl/ffx_fsr2_gl.h>

#include <Windows.h>

#include <cstdint>
#include <vector>

namespace MWRender::Fsr2Bridge
{
    namespace
    {
        // wglGetProcAddress only knows functions newer than OpenGL 1.1; it is looked up at run time because
        // wingdi.h is not visible next to glad/gl.h
        ffx_glProc loadGl(const char* name)
        {
            using WglGetProcAddress = PROC(WINAPI*)(LPCSTR);
            static const HMODULE opengl = GetModuleHandleA("opengl32.dll");
            static const auto wglGetProcAddressPtr
                = reinterpret_cast<WglGetProcAddress>(GetProcAddress(opengl, "wglGetProcAddress"));
            PROC proc = wglGetProcAddressPtr ? wglGetProcAddressPtr(name) : nullptr;
            const auto value = reinterpret_cast<std::intptr_t>(proc);
            if (value == 0 || value == 1 || value == 2 || value == 3 || value == -1)
                proc = GetProcAddress(opengl, name);
            return reinterpret_cast<ffx_glProc>(proc);
        }

        std::string errorText(const char* call, FfxErrorCode code)
        {
            return std::string(call) + " failed with FfxErrorCode " + std::to_string(static_cast<int>(code));
        }

        // Morrowind units are about 1/70 of a meter
        constexpr float sMetersPerUnit = 1.f / 70.f;
    }

    int jitterPhaseCount(int sceneWidth, int outputWidth)
    {
        return ffxFsr2GetJitterPhaseCount(sceneWidth, outputWidth);
    }

    void jitterOffset(float& x, float& y, int index, int phaseCount)
    {
        ffxFsr2GetJitterOffset(&x, &y, index, phaseCount);
    }

    struct Context::Impl
    {
        std::vector<unsigned char> mScratch;
        FfxFsr2Context mContext;
        bool mCreated = false;
        bool mReverseZ = false;
        int mOutputWidth = 0;
        int mOutputHeight = 0;
    };

    Context::Context()
        : mImpl(std::make_unique<Impl>())
    {
    }

    Context::~Context() = default;

    std::string Context::create(int sceneWidth, int sceneHeight, int outputWidth, int outputHeight, bool reverseZ)
    {
        destroy();

        mImpl->mScratch.assign(ffxFsr2GetScratchMemorySizeGL(), 0);

        FfxFsr2ContextDescription desc{};
        if (const FfxErrorCode code
            = ffxFsr2GetInterfaceGL(&desc.callbacks, mImpl->mScratch.data(), mImpl->mScratch.size(), loadGl);
            code != FFX_OK)
            return errorText("ffxFsr2GetInterfaceGL", code);

        // the GL backend has no device or command list, dispatch refuses null ones without this flag
        desc.flags = FFX_FSR2_ALLOW_NULL_DEVICE_AND_COMMAND_LIST | (reverseZ ? FFX_FSR2_ENABLE_DEPTH_INVERTED : 0);
        desc.maxRenderSize = { static_cast<uint32_t>(sceneWidth), static_cast<uint32_t>(sceneHeight) };
        desc.displaySize = { static_cast<uint32_t>(outputWidth), static_cast<uint32_t>(outputHeight) };
        desc.device = nullptr;

        if (const FfxErrorCode code = ffxFsr2ContextCreate(&mImpl->mContext, &desc); code != FFX_OK)
            return errorText("ffxFsr2ContextCreate", code);

        mImpl->mCreated = true;
        mImpl->mReverseZ = reverseZ;
        mImpl->mOutputWidth = outputWidth;
        mImpl->mOutputHeight = outputHeight;
        return {};
    }

    void Context::destroy()
    {
        if (!mImpl->mCreated)
            return;
        ffxFsr2ContextDestroy(&mImpl->mContext);
        mImpl->mCreated = false;
    }

    std::string Context::dispatch(const Inputs& in)
    {
        if (!mImpl->mCreated)
            return "context not created";

        const uint32_t w = static_cast<uint32_t>(in.mSceneWidth);
        const uint32_t h = static_cast<uint32_t>(in.mSceneHeight);

        FfxFsr2DispatchDescription desc{};
        desc.commandList = nullptr;
        desc.color = ffxGetTextureResourceGL(in.mColor, w, h, GL_RGBA8, L"nm_scene_color");
        desc.depth = ffxGetTextureResourceGL(in.mDepth, w, h, in.mDepthFormat, L"nm_scene_depth");
        desc.motionVectors = ffxGetTextureResourceGL(in.mVelocity, w, h, GL_RG16F, L"nm_velocity");
        desc.reactive = ffxGetTextureResourceGL(in.mReactive, w, h, GL_R8, L"nm_reactive");
        desc.output = ffxGetTextureResourceGL(in.mOutput, static_cast<uint32_t>(mImpl->mOutputWidth),
            static_cast<uint32_t>(mImpl->mOutputHeight), GL_RGBA16F, L"nm_upscaled");
        desc.jitterOffset = { in.mJitterX, in.mJitterY };
        // velocity is stored in UV units, FSR2 wants scene pixels
        desc.motionVectorScale = { static_cast<float>(w), static_cast<float>(h) };
        desc.renderSize = { w, h };
        desc.enableSharpening = in.mSharpness > 0.f;
        desc.sharpness = in.mSharpness;
        desc.frameTimeDelta = in.mFrameTimeMs;
        desc.preExposure = 1.f;
        desc.reset = in.mReset;
        // with inverted depth FSR2 wants the planes swapped
        desc.cameraNear = mImpl->mReverseZ ? in.mFar : in.mNear;
        desc.cameraFar = mImpl->mReverseZ ? in.mNear : in.mFar;
        desc.cameraFovAngleVertical = in.mFovYRadians;
        desc.viewSpaceToMetersFactor = sMetersPerUnit;
        // reverse Z uses clip control zero-to-one, the default GL depth range is [-1, 1]
        desc.deviceDepthNegativeOneToOne = !mImpl->mReverseZ;

        if (const FfxErrorCode code = ffxFsr2ContextDispatch(&mImpl->mContext, &desc); code != FFX_OK)
            return errorText("ffxFsr2ContextDispatch", code);
        return {};
    }
}
