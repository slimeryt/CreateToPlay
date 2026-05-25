#version 330 core
in  vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D uHDRScene;
uniform sampler2D uBloom;
uniform float     uExposure;
uniform float     uBloomStrength;

// ACES fitted tone map (Narkowicz 2015)
vec3 ACESToneMap(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr   = texture(uHDRScene, TexCoord).rgb;
    vec3 bloom = texture(uBloom,    TexCoord).rgb;

    vec3 color = hdr * uExposure + bloom * uBloomStrength;

    // ACES tone mapping
    color = ACESToneMap(color);

    // Gamma correction (linear → sRGB)
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}
