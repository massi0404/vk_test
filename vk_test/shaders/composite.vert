#version 450
#extension GL_EXT_buffer_reference : require

vec2 positions[6] = {
	vec2(-1, 1),
	vec2(1, 1),
	vec2(1, -1),
	vec2(-1, -1),
	vec2(-1, 1),
	vec2(1, -1)
};

layout(location = 0) out vec2 outTexCoords;

void main()
{
	gl_Position = vec4(positions[gl_VertexIndex], 0, 1.0);

	outTexCoords.x = (gl_Position.x + 1.0) / 2.0;
	outTexCoords.y = (gl_Position.y + 1.0) / 2.0 * -1.0;
}