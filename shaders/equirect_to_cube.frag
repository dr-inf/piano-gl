#version 330 core
in vec3 vDir;
out vec4 FragColor;
uniform sampler2D uEquirect;
const float PI = 3.14159265359;
void main() {
    vec3 d = normalize(vDir);
    float u = atan(d.z, d.x) / (2.0 * PI) + 0.5;
    float v = 0.5 - asin(clamp(d.y, -1.0, 1.0)) / PI;
    vec3 color = texture(uEquirect, vec2(u, v)).rgb;
    FragColor = vec4(color, 1.0);
}
