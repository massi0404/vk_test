#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 inTexCoords;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec4 inShadowView;
layout(location = 4) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D prettyTexture;

layout (set = 1, binding = 0) uniform sampler2D allTextures[];


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

layout (binding = 2) uniform sampler2D depthMapTexture;

void main()
{
	vec4 baseFragColor = inColor;

	if(textureIndex != 0xffffffff)
		baseFragColor *= texture(allTextures[textureIndex], inTexCoords);
	
	float localAmbient = 0.1;

	vec3 meLookingAtSun = normalize(sunPos.xyz - inWorldPos);

	float diffuseIntensity = max(dot(normalize(inNormal), meLookingAtSun), 0.0);
	vec4 diffuse = diffuseIntensity * sunColor;

	vec3 reflectedLightDir = reflect(meLookingAtSun * -1.0, normalize(inNormal));
	vec3 meLookingAtEye = normalize(viewPos.xyz - inWorldPos);
	float specularIntensity = pow(max(dot(meLookingAtEye, reflectedLightDir), 0.0), 32);
	float specularStrength = 0.5;
	vec4 specular = specularStrength * specularIntensity * sunColor;

	float shadow = 1.0;
	
	if(shadowMappingEnabled == 1)
	{
		vec4 lightSpaceFrag4 = inShadowView;
		vec3 lightSpaceFrag3 = lightSpaceFrag4.xyz / lightSpaceFrag4.w; // perspective devide (useless with ortho)

		float currentDepth = lightSpaceFrag3.z;


		if(currentDepth < 1.0)
		{
			vec2 uv = lightSpaceFrag3.xy * 0.5 + 0.5; // [-1, +1] -> [0, 1]

			if((uv.x < 1 && uv.x > 0) && (uv.y < 1 && uv.y > 0))
			{
				uv.y = 1 - uv.y; // UV y flip

				if(currentDepth > texture(depthMapTexture, uv).r + 0.005)
					shadow = 0.0;
			}

		}
	}

	outColor = baseFragColor * shadow * (localAmbient + diffuse + specular);

	//outColor = inColor;
}