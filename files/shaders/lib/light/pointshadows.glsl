#ifndef LIB_LIGHT_POINTSHADOWS
#define LIB_LIGHT_POINTSHADOWS

// NerevarMoon: indoor point light shadows (components/sceneutil/pointshadow.cpp). Each depth map is an atlas of 6 faces x 4 slots.

#if @pointShadows

uniform sampler2DShadow nmPointShadowStatic;
uniform sampler2DShadow nmPointShadowDynamic;
uniform int nmShadowLight[4];
uniform float nmShadowFade[4];
uniform int nmDynFaces[4];
uniform vec3 nmShadowLightPos[4];
uniform float nmShadowFar[4];
uniform mat3 nmViewToWorld;
uniform vec3 nmCameraPos;

// Face table shared with components/sceneutil/pointshadowmath.cpp: change both together.
void nmPointShadowFace(vec3 dir, out int face, out vec2 uv, out float axisDistance)
{
    vec3 a = abs(dir);
    vec3 right;
    vec3 up = vec3(0.0, 0.0, 1.0);
    if (a.x >= a.y && a.x >= a.z)
    {
        axisDistance = a.x;
        face = dir.x >= 0.0 ? 0 : 1;
        right = dir.x >= 0.0 ? vec3(0.0, -1.0, 0.0) : vec3(0.0, 1.0, 0.0);
    }
    else if (a.y >= a.z)
    {
        axisDistance = a.y;
        face = dir.y >= 0.0 ? 2 : 3;
        right = dir.y >= 0.0 ? vec3(1.0, 0.0, 0.0) : vec3(-1.0, 0.0, 0.0);
    }
    else
    {
        axisDistance = a.z;
        face = dir.z >= 0.0 ? 4 : 5;
        right = vec3(1.0, 0.0, 0.0);
        up = dir.z >= 0.0 ? vec3(0.0, -1.0, 0.0) : vec3(0.0, 1.0, 0.0);
    }
    uv = vec2(dot(dir, right), dot(dir, up)) / axisDistance * 0.5 + 0.5;
}

// Soft edges: 12 Poisson taps over a 2-texel radius (16 bilinear compares each, 48 texels wide at 512).
float nmSamplePointShadow(sampler2DShadow map, int slot, int face, vec2 uv, float ref)
{
    float texel = 1.0 / @pointShadowResolution;
    // keep every tap inside this face's tile
    uv = clamp(uv, vec2(2.5 * texel), vec2(1.0 - 2.5 * texel));
    vec2 base = (vec2(float(face), float(slot)) + uv) * vec2(1.0 / 6.0, 0.25);
    vec2 r = vec2(2.0 * texel / 6.0, 2.0 * texel * 0.25);
    float sum = shadow2D(map, vec3(base + vec2(-0.326, -0.406) * r, ref)).r;
    sum += shadow2D(map, vec3(base + vec2(-0.840, -0.074) * r, ref)).r;
    sum += shadow2D(map, vec3(base + vec2(-0.696, 0.457) * r, ref)).r;
    sum += shadow2D(map, vec3(base + vec2(-0.203, 0.621) * r, ref)).r;
    sum += shadow2D(map, vec3(base + vec2(0.962, -0.195) * r, ref)).r;
    sum += shadow2D(map, vec3(base + vec2(0.473, -0.480) * r, ref)).r;
    sum += shadow2D(map, vec3(base + vec2(0.519, 0.767) * r, ref)).r;
    sum += shadow2D(map, vec3(base + vec2(0.185, -0.893) * r, ref)).r;
    sum += shadow2D(map, vec3(base + vec2(0.507, 0.064) * r, ref)).r;
    sum += shadow2D(map, vec3(base + vec2(0.896, 0.412) * r, ref)).r;
    sum += shadow2D(map, vec3(base + vec2(-0.322, -0.933) * r, ref)).r;
    sum += shadow2D(map, vec3(base + vec2(-0.792, -0.598) * r, ref)).r;
    return sum / 12.0;
}

// Visibility (0..1) of the point light at LightBuffer index lightIndex; 1 for lights without a shadow slot.
float nmPointShadow(int lightIndex, vec3 viewPos, vec3 viewNormal)
{
    for (int slot = 0; slot < 4; ++slot)
    {
        if (nmShadowLight[slot] != lightIndex || nmShadowFade[slot] <= 0.0)
            continue;

        vec3 worldPos = nmViewToWorld * viewPos + nmCameraPos;
        float lightDistance = length(worldPos - nmShadowLightPos[slot]);
        // normal offset grows with the texel footprint, which grows with distance from the light
        worldPos += nmViewToWorld * viewNormal * (@pointShadowNormalOffset * (2.0 / @pointShadowResolution) * lightDistance);

        int face;
        vec2 uv;
        float axisDistance;
        nmPointShadowFace(worldPos - nmShadowLightPos[slot], face, uv, axisDistance);

        float farPlane = nmShadowFar[slot];
        float nearPlane = @pointShadowNear; // "point shadow near": casters around the light never cast
        // bias as a fraction of the distance (PointShadow::shadowCompareDepth), same world margin at any range
        float biased = axisDistance * (1.0 - @pointShadowBias);
        if (axisDistance >= farPlane || biased <= nearPlane)
            return 1.0;
        float ndc = (farPlane + nearPlane) / (farPlane - nearPlane)
            - 2.0 * farPlane * nearPlane / ((farPlane - nearPlane) * biased);
        float ref = ndc * 0.5 + 0.5;

        float visibility = nmSamplePointShadow(nmPointShadowStatic, slot, face, uv, ref);
        if (nmDynFaces[slot] != 0)
            visibility *= nmSamplePointShadow(nmPointShadowDynamic, slot, face, uv, ref);
        // "point shadow strength" (PointShadow::applyStrength): a shadowed receiver keeps 1 - strength of the light
        visibility = 1.0 - @pointShadowStrength * (1.0 - visibility);
        return mix(1.0, visibility, nmShadowFade[slot]);
    }
    return 1.0;
}

// Slot colour for "point shadow debug": red, green, blue, yellow; white for lights without a slot.
vec3 nmPointShadowDebugTint(int lightIndex)
{
    if (nmShadowLight[0] == lightIndex)
        return vec3(1.0, 0.2, 0.2);
    if (nmShadowLight[1] == lightIndex)
        return vec3(0.2, 1.0, 0.2);
    if (nmShadowLight[2] == lightIndex)
        return vec3(0.2, 0.2, 1.0);
    if (nmShadowLight[3] == lightIndex)
        return vec3(1.0, 1.0, 0.2);
    return vec3(1.0);
}

#else

float nmPointShadow(int lightIndex, vec3 viewPos, vec3 viewNormal)
{
    return 1.0;
}

vec3 nmPointShadowDebugTint(int lightIndex)
{
    return vec3(1.0);
}

#endif

#endif
