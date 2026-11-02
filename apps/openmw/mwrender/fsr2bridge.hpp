#ifndef OPENMW_MWRENDER_FSR2BRIDGE_H
#define OPENMW_MWRENDER_FSR2BRIDGE_H

// NerevarMoon: the only code that includes the FSR2 headers. Their glad/gl.h replaces the gl* functions with
// glad_* macros and refuses GL/gl.h, so nothing here may see an OSG or GL header.

#include <memory>
#include <string>

namespace MWRender::Fsr2Bridge
{
    int jitterPhaseCount(int sceneWidth, int outputWidth);

    // sub-pixel offset of the given phase, in scene pixels
    void jitterOffset(float& x, float& y, int index, int phaseCount);

    // GL texture names and formats, as plain integers
    struct Inputs
    {
        unsigned mColor = 0;
        unsigned mDepth = 0;
        unsigned mDepthFormat = 0;
        unsigned mVelocity = 0;
        unsigned mReactive = 0;
        unsigned mOutput = 0;
        int mSceneWidth = 0;
        int mSceneHeight = 0;
        float mJitterX = 0.f;
        float mJitterY = 0.f;
        float mFrameTimeMs = 0.f;
        float mSharpness = 0.f;
        float mNear = 0.f;
        float mFar = 0.f;
        float mFovYRadians = 0.f;
        bool mReset = false;
    };

    class Context
    {
    public:
        Context();
        // does not call GL: the context may already be gone, the GL objects are left to the driver
        ~Context();

        // empty on success; needs the GL context current
        std::string create(int sceneWidth, int sceneHeight, int outputWidth, int outputHeight, bool reverseZ);
        void destroy();
        // empty on success; needs the GL context current
        std::string dispatch(const Inputs& inputs);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
