#include "upscaler.hpp"

#include <SDL_opengl_glext.h>

#include <osg/GLExtensions>
#include <osg/State>

#include <cmath>
#include <cstdlib>

namespace MWRender
{
    namespace
    {
        // camera motion of each pixel, previous UV minus current UV; same math as SceneUtil::reprojectUv
        constexpr const char* sVelocitySource = R"GLSL(#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 0) uniform sampler2D depthTex;
layout(rg16f, binding = 0) uniform writeonly image2D velocityImg;
uniform mat4 clipToPrevClip;
uniform ivec2 size;
uniform bool reverseZ;
void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, size)))
        return;
    vec2 uv = (vec2(p) + 0.5) / vec2(size);
    float d = texelFetch(depthTex, p, 0).r;
    float z = reverseZ ? d : d * 2.0 - 1.0;
    vec4 prev = clipToPrevClip * vec4(uv * 2.0 - 1.0, z, 1.0);
    vec2 prevUv = prev.xy / prev.w * 0.5 + 0.5;
    imageStore(velocityImg, p, vec4(prevUv - uv, 0.0, 0.0));
}
)GLSL";

        // 1 where arms and weapon were drawn in first person: their motion is not the camera's
        constexpr const char* sReactiveSource = R"GLSL(#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 0) uniform sampler2D firstPersonDepthTex;
