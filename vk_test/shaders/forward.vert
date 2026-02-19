#version 450
#extension GL_EXT_buffer_reference : require

layout(location = 0) out vec2 outTexCoords;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outWorldPos;

struct Vertex {
	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
}; 

layout(buffer_reference, std430) readonly buffer VertexBuffer {
	Vertex vertices[];
};

//push constants block
layout( push_constant ) uniform constants
{	
	mat4 render_matrix; // mvp
	mat4 model_matrix;
	VertexBuffer vertexBuffer;
} PushConstants;

void main() 
{	
	//load vertex data from device address
	Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];

	//output data
	gl_Position = PushConstants.render_matrix * vec4(v.position, 1.0f);

	outTexCoords = vec2(v.uv_x, v.uv_y);
	
	//outNormal = (PushConstants.model_matrix * vec4(v.normal, 1.0f)).xyz;
	outNormal = mat3(transpose(inverse(PushConstants.model_matrix))) * v.normal;

	outWorldPos = (PushConstants.model_matrix * vec4(v.position, 1.0f)).xyz;
}