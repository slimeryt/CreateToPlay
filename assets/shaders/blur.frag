#version 330 core
in  vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D uImage;
uniform bool      uHorizontal;

// 9-tap Gaussian kernel weights
const float kWeight[5] = float[](0.227027, 0.194595, 0.121622, 0.054054, 0.016216);

void main() {
    vec2 offset = 1.0 / vec2(textureSize(uImage, 0));
    vec3 result = texture(uImage, TexCoord).rgb * kWeight[0];

    if (uHorizontal) {
        for (int i = 1; i < 5; ++i) {
            result += texture(uImage, TexCoord + vec2(offset.x * i, 0.0)).rgb * kWeight[i];
            result += texture(uImage, TexCoord - vec2(offset.x * i, 0.0)).rgb * kWeight[i];
        }
    } else {
        for (int i = 1; i < 5; ++i) {
            result += texture(uImage, TexCoord + vec2(0.0, offset.y * i)).rgb * kWeight[i];
            result += texture(uImage, TexCoord - vec2(0.0, offset.y * i)).rgb * kWeight[i];
        }
    }

    FragColor = vec4(result, 1.0);
}
