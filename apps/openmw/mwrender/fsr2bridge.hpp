#ifndef OPENMW_MWRENDER_FSR2BRIDGE_H
#define OPENMW_MWRENDER_FSR2BRIDGE_H

// NerevarMoon: the only code that includes the FSR2 headers. Their glad/gl.h replaces the gl* functions with
// glad_* macros and refuses GL/gl.h, so nothing here may see an OSG or GL header.

namespace MWRender::Fsr2Bridge
{
    int jitterPhaseCount(int sceneWidth, int outputWidth);

    // sub-pixel offset of the given phase, in scene pixels
    void jitterOffset(float& x, float& y, int index, int phaseCount);
}

#endif
