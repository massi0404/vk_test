#version 450
#extension GL_EXT_buffer_reference : require

layout (location = 0) out vec3 outColor; // for albedo
layout (location = 1) out vec2 outUV; // for albedo
layout (location = 2) out vec3 outNormal; // for lighting
layout (location = 3) out vec3 outEntityID; // misc, mouse picking bla bla
layout(location = 4) out vec3 outPosition; // for lighting

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
	
	outColor = v.color.xyz;
	outUV.x = v.uv_x;
	outUV.y = v.uv_y;

	//outNormal = (PushConstants.model_matrix * vec4(v.normal, 1.0f)).xyz;
	outNormal = mat3(transpose(inverse(PushConstants.model_matrix))) * v.normal; // normal matrix
	outEntityID = vec3(0.0, 0.0, 1.0); // blue for debug
	outPosition = (PushConstants.model_matrix * vec4(v.position, 1.0f)).xyz;
}