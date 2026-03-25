#version 450
#extension GL_EXT_buffer_reference : require

struct Vertex 
{
	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
}; 

layout(buffer_reference, std430) readonly buffer VertexBuffer 
{
	Vertex vertices[];
};

//push constants block
layout( push_constant ) uniform constants
{	
	mat4 lightViewMatrix;
	VertexBuffer vertexBuffer;
	float padding[2];
} PushConstants;

void main()
{
	//load vertex data from device address
	Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];

	//output data
	gl_Position = PushConstants.lightViewMatrix * vec4(v.position, 1.0f);
}