#ifndef OPENMW_MWRENDER_UPSCALER_H
#define OPENMW_MWRENDER_UPSCALER_H

// NerevarMoon: FSR2 temporal upscaler. Runs on the draw thread inside PingPongCanvas::drawImplementation.

#include <osg/GL>
#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec2i>

#include <string>

#include "fsr2bridge.hpp"

namespace osg
{
    class State;
}

namespace MWRender
{
    class Fsr2Upscaler
    {
    public:
        struct Inputs
        {
            GLuint mColor = 0;
            GLuint mDepth = 0;
            GLenum mDepthFormat = 0;
            // 0 when nothing was drawn in first person yet
            GLuint mFirstPersonDepth = 0;
            GLuint mOutput = 0;
            osg::Vec2i mSceneSize;
            osg::Vec2f mJitterPixels;
            osg::Matrixf mClipToPrevClip;
            float mFrameTimeMs = 0.f;
            float mSharpness = 0.f;
            float mNear = 0.f;
            float mFar = 0.f;
            float mFovY = 0.f;
            bool mReverseZ = false;
            bool mReset = false;
        };

        // empty when GL 4.6, GL_ARB_gl_spirv and GL_KHR_shader_subgroup are there
        static std::string checkSupport(unsigned contextId);

        // empty on success
        std::string create(osg::State& state, osg::Vec2i sceneSize, osg::Vec2i outputSize, bool reverseZ);
        void destroy(osg::State& state);
        // empty on success
        std::string dispatch(osg::State& state, const Inputs& inputs);

    private:
        Fsr2Bridge::Context mContext;
        GLuint mVelocityProgram = 0;
        GLuint mReactiveProgram = 0;
        GLuint mVelocity = 0;
        GLuint mReactive = 0;
    };
}

#endif
