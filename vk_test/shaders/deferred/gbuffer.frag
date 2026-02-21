#version 450

layout(location = 0) in vec3 inVertexColor;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inEntityID;
layout(location = 4) in vec3 inPosition;

layout(location = 0) out vec4 outGBUffer_Albedo;
layout(location = 1) out vec4 outGBUffer_Normals;
layout(location = 2) out vec4 outGBUffer_EntityID;
layout(location = 3) out vec4 outGBUffer_Positions;

layout (binding = 0) uniform sampler2D prettyTexture;

void main()
{
	outGBUffer_Albedo = texture(prettyTexture, inUV);
	outGBUffer_Normals = vec4(inNormal, 1.0);
	outGBUffer_EntityID = vec4(inEntityID, 1.0);
	outGBUffer_Positions = vec4(inPosition, 1.0);
}