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
	vec4 c3;
};

void main()
{
	vec4 baseFragColor = texture(albedo, inTexCoords);
	vec3 worldPos = vec3(texture(positions, inTexCoords).xyz);
	vec3 worldNormal = vec3(texture(normals, inTexCoords).xyz); // this is actually model normal ...

	vec3 sunDirection = normalize(worldPos - sunPos.xyz); // il sole che punta verso di me

	float diffuse = dot(sunDirection, worldNormal);

	outColor = baseFragColor * (ambient + (diffuse * sunColor));
}