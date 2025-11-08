#version 450

layout(location = 0) in vec3 inVertexColor; // gia interpolato
layout(location = 0) out vec4 outFragmentColor;

void main()
{
	outFragmentColor = vec4(inVertexColor, 1.0);
	// outFragmentColor = vec4(1.0, 0.0, 0.0, 1.0f);
}