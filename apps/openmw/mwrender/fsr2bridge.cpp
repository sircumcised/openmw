#include "fsr2bridge.hpp"

#include <ffx_fsr2.h>

namespace MWRender::Fsr2Bridge
{
    int jitterPhaseCount(int sceneWidth, int outputWidth)
    {
        return ffxFsr2GetJitterPhaseCount(sceneWidth, outputWidth);
    }

    void jitterOffset(float& x, float& y, int index, int phaseCount)
    {
        ffxFsr2GetJitterOffset(&x, &y, index, phaseCount);
    }
}