layout(r8, binding = 0) uniform writeonly image2D reactiveImg;
uniform ivec2 size;
uniform bool reverseZ;
uniform bool hasFirstPerson;
void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, size)))
        return;
    bool firstPerson = false;
    if (hasFirstPerson)
    {
        float d = texelFetch(firstPersonDepthTex, p, 0).r;
        firstPerson = reverseZ ? d > 0.0 : d < 1.0;
    }
    imageStore(reactiveImg, p, vec4(firstPerson ? 1.0 : 0.0));
}
)GLSL";

        GLuint compileCompute(osg::GLExtensions* ext, const char* source, std::string& error)
        {
            const GLuint shader = ext->glCreateShader(GL_COMPUTE_SHADER);
            ext->glShaderSource(shader, 1, &source, nullptr);
            ext->glCompileShader(shader);
            GLint ok = GL_FALSE;
            ext->glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[1024] = {};
                ext->glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                error = std::string("compute shader: ") + log;
                ext->glDeleteShader(shader);
                return 0;
            }
            const GLuint program = ext->glCreateProgram();
            ext->glAttachShader(program, shader);
            ext->glLinkProgram(program);
            ext->glDeleteShader(shader);
            ext->glGetProgramiv(program, GL_LINK_STATUS, &ok);
            if (!ok)
            {
                char log[1024] = {};
                ext->glGetProgramInfoLog(program, sizeof(log), nullptr, log);
                error = std::string("compute program: ") + log;
                ext->glDeleteProgram(program);
                return 0;
            }
            return program;
        }

        GLuint createTexture(osg::GLExtensions* ext, GLenum format, osg::Vec2i size)
        {
            GLuint id = 0;
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);
            ext->glTexStorage2D(GL_TEXTURE_2D, 1, format, size.x(), size.y());
            glBindTexture(GL_TEXTURE_2D, 0);
            return id;
        }

        void dispatchCompute(osg::GLExtensions* ext, osg::Vec2i size)
        {
            ext->glDispatchCompute((size.x() + 7) / 8, (size.y() + 7) / 8, 1);
        }
    }

    std::string Fsr2Upscaler::checkSupport(unsigned contextId)
    {
        if (std::getenv("OPENMW_NM_FSR2_FAIL"))
            return "forced by OPENMW_NM_FSR2_FAIL";
        if (osg::getGLVersionNumber() < 4.6f)
            return "OpenGL 4.6 required";
        for (const char* extension : { "GL_ARB_gl_spirv", "GL_KHR_shader_subgroup" })
            if (!osg::isGLExtensionSupported(contextId, extension))
                return std::string(extension) + " missing";
        return {};
    }

    std::string Fsr2Upscaler::create(osg::State& state, osg::Vec2i sceneSize, osg::Vec2i outputSize, bool reverseZ)
    {
        destroy(state);

        osg::GLExtensions* ext = state.get<osg::GLExtensions>();
        std::string error;
        mVelocityProgram = compileCompute(ext, sVelocitySource, error);
        if (!mVelocityProgram)
            return error;
        mReactiveProgram = compileCompute(ext, sReactiveSource, error);
        if (!mReactiveProgram)
            return error;

        mVelocity = createTexture(ext, GL_RG16F, sceneSize);
        mReactive = createTexture(ext, GL_R8, sceneSize);

        return mContext.create(sceneSize.x(), sceneSize.y(), outputSize.x(), outputSize.y(), reverseZ);
    }

    void Fsr2Upscaler::destroy(osg::State& state)
    {
        osg::GLExtensions* ext = state.get<osg::GLExtensions>();
        mContext.destroy();
        for (GLuint* program : { &mVelocityProgram, &mReactiveProgram })
        {
            if (*program)
                ext->glDeleteProgram(*program);
            *program = 0;
        }
        for (GLuint* texture : { &mVelocity, &mReactive })
        {
            if (*texture)
                glDeleteTextures(1, texture);
            *texture = 0;
        }
    }

    std::string Fsr2Upscaler::dispatch(osg::State& state, const Inputs& in)
    {
        osg::GLExtensions* ext = state.get<osg::GLExtensions>();

        ext->glActiveTexture(GL_TEXTURE0);
        ext->glBindSampler(0, 0);

        ext->glUseProgram(mVelocityProgram);
        glBindTexture(GL_TEXTURE_2D, in.mDepth);
        ext->glBindImageTexture(0, mVelocity, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
        ext->glUniformMatrix4fv(
            ext->glGetUniformLocation(mVelocityProgram, "clipToPrevClip"), 1, GL_FALSE, in.mClipToPrevClip.ptr());
        ext->glUniform2i(ext->glGetUniformLocation(mVelocityProgram, "size"), in.mSceneSize.x(), in.mSceneSize.y());
        ext->glUniform1i(ext->glGetUniformLocation(mVelocityProgram, "reverseZ"), in.mReverseZ);
        dispatchCompute(ext, in.mSceneSize);

        ext->glUseProgram(mReactiveProgram);
        glBindTexture(GL_TEXTURE_2D, in.mFirstPersonDepth);
        ext->glBindImageTexture(0, mReactive, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R8);
        ext->glUniform2i(ext->glGetUniformLocation(mReactiveProgram, "size"), in.mSceneSize.x(), in.mSceneSize.y());
        ext->glUniform1i(ext->glGetUniformLocation(mReactiveProgram, "reverseZ"), in.mReverseZ);
        ext->glUniform1i(ext->glGetUniformLocation(mReactiveProgram, "hasFirstPerson"), in.mFirstPersonDepth != 0);
        dispatchCompute(ext, in.mSceneSize);

        ext->glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        glBindTexture(GL_TEXTURE_2D, 0);

        Fsr2Bridge::Inputs bridge;
        bridge.mColor = in.mColor;
        bridge.mDepth = in.mDepth;
        bridge.mDepthFormat = in.mDepthFormat;
        bridge.mVelocity = mVelocity;
        bridge.mReactive = mReactive;
        bridge.mOutput = in.mOutput;
        bridge.mSceneWidth = in.mSceneSize.x();
        bridge.mSceneHeight = in.mSceneSize.y();
        bridge.mJitterX = in.mJitterPixels.x();
        bridge.mJitterY = in.mJitterPixels.y();
        bridge.mFrameTimeMs = in.mFrameTimeMs;
        bridge.mSharpness = in.mSharpness;
        bridge.mNear = in.mNear;
        bridge.mFar = in.mFar;
        bridge.mFovYRadians = in.mFovY;
        bridge.mReset = in.mReset;
        const std::string error = mContext.dispatch(bridge);
        ext->glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);

        // FSR2 leaves its programs, samplers, images and textures bound and does not tell OSG
        ext->glUseProgram(0);
        GLint textureUnits = 0;
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &textureUnits);
        for (GLuint unit = 0; unit < static_cast<GLuint>(textureUnits); ++unit)
            ext->glBindSampler(unit, 0);
        GLint imageUnits = 0;
        glGetIntegerv(GL_MAX_IMAGE_UNITS, &imageUnits);
        for (GLuint unit = 0; unit < static_cast<GLuint>(imageUnits); ++unit)
        {
            ext->glBindImageTexture(unit, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R8);
        }
        ext->glActiveTexture(GL_TEXTURE0 + state.getActiveTextureUnit());
        state.setLastAppliedProgramObject(nullptr);
        state.dirtyAllAttributes();
        state.dirtyAllModes();

        return error;
    }
}
