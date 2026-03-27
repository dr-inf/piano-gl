#version 330 core
layout(location = 0) in vec3 aPos;

uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uEnvRot;
out vec3 vDir;

void main() {
    vDir = uEnvRot * aPos;
    mat4 viewNoTrans = mat4(mat3(uView));
    gl_Position = uProj * viewNoTrans * vec4(aPos, 1.0);
}
