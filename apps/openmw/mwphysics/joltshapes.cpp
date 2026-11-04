#include "joltshapes.hpp"

#include <Jolt/Geometry/Triangle.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <BulletCollision/CollisionShapes/btConcaveShape.h>
#include <BulletCollision/CollisionShapes/btConvexHullShape.h>
#include <BulletCollision/CollisionShapes/btTriangleCallback.h>

#include <components/debug/debuglog.hpp>

namespace MWPhysics
{
    namespace
    {
        JPH::Float3 toMetres(const btVector3& v)
        {
            return JPH::Float3(static_cast<float>(v.x()) / sJoltUnitsPerMetre,
                static_cast<float>(v.y()) / sJoltUnitsPerMetre, static_cast<float>(v.z()) / sJoltUnitsPerMetre);
        }

        struct TriangleCollector : btTriangleCallback
        {
            JPH::TriangleList mTriangles;

            void processTriangle(btVector3* triangle, int /*partId*/, int /*triangleIndex*/) override
            {
                mTriangles.emplace_back(toMetres(triangle[0]), toMetres(triangle[1]), toMetres(triangle[2]));
            }
        };

        JPH::ShapeRefC create(JPH::ShapeSettings& settings, JoltShapeStats& stats)
        {
            settings.SetEmbedded(); // on the stack: Jolt must not free it when a reference drops
            JPH::ShapeSettings::ShapeResult result = settings.Create();
            if (result.HasError())
            {
                ++stats.mFailed;
                Log(Debug::Verbose) << "Jolt: shape conversion failed: " << result.GetError().c_str();
                return nullptr;
            }
            ++stats.mConverted;
            return result.Get();
        }
    }

    JPH::ShapeRefC convertBulletShape(const btCollisionShape& shape, JoltShapeStats& stats)
    {
        switch (shape.getShapeType())
        {
            case COMPOUND_SHAPE_PROXYTYPE:
            {
                const auto& compound = static_cast<const btCompoundShape&>(shape);
                JPH::StaticCompoundShapeSettings settings;
                int parts = 0;
                for (int i = 0; i < compound.getNumChildShapes(); ++i)
                {
                    JPH::ShapeRefC child = convertBulletShape(*compound.getChildShape(i), stats);
                    if (child == nullptr)
                        continue;
                    const btTransform& t = compound.getChildTransform(i);
                    const btQuaternion q = t.getRotation();
                    const JPH::Float3 o = toMetres(t.getOrigin());
                    settings.AddShape(JPH::Vec3(o.x, o.y, o.z),
                        JPH::Quat(static_cast<float>(q.x()), static_cast<float>(q.y()), static_cast<float>(q.z()),
                            static_cast<float>(q.w()))
                            .Normalized(),
                        child);
                    ++parts;
                }
                if (parts == 0)
                    return nullptr;
                return create(settings, stats);
            }
            case BOX_SHAPE_PROXYTYPE:
            {
                const JPH::Float3 h = toMetres(static_cast<const btBoxShape&>(shape).getHalfExtentsWithMargin());
                const JPH::Vec3 half = JPH::Vec3::sMax(JPH::Vec3(h.x, h.y, h.z), JPH::Vec3::sReplicate(0.01f));
                ++stats.mConverted;
                return new JPH::BoxShape(half, std::min(JPH::cDefaultConvexRadius, half.ReduceMin() * 0.5f));
            }
            case CONVEX_HULL_SHAPE_PROXYTYPE:
            {
                const auto& hull = static_cast<const btConvexHullShape&>(shape);
                JPH::Array<JPH::Vec3> points;
                points.reserve(hull.getNumPoints());
                for (int i = 0; i < hull.getNumPoints(); ++i)
                {
                    const JPH::Float3 p = toMetres(hull.getUnscaledPoints()[i]);
                    points.emplace_back(p.x, p.y, p.z);
                }
                JPH::ConvexHullShapeSettings settings(points);
                return create(settings, stats);
            }
            default:
                break;
        }

        if (shape.isConcave())
        {
            TriangleCollector collector;
            const btVector3 big(BT_LARGE_FLOAT, BT_LARGE_FLOAT, BT_LARGE_FLOAT);
            static_cast<const btConcaveShape&>(shape).processAllTriangles(&collector, -big, big);
            if (collector.mTriangles.empty())
                return nullptr;
            JPH::MeshShapeSettings settings(collector.mTriangles);
            return create(settings, stats);
        }

        ++stats.mUnsupported;
        return nullptr;
    }

    JPH::ShapeRefC makeTerrainShape(const float* heights, int x, int y, int size, int verts)
    {
        const float step = static_cast<float>(size) / static_cast<float>(verts - 1);
        const float originX = static_cast<float>(x) * size;
        const float originY = static_cast<float>(y) * size;

        JPH::VertexList vertices;
        vertices.reserve(static_cast<size_t>(verts) * verts);
        for (int j = 0; j < verts; ++j)
            for (int i = 0; i < verts; ++i)
                vertices.emplace_back((originX + i * step) / sJoltUnitsPerMetre,
                    (originY + j * step) / sJoltUnitsPerMetre, heights[j * verts + i] / sJoltUnitsPerMetre);

        JPH::IndexedTriangleList triangles;
        triangles.reserve(static_cast<size_t>(verts - 1) * (verts - 1) * 2);
        const auto index = [verts](int i, int j) { return static_cast<JPH::uint32>(j * verts + i); };
        for (int j = 0; j < verts - 1; ++j)
            for (int i = 0; i < verts - 1; ++i)
            {
                const JPH::uint32 a = index(i, j), b = index(i + 1, j), c = index(i + 1, j + 1), d = index(i, j + 1);
                // btHeightfieldTerrainShape with diamond subdivision: the diagonal alternates like a checkerboard
                if (((i + j) & 1) == 0)
                {
                    triangles.emplace_back(a, b, c, 0);
                    triangles.emplace_back(a, c, d, 0);
                }
                else
                {
                    triangles.emplace_back(a, b, d, 0);
                    triangles.emplace_back(b, c, d, 0);
                }
            }

        JoltShapeStats stats;
        JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
        return create(settings, stats);
    }
}
