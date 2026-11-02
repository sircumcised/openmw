#ifndef OPENMW_COMPONENTS_SCENEUTIL_UPSCALEMATH_H
#define OPENMW_COMPONENTS_SCENEUTIL_UPSCALEMATH_H

// NerevarMoon: pure math of the FSR2 upscaler (mwrender/upscaler.cpp), kept free of the scene graph
// so it can be tested on its own.

#include <osg/Matrixd>
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

    // current clip space to previous clip space, inverse(proj) * inverse(view) * prevView * prevProj, built in double
    // so the large view translations of exterior cells cancel before the result goes to float
    osg::Matrixf clipToPrevClip(
        const osg::Matrixd& proj, const osg::Matrixd& view, const osg::Matrixd& prevView, const osg::Matrixd& prevProj);

    // motion vector of a pixel caused by the camera alone: previous UV minus current UV.
    // The GLSL velocity pass in mwrender/upscaler.cpp computes the same thing.
    osg::Vec2f reprojectUv(osg::Vec2f uv, float depth, const osg::Matrixf& clipToPrevClip, bool reverseZ);

    // vertical field of view in radians of a perspective projection; a jitter translation does not change it
    float fovYFromProjection(const osg::Matrixd& proj);

    bool isCameraCut(const osg::Vec3f& prevPos, const osg::Vec3f& pos, float threshold = 512.f);
}

#endif
