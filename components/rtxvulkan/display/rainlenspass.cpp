#include "rainlenspass.hpp"

#include <array>
#include <cmath>

#include <components/rtx/renderer/framezone.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/rainlens.h>

#include "tonepass.hpp"

namespace Rtx
{
    namespace
    {
        /// The toned picture in, sampled, and the shown picture out.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::RAIN_LENS_BINDINGS> sBindings{
            computeBinding(Shaders::RAIN_LENS_BIND_SOURCE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            computeBinding(Shaders::RAIN_LENS_BIND_TARGET, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        };

        /// The curve writes it as a storage image, and this pass samples it.
        ImageDescription sourceDescription(const std::uint32_t width, const std::uint32_t height)
        {
            return ImageDescription{ .mWidth = width,
                .mHeight = height,
                .mFormat = TonePass::sTargetFormat,
                .mUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };
        }
    }

    RainLensPass::RainLensPass(const Device& device)
        : mDevice(device)
        , mPipeline(device, sBindings, {}, "rainlens.comp.spv", "rain lens")
        , mSampler(makeTargetSampler(device, "rain lens"))
    {
    }

    void RainLensPass::resize(const std::uint32_t width, const std::uint32_t height)
    {
        if (!mSource.isEmpty() && mSource.getWidth() == width && mSource.getHeight() == height)
            return;
        mSource = Image(MemoryUse::Frame, mDevice, sourceDescription(width, height), "rain lens source");
    }

    VkDeviceSize RainLensPass::bytesAt(const Device& device, const std::uint32_t width, const std::uint32_t height)
    {
        return Image::bytesFor(device, sourceDescription(width, height));
    }

    const Image* RainLensPass::sourceFor(const RainLens& rain, const VkExtent2D extent) const
    {
        if (rain.mStreak < sThreshold || mSource.isEmpty() || mSource.getWidth() != extent.width
            || mSource.getHeight() != extent.height)
            return nullptr;
        return &mSource;
    }

    void RainLensPass::record(const VkCommandBuffer commands, const RainLens& rain, const Image& source,
        const Image& target, const VkExtent2D extent, GpuTimer* timer) const
    {
        const GpuZone timed(timer, commands, FrameZone::Tone);

        source.transition(commands, Use::sComputeWrite, Use::sComputeSample);

        DescriptorWrites writes(mPipeline);
        writes.image(Shaders::RAIN_LENS_BIND_SOURCE, source.describeSampled(mSampler.get()));
        writes.image(Shaders::RAIN_LENS_BIND_TARGET, target.describeStorage());

        // 1.5 cycles a second repeat every two seconds, so the clock is reduced over two in double.
        const Shaders::RainLensConstants constants{
            .mStreak = rain.mStreak,
            .mSeconds = static_cast<float>(std::fmod(rain.mSeconds, 2.0)),
            .mMotion = rain.mMotion,
            .mSize = osg::Vec2ui(extent.width, extent.height),
        };

        dispatch(commands, mPipeline, writes, constants,
            Groups::covering(extent.width, extent.height, Shaders::RAIN_LENS_WORKGROUP));
    }
}
