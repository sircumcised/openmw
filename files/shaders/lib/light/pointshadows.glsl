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

float nmSamplePointShadow(sampler2DShadow map, int slot, int face, vec2 uv, float ref)
{
    float texel = 1.0 / @pointShadowResolution;
    // keep the 4 linear-filtered taps inside this face's tile
    uv = clamp(uv, vec2(1.5 * texel), vec2(1.0 - 1.5 * texel));
    vec2 tile = vec2(float(face), float(slot));
    vec2 scale = vec2(1.0 / 6.0, 0.25);
    float o = 0.5 * texel;
    float sum = shadow2D(map, vec3((tile + uv + vec2(-o, -o)) * scale, ref)).r;
    sum += shadow2D(map, vec3((tile + uv + vec2(o, -o)) * scale, ref)).r;
    sum += shadow2D(map, vec3((tile + uv + vec2(-o, o)) * scale, ref)).r;
    sum += shadow2D(map, vec3((tile + uv + vec2(o, o)) * scale, ref)).r;
    return sum * 0.25;
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
        if (axisDistance >= farPlane)
            return 1.0;
        float ndc = (farPlane + 1.0) / (farPlane - 1.0) - 2.0 * farPlane / ((farPlane - 1.0) * max(axisDistance, 1.0));
        float ref = ndc * 0.5 + 0.5 - @pointShadowBias;

        float visibility = nmSamplePointShadow(nmPointShadowStatic, slot, face, uv, ref);
        if (nmDynFaces[slot] != 0)
            visibility *= nmSamplePointShadow(nmPointShadowDynamic, slot, face, uv, ref);
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
