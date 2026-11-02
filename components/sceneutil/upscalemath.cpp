#include "upscalemath.hpp"

#include <osg/Vec4f>

#include <algorithm>
#include <charconv>
#include <cmath>

namespace SceneUtil
{
    float upscaleMipBias(float scale)
    {
        return std::log2(scale) - 1.f;
    }

    std::optional<float> parseMipBias(std::string_view value)
    {
        float result = 0.f;
        const char* end = value.data() + value.size();
        const auto [ptr, ec] = std::from_chars(value.data(), end, result);
        if (ec != std::errc() || ptr != end)
            return std::nullopt;
        return result;
    }

    osg::Vec2i upscaleSceneSize(osg::Vec2i outputSize, float scale)
    {
        return osg::Vec2i(std::max(1, static_cast<int>(std::lround(outputSize.x() * scale))),
            std::max(1, static_cast<int>(std::lround(outputSize.y() * scale))));
    }

    osg::Vec2f jitterToNdc(osg::Vec2f offsetPixels, osg::Vec2i sceneSize)
    {
        return osg::Vec2f(2.f * offsetPixels.x() / sceneSize.x(), 2.f * offsetPixels.y() / sceneSize.y());
    }

    osg::Matrixf clipToPrevClip(
        const osg::Matrixd& proj, const osg::Matrixd& view, const osg::Matrixd& prevView, const osg::Matrixd& prevProj)
    {
        return osg::Matrixf(osg::Matrixd::inverse(proj) * osg::Matrixd::inverse(view) * prevView * prevProj);
    }

    osg::Vec2f reprojectUv(osg::Vec2f uv, float depth, const osg::Matrixf& clipToPrevClip, bool reverseZ)
    {
        // reverse Z uses clip control zero-to-one, so NDC z is the depth value itself
        const float ndcZ = reverseZ ? depth : depth * 2.f - 1.f;
        const osg::Vec4f prev = osg::Vec4f(uv.x() * 2.f - 1.f, uv.y() * 2.f - 1.f, ndcZ, 1.f) * clipToPrevClip;
        const osg::Vec2f prevUv(prev.x() / prev.w() * 0.5f + 0.5f, prev.y() / prev.w() * 0.5f + 0.5f);
        return prevUv - uv;
    }

    float fovYFromProjection(const osg::Matrixd& proj)
    {
        return static_cast<float>(2.0 * std::atan(1.0 / proj(1, 1)));
    }

    bool isCameraCut(const osg::Vec3f& prevPos, const osg::Vec3f& pos, float threshold)
    {
        return (pos - prevPos).length2() > threshold * threshold;
    }
}
