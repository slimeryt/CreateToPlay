#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

out vec3 FragPos;
out vec3 Normal;
out vec4 FragPosLightSpace;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
uniform mat4 uLightSpaceMatrix;

void main() {
    vec4 worldPos     = uModel * vec4(aPos, 1.0);
    FragPos           = worldPos.xyz;
    Normal            = normalize(uNormalMatrix * aNormal);
    FragPosLightSpace = uLightSpaceMatrix * worldPos;
    gl_Position       = uProjection * uView * worldPos;
}
