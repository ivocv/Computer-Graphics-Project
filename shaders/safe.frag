#version 330 core

in vec3 vWorldPosition;
in vec3 vNormal;

uniform vec3 uViewPosition;
uniform vec3 uLightPosition;
uniform vec3 uLightColor;
uniform vec3 uBaseColor;
uniform vec3 uEmissive;
uniform float uMetalness;
uniform float uRoughness;
uniform int uBrushed;

out vec4 fragmentColor;

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 lightDirection = normalize(uLightPosition - vWorldPosition);
    vec3 viewDirection = normalize(uViewPosition - vWorldPosition);
    vec3 halfDirection = normalize(lightDirection + viewDirection);

    float distanceToLight = length(uLightPosition - vWorldPosition);
    float attenuation = 1.0 / (1.0 + 0.045 * distanceToLight + 0.008 * distanceToLight * distanceToLight);
    float diffuseStrength = max(dot(normal, lightDirection), 0.0);
    float specularExponent = mix(12.0, 128.0, 1.0 - uRoughness);
    float specularStrength = pow(max(dot(normal, halfDirection), 0.0), specularExponent);

    vec3 ambient = 0.22 * uBaseColor;
    vec3 diffuse = diffuseStrength * uBaseColor * uLightColor * attenuation;
    vec3 specularColor = mix(vec3(0.35), uBaseColor, uMetalness);
    vec3 specular = specularStrength * specularColor * (0.30 + 0.85 * uMetalness) * uLightColor * attenuation;

    float brushedPattern = 1.0;
    if (uBrushed == 1)
    {
        float fineLines = sin(vWorldPosition.y * 260.0) * 0.018;
        float broadLines = sin(vWorldPosition.y * 36.0 + vWorldPosition.x * 4.0) * 0.024;
        brushedPattern += fineLines + broadLines;
    }

    vec3 color = (ambient + diffuse + specular) * brushedPattern + uEmissive;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    fragmentColor = vec4(color, 1.0);
}
