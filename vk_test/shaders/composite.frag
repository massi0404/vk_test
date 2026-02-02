#version 450

layout(location = 0) in vec2 inTexCoords;
layout(location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D albedo;
layout (binding = 1) uniform sampler2D normals;
layout (binding = 2) uniform sampler2D entityid;

void main()
{
	outColor = texture(albedo, inTexCoords);
}