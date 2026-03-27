#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent; // xyz: tangent, w: handedness
layout(location = 4) in float aCut;    // custom attribute
layout(location = 5) in vec2 aCutWidth; // instanced: left/right cuts
layout(location = 6) in float aCenter;  // instanced: center offset
layout(location = 7) in float aRotation; // instanced: rotation angle (radians)

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMat;
uniform mat4 uLightVP;

out VS_OUT {
    vec3 WorldPos;
    vec3 Normal;
    vec2 UV;
    mat3 TBN;
    vec4 LightPos;
} vsOut;

void main() {
    // Offset based on cut direction and center (only active when instanced attributes are bound).
    vec3 pos = aPos;
    bool isLeft = (aCut < 0.0);
    float cut = isLeft ? 0.01*aCutWidth.x : 0.01*aCutWidth.y;
    pos.z -= aCut * -cut;
    pos.z -= 0.01*aCenter;
    vec2 rotPivot = vec2(-2.5, 0.0);
    float c = cos(aRotation);
    float s = sin(aRotation);
    mat2 rot = mat2(c, -s, s, c);
    pos.xy -= rotPivot;
    pos.xy = rot * pos.xy;
    pos.xy += rotPivot;

    vec3 rotatedNormal = vec3(rot * aNormal.xy, aNormal.z);
    vec3 rotatedTangent = vec3(rot * aTangent.xy, aTangent.z);
    vec3 N = normalize(uNormalMat * rotatedNormal);
    vec3 T = normalize(uNormalMat * rotatedTangent);
    vec3 B = aTangent.w * normalize(cross(N, T));
    vsOut.TBN = mat3(T, B, N);
    vsOut.WorldPos = vec3(uModel * vec4(pos, 1.0));
    vsOut.Normal = N;
    vsOut.UV = aUV;
    vsOut.LightPos = uLightVP * vec4(vsOut.WorldPos, 1.0);
    gl_Position = uProj * uView * vec4(vsOut.WorldPos, 1.0);
}
