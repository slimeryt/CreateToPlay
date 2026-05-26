#pragma once

// ── Default server address (override by placing server.txt next to the exe) ──
static constexpr const char* kEmbeddedServerAddr = "zephyr.proxy.rlwy.net:40835";

// ── Shaders ───────────────────────────────────────────────────────────────────

static const char* kPhongVert = R"GLSL(
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
)GLSL";

static const char* kPhongFrag = R"GLSL(
#version 330 core

in  vec3 FragPos;
in  vec3 Normal;
in  vec4 FragPosLightSpace;
out vec4 FragColor;

uniform vec3      uObjectColor;
uniform float     uReflectance;
uniform vec3      uLightDir;
uniform vec3      uLightColor;
uniform vec3      uSkyColor;
uniform vec3      uGroundColor;
uniform vec3      uFillDir;
uniform vec3      uFillColor;
uniform vec3      uViewPos;
uniform sampler2D uShadowMap;
uniform vec3      uFogColor;
uniform float     uFogDensity;

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
    float hemi    = Normal.y * 0.5 + 0.5;
    vec3  ambient = mix(uGroundColor, uSkyColor, hemi) * uObjectColor;
    float shadow  = ShadowFactor(FragPosLightSpace);
    float diff    = max(dot(Normal, uLightDir), 0.0);
    vec3  diffuse = diff * uLightColor * uObjectColor;
    vec3  viewDir  = normalize(uViewPos - FragPos);
    vec3  halfway  = normalize(uLightDir + viewDir);
    float spec     = pow(max(dot(Normal, halfway), 0.0), 64.0);
    vec3  specular = uReflectance * spec * uLightColor;
    vec3  fill     = max(dot(Normal, uFillDir), 0.0) * uFillColor * uObjectColor;
    vec3  lit      = ambient + (1.0 - shadow) * (diffuse + specular) + fill;
    float depth     = length(uViewPos - FragPos);
    float fogFactor = clamp(1.0 - exp(-uFogDensity * depth), 0.0, 1.0);
    FragColor = vec4(mix(lit, uFogColor, fogFactor), 1.0);
}
)GLSL";

static const char* kShadowVert = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uLightSpaceMatrix;
uniform mat4 uModel;
void main() {
    gl_Position = uLightSpaceMatrix * uModel * vec4(aPos, 1.0);
}
)GLSL";

static const char* kShadowFrag = R"GLSL(
#version 330 core
void main() {}
)GLSL";

static const char* kQuadVert = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 TexCoord;
void main() {
    TexCoord    = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

static const char* kBloomExtractFrag = R"GLSL(
#version 330 core
in  vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D uScene;
uniform float     uThreshold;
void main() {
    vec3  color      = texture(uScene, TexCoord).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    FragColor = brightness > uThreshold ? vec4(color, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
}
)GLSL";

static const char* kBlurFrag = R"GLSL(
#version 330 core
in  vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D uImage;
uniform bool      uHorizontal;
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
)GLSL";

// Sky shader — uses kQuadVert, renders behind all geometry
static const char* kSkyFrag = R"GLSL(
#version 330 core
in  vec2 TexCoord;
out vec4 FragColor;

uniform mat4 uInvProj;
uniform mat4 uInvView;
uniform vec3 uSunDir;

void main() {
    // Reconstruct world-space view direction from UV
    vec2  ndc     = TexCoord * 2.0 - 1.0;
    vec4  rayClip = vec4(ndc, -1.0, 1.0);
    vec4  rayEye  = uInvProj * rayClip;
    rayEye        = vec4(rayEye.xy, -1.0, 0.0);
    vec3  ray     = normalize((uInvView * rayEye).xyz);

    // Sky gradient: ground → horizon → zenith
    float t       = ray.y;
    vec3  zenith  = vec3(0.10, 0.20, 0.50);
    vec3  horizon = vec3(0.52, 0.72, 0.94);
    vec3  ground  = vec3(0.32, 0.38, 0.32);

    vec3 sky;
    if (t >= 0.0)
        sky = mix(horizon, zenith, pow(t, 0.6));
    else
        sky = mix(horizon, ground, min(-t * 5.0, 1.0));

    // Sun halo + disc
    float sunDot  = dot(ray, normalize(uSunDir));
    float sunHalo = pow(max(sunDot, 0.0), 60.0) * 0.30;
    float sunDisc = step(0.9997, sunDot);
    sky += vec3(1.2, 1.0, 0.7) * sunHalo;
    sky  = mix(sky, vec3(3.0, 2.8, 2.2), sunDisc);

    FragColor = vec4(sky, 1.0);
}
)GLSL";

static const char* kCompositeFrag = R"GLSL(
#version 330 core
in  vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D uHDRScene;
uniform sampler2D uBloom;
uniform float     uExposure;
uniform float     uBloomStrength;
vec3 ACESToneMap(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
void main() {
    vec3 hdr   = texture(uHDRScene, TexCoord).rgb;
    vec3 bloom = texture(uBloom,    TexCoord).rgb;
    vec3 color = ACESToneMap(hdr * uExposure + bloom * uBloomStrength);
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
)GLSL";
