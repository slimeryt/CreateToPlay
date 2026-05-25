#version 330 core

in  vec3 FragPos;
in  vec3 Normal;
in  vec4 FragPosLightSpace;
out vec4 FragColor;

uniform vec3      uObjectColor;
uniform float     uReflectance;

// Main sun
uniform vec3      uLightDir;
uniform vec3      uLightColor;

// Hemisphere ambient
uniform vec3      uSkyColor;
uniform vec3      uGroundColor;

// Fill light
uniform vec3      uFillDir;
uniform vec3      uFillColor;

uniform vec3      uViewPos;

// Shadow map
uniform sampler2D uShadowMap;

// Fog
uniform vec3      uFogColor;
uniform float     uFogDensity;

// PCF soft shadow — 3×3 kernel
float ShadowFactor(vec4 fragPosLS) {
    vec3 proj = fragPosLS.xyz / fragPosLS.w;
    proj = proj * 0.5 + 0.5;

    if (proj.z > 1.0) return 0.0;

    float bias   = max(0.006 * (1.0 - dot(Normal, uLightDir)), 0.002);
    float shadow = 0.0;
    vec2  ts     = 1.0 / vec2(textureSize(uShadowMap, 0));

    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float depth = texture(uShadowMap, proj.xy + vec2(x, y) * ts).r;
            shadow += (proj.z - bias > depth) ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

void main() {
    // Hemisphere ambient
    float hemi    = Normal.y * 0.5 + 0.5;
    vec3  ambient = mix(uGroundColor, uSkyColor, hemi) * uObjectColor;

    // Shadow
    float shadow = ShadowFactor(FragPosLightSpace);

    // Main sun diffuse + Blinn-Phong specular
    float diff    = max(dot(Normal, uLightDir), 0.0);
    vec3  diffuse = diff * uLightColor * uObjectColor;

    vec3  viewDir  = normalize(uViewPos - FragPos);
    vec3  halfway  = normalize(uLightDir + viewDir);
    float spec     = pow(max(dot(Normal, halfway), 0.0), 64.0);
    vec3  specular = uReflectance * spec * uLightColor;

    // Fill light (no specular)
    vec3 fill = max(dot(Normal, uFillDir), 0.0) * uFillColor * uObjectColor;

    vec3 lit = ambient + (1.0 - shadow) * (diffuse + specular) + fill;

    // Exponential fog
    float depth     = length(uViewPos - FragPos);
    float fogFactor = clamp(1.0 - exp(-uFogDensity * depth), 0.0, 1.0);
    vec3  color     = mix(lit, uFogColor, fogFactor);

    FragColor = vec4(color, 1.0);
}
