#version 450

layout(location = 0) in vec2 inTexCoords;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D prettyTexture;

layout (binding = 1) uniform SceneLighting
{
	vec4 ambient;
	vec4 sunPos;
	vec4 sunColor;
	vec4 viewPos;
};

void main()
{
	vec4 baseFragColor = texture(prettyTexture, inTexCoords);
	
	float localAmbient = 0.1;

	vec3 meLookingAtSun = normalize(sunPos.xyz - inWorldPos);

	float diffuseIntensity = max(dot(inNormal, meLookingAtSun), 0.0);
	vec4 diffuse = diffuseIntensity * sunColor;

	vec3 reflectedLightDir = reflect(meLookingAtSun * -1.0, inNormal);
	vec3 meLookingAtEye = normalize(viewPos.xyz - inWorldPos);
	float specularIntensity = pow(max(dot(meLookingAtEye, reflectedLightDir), 0.0), 128);
	float specularStrength = 0.8;
	vec4 specular = specularStrength * specularIntensity * sunColor;

	outColor = baseFragColor * (localAmbient + diffuse + specular);
}