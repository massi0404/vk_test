#version 450
#extension GL_EXT_buffer_reference : require

layout(location = 0) out vec2 outTexCoords;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec4 outShadowView;
layout(location = 4) out vec4 outColor;

struct Vertex {
	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
	uint bones[4];
	float weights[4];
}; 

layout(buffer_reference, std430) readonly buffer VertexBuffer {
	Vertex vertices[];
};

layout(buffer_reference, std430) readonly buffer AnimBuffer {
	mat4 boneData[];
};

//push constants block
layout( push_constant ) uniform constants
{	
	mat4 render_matrix; // mvp
	mat4 model_matrix;
	VertexBuffer vertexBuffer;
	AnimBuffer animBuffer;

} PushConstants;

layout (binding = 1) uniform SceneLighting
{
	mat4 shadowView;
	vec4 ambient;
	vec4 sunPos;
	vec4 sunColor;
	vec4 viewPos;
	uint textureIndex;
	uint shadowMappingEnabled;
	uint pad[2];
};

void main()
{	
	//load vertex data from device address
	Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];

	vec4 finalPos = vec4(0.0f);
	vec3 finalNorm = vec3(0.0f);

	int done = 0;

	for(int i = 0; i < 4; i++)
	{
		uint boneIndex = v.bones[i];
		float weight = v.weights[i];
		
		if(boneIndex != -1)
		{
			done = 1;

			mat4 boneMat = PushConstants.animBuffer.boneData[boneIndex];
			vec4 pos = boneMat * vec4(v.position, 1.0);
			vec3 norm = mat3(boneMat) * v.normal; // why not transpose(inverse(boneMat)) ???

			finalPos += pos * weight;
			finalNorm += norm * weight;
		}
	}

	if(done == 0)
	{
		finalPos = vec4(v.position, 1.0f);
		finalNorm = v.normal;
	}
		

	//output data
	gl_Position = PushConstants.render_matrix * finalPos;

	outTexCoords = vec2(v.uv_x, v.uv_y);
	
	outNormal = mat3(transpose(inverse(PushConstants.model_matrix))) * finalNorm; // normal matrix

	outWorldPos = (PushConstants.model_matrix * finalPos).xyz;
	
	outShadowView = shadowView * vec4(outWorldPos, 1.0f);

	outColor = v.color;
}