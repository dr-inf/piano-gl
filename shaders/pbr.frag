#version 330 core
in VS_OUT {
    vec3 WorldPos;
    vec3 Normal;
    vec2 UV;
    mat3 TBN;
    vec4 LightPos;
} fsIn;

out vec4 FragColor;

uniform sampler2D uBaseColorTex;
uniform sampler2D uMRRTex;     // R=occlusion, G=roughness, B=metallic
uniform sampler2D uNormalTex;
uniform sampler2D uEmissiveTex;
uniform samplerCube uEnvMap;
uniform mat3 uEnvRot;
uniform sampler2D uShadowMap;

uniform vec4 uBaseColorFactor = vec4(1.0);
uniform float uMetallicFactor = 1.0;
uniform float uRoughnessFactor = 1.0;
uniform vec3 uEmissiveFactor = vec3(0.0);

// Direction toward the light (from the fragment to the light source).
uniform vec3 uLightDir = normalize(vec3(0.3, 1.0, 0.2));
uniform vec3 uLightColor = vec3(1.0);
uniform float uLightIntensity = 3.0;
uniform vec3 uAmbientColor = vec3(0.05);
uniform int uUnlit = 0; // 1 => KHR_materials_unlit path
uniform vec3 uCamPos;
uniform float uDarkenFactor = 1.0;
uniform float uEnvIntensity = 0.25;
uniform float uShadowBias = 0.00015;
uniform int uShadowDebug = 0;
uniform vec3 uGroundNormal = vec3(0.0);
uniform vec3 uGroundPoint = vec3(0.0);
uniform vec2 uGroundParams = vec2(0.0); // x=radius, y=intensity

const float PI = 3.14159265;
const vec2 POISSON[16] = vec2[](
    vec2(-0.326,  0.406),
    vec2(-0.840, -0.074),
    vec2(-0.696,  0.457),
    vec2(-0.203,  0.621),
    vec2( 0.962, -0.195),
    vec2( 0.473, -0.480),
    vec2( 0.519,  0.767),
    vec2( 0.185, -0.893),
    vec2( 0.150,  0.053),
    vec2(-0.511, -0.236),
    vec2(-0.115,  0.252),
    vec2( 0.711,  0.121),
    vec2(-0.657, -0.742),
    vec2( 0.327, -0.933),
    vec2( 0.233,  0.169),
    vec2(-0.760,  0.254)
);

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float distributionGGX(vec3 N, vec3 H, float rough) {
    float a = rough * rough;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 1e-4);
}

float geometrySchlickGGX(float NdotV, float rough) {
    float r = (rough + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float rough) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = geometrySchlickGGX(NdotV, rough);
    float ggx2 = geometrySchlickGGX(NdotL, rough);
    return ggx1 * ggx2;
}

float computeShadow(vec4 lightPos, float NdotL) {
    vec3 proj = lightPos.xyz / lightPos.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float ndl = clamp(NdotL, 0.0, 1.0);
    // Keep bias tied to texel size to reduce peter-panning gaps near occluders.
    float minBias = uShadowBias * 0.05;
    float slopeBias = texel.x * 0.35 * (1.0 - ndl);
    float bias = max(minBias, slopeBias);
    float shadow = 0.0;
    float radius = mix(1.25, 2.2, clamp(proj.z, 0.0, 1.0));
    for (int i = 0; i < 16; ++i) {
        vec2 offset = POISSON[i] * radius * texel;
        float pcfDepth = texture(uShadowMap, proj.xy + offset).r;
        float current = proj.z - bias;
        shadow += current > pcfDepth ? 1.0 : 0.0;
    }
    shadow /= 16.0;
    return shadow;
}

void main() {
    vec4 baseSample = texture(uBaseColorTex, fsIn.UV);
    vec4 baseColor = baseSample * uBaseColorFactor;
    vec3 emissive = texture(uEmissiveTex, fsIn.UV).rgb * uEmissiveFactor;

    vec3 mrt = texture(uMRRTex, fsIn.UV).rgb;
    float perceptualRough = clamp(mrt.g * uRoughnessFactor, 0.02, 1.0);
    float metallic = clamp(mrt.b * uMetallicFactor, 0.0, 1.0);

    vec3 N = normalize(fsIn.Normal);
    vec3 V = normalize(uCamPos - fsIn.WorldPos);
    vec3 nmap = texture(uNormalTex, fsIn.UV).xyz * 2.0 - 1.0;
    N = normalize(fsIn.TBN * nmap);

    if (uUnlit == 1) {
        vec3 color = baseColor.rgb + emissive;
        color = pow(color, vec3(1.0/2.2));
        FragColor = vec4(color, baseColor.a);
        return;
    }

    vec3 L = normalize(uLightDir);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float shadow = computeShadow(fsIn.LightPos, NdotL);

    vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = distributionGGX(N, H, perceptualRough);
    float G = geometrySmith(N, V, L, perceptualRough);

    vec3 numerator = D * G * F;
    float denom = 4.0 * max(NdotL * NdotV, 1e-4);
    vec3 specular = numerator / max(denom, 1e-4);
    //specular *= 12.0;
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kd * baseColor.rgb / PI;

    vec3 ambient = kd * baseColor.rgb * uAmbientColor;
    vec3 R = reflect(-V, N);
    vec3 envDir = normalize(uEnvRot * R);
    vec3 env = texture(uEnvMap, envDir).rgb * uEnvIntensity;
    vec3 envSpec = env * fresnelSchlick(max(dot(N, V), 0.0), F0) * 0.2; // tone down specular IBL
    // Simple diffuse environment approximation using the normal direction.
    vec3 envDiffuse = texture(uEnvMap, normalize(uEnvRot * N)).rgb * uEnvIntensity;
    float groundAO = 1.0;
    if (uGroundParams.x > 0.0 && uGroundParams.y > 0.0) {
        float dist = dot(fsIn.WorldPos - uGroundPoint, uGroundNormal);
        if (dist > 0.0) {
            float t = clamp(dist / uGroundParams.x, 0.0, 1.0);
            float smoothT = t * t * (3.0 - 2.0 * t);
            groundAO = 1.0 - uGroundParams.y * (1.0 - smoothT);
        }
    }

    if (uShadowDebug == 1) {
        float vis = 1.0 - shadow;
        FragColor = vec4(vec3(vis), 1.0);
        return;
    }

    vec3 color = ambient * groundAO + (1.0 - shadow) * ((diffuse + specular) * uLightColor * (uLightIntensity * NdotL)) + emissive;
    // Tint diffuse env light by the surface albedo so it doesn't take only the environment hue.
    color += (1.0 - shadow) * (envDiffuse * baseColor.rgb * kd + envSpec) * groundAO;
    color *= uDarkenFactor;
    color = color / (color + vec3(1.0));   // simple Reinhard
    color = pow(color, vec3(1.0/2.2));     // gamma out
    FragColor = vec4(color, baseColor.a);
}
