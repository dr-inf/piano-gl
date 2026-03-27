#version 330 core
in vec3 vDir;
out vec4 FragColor;

uniform samplerCube uCubemap;

void main() {
    vec3 color = texture(uCubemap, vDir).rgb;
    FragColor = vec4(color, 1.0);
}
