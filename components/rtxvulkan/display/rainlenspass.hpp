#pragma once

#include <cstdint>

#include <osg/Vec2f>
#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/rainlens.h>

namespace Rtx
{
    class Device;
    class GpuTimer;

    /// What one run of the streaks is over.
    struct RainLens
    {
        /// How strong the streaks are, 0..1. Under `RainLensPass::sThreshold` nothing runs.
        float mStreak = 0.0f;

        /// Camera-space motion, x right and y forward, normalized or nought.
        osg::Vec2f mMotion{};

        /// The simulation's seconds, which the streaks scroll by.
        double mSeconds = 0.0;
    };

    /// NerevarMoon: rain streaks on the lens, after the curve (`rainlens.comp`, the 0.51 line's
    /// `nm_rain.omwfx`). The curve writes into this pass's own image on a frame with streaks, and
    /// this reads it and writes the target the window shows: the streaks read the picture a few
    /// pixels away, which one image cannot be both read and written for.
    class RainLensPass
    {
    public:
        /// Streaks weaker than this are not drawn, and cost nothing.
        static constexpr float sThreshold = 0.01f;

        explicit RainLensPass(const Device& device);

        /// Makes the image the curve writes into for a picture this size, if the last one was not.
        void resize(std::uint32_t width, std::uint32_t height);

        /// Lets the image go, until the next `resize`.
        void release() { mSource = Image(); }

        /// What `resize` to this extent takes of the device's memory.
        static VkDeviceSize bytesAt(const Device& device, std::uint32_t width, std::uint32_t height);

        /// The image the curve writes into this frame, or null where no streaks are drawn: streaks
        /// under the threshold, or a picture of another extent than `resize` was told.
        const Image* sourceFor(const RainLens& rain, VkExtent2D extent) const;

        /// Reads `source`, which the curve left as `Use::sComputeWrite`, and writes the streaked
        /// picture into `target` over `extent`, left as `Use::sComputeWrite`.
        void record(VkCommandBuffer commands, const RainLens& rain, const Image& source, const Image& target,
            VkExtent2D extent, GpuTimer* timer) const;

    private:
        const Device& mDevice;
        ComputePipeline<Shaders::RainLensConstants> mPipeline;

        /// Linear and clamped.
        Sampler mSampler;

        /// What the curve writes into on a frame with streaks, the target's format.
        Image mSource;
    };
}
