#ifndef OPENMW_COMPONENTS_SCENEUTIL_UPSCALEMATH_H
#define OPENMW_COMPONENTS_SCENEUTIL_UPSCALEMATH_H

// NerevarMoon: pure math of the FSR2 upscaler (mwrender/upscaler.cpp), kept free of the scene graph
// so it can be tested on its own.

#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec2i>
#include <osg/Vec3f>

#include <optional>
#include <string_view>

namespace SceneUtil
{
    // texture LOD bias that keeps the detail of the output resolution: log2(scale) - 1
    float upscaleMipBias(float scale);

    // the "upscale mip bias" setting: a number, or nothing for "auto" and unreadable text
    std::optional<float> parseMipBias(std::string_view value);

    osg::Vec2i upscaleSceneSize(osg::Vec2i outputSize, float scale);

    osg::Vec2f jitterToNdc(osg::Vec2f offsetPixels, osg::Vec2i sceneSize);

    // motion vector of a pixel caused by the camera alone: previous UV minus current UV.
    // The GLSL velocity pass in mwrender/upscaler.cpp computes the same thing.
    osg::Vec2f reprojectUv(osg::Vec2f uv, float depth, const osg::Matrixf& invViewProj,
        const osg::Matrixf& prevViewProj, bool reverseZ);

    bool isCameraCut(const osg::Vec3f& prevPos, const osg::Vec3f& pos, float threshold = 512.f);
}

#endif
