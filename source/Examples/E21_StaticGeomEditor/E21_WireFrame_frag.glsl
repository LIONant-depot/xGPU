#version 450

layout(location = 0) in vec4 gWorldPosition;
layout(location = 1) in vec4 gDist;

float uWireSmoothness = 3.0;
vec3  uWireColor      = vec3(1,1,1);

layout(location = 0) out vec4 fragColor;

void main() 
{
    float minDist   = min(gDist.x, min(gDist.y, gDist.z)) * gDist.w;
    float t         = exp2(uWireSmoothness * -1.0 * minDist * minDist);
    float a         = (minDist > 0.9) ? 0 : t;

    fragColor.rgb   = uWireColor;
    fragColor.a     = a*0.7;
}