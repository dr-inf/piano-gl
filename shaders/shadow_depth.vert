#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 4) in float aCut;      // custom attribute
layout(location = 5) in vec2 aCutWidth;  // instanced: left/right cuts
layout(location = 6) in float aCenter;   // instanced: center offset
layout(location = 7) in float aRotation; // instanced: rotation angle (radians)

uniform mat4 uModel;
uniform mat4 uLightVP;

void main() {
    // Apply the same per-instance offset logic as the main PBR vertex shader.
    vec3 pos = aPos;
    bool isLeft = (aCut < 0.0);
    float cut = isLeft ? 0.01 * aCutWidth.x : 0.01 * aCutWidth.y;
    pos.z -= aCut * -cut;
    pos.z -= 0.01 * aCenter;
    vec2 rotPivot = vec2(-2.5, 0.0);
    float c = cos(aRotation);
    float s = sin(aRotation);
    mat2 rot = mat2(c, -s, s, c);
    pos.xy -= rotPivot;
    pos.xy = rot * pos.xy;
    pos.xy += rotPivot;

    gl_Position = uLightVP * uModel * vec4(pos, 1.0);
}
