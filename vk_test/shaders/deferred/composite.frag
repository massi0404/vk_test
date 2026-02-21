#version 450

layout(location = 0) in vec2 inTexCoords;
layout(location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D albedo;
layout (binding = 1) uniform sampler2D normals;
layout (binding = 2) uniform sampler2D entityid;
layout (binding = 3) uniform sampler2D positions;

layout (binding = 4) uniform SceneLighting
{
	vec4 ambient;
	vec4 sunPos;
	vec4 sunColor;
	vec4 viewPos;
};

void main()
{
	vec4 baseFragColor = texture(albedo, inTexCoords);
	vec3 worldPos = vec3(texture(positions, inTexCoords).xyz);
	vec3 normal = vec3(texture(normals, inTexCoords).xyz);
	
	float localAmbient = 0.1;

	vec3 meLookingAtSun = normalize(sunPos.xyz - worldPos);

	float diffuseIntensity = max(dot(normal, meLookingAtSun), 0.0);
	vec4 diffuse = diffuseIntensity * sunColor;

	vec3 reflectedLightDir = reflect(meLookingAtSun * -1.0, normal);
	vec3 meLookingAtEye = normalize(viewPos.xyz - worldPos);
	float specularIntensity = pow(max(dot(meLookingAtEye, reflectedLightDir), 0.0), 128);
	float specularStrength = 0.8;
	vec4 specular = specularStrength * specularIntensity * sunColor;

	outColor = baseFragColor * (localAmbient + diffuse + specular);
}