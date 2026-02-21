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
	vec4 c3;
};

void main()
{
	outColor = vec4(0.0f, 0.4f, 0.1f, 1.0f);
}