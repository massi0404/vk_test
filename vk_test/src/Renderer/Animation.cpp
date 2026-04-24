#include "Animation.h"
#include "SkeletalMesh.h"

namespace Anim
{
	void BakeKeyFrame(const Node* node, Animation* anim, u32 frameIndex, const glm::mat4& parentMatrix, glm::mat4* outBoneData)
	{
		u32 nodeKeyframesIndex = anim->keyFrameMap[node->nodeID];

		glm::mat4 myMatrix = node->localTransform;

		if (nodeKeyframesIndex != -1)
		{
			NodeAnim& keyFrames = anim->nodeKeyframes[nodeKeyframesIndex];

			glm::vec3 pos = GetKey(keyFrames.keyFramesPos, frameIndex);
			glm::quat rot = GetKey(keyFrames.keyFramesRot, frameIndex);
			glm::vec3 sc = GetKey(keyFrames.keyFramesScale, frameIndex);

			glm::mat4 translation = glm::translate(pos);
			glm::mat4 rotation = glm::toMat4(rot);
			glm::mat4 scale = glm::scale(sc);

			myMatrix = translation * rotation * scale;
		}

		glm::mat4 finalTransform = parentMatrix * myMatrix;

		if (node->boneDataIndex != -1)
		{
			outBoneData[node->boneDataIndex] = finalTransform * node->offsetMatrix;
		}

		for (const Node& children : node->children)
			BakeKeyFrame(&children, anim, frameIndex, finalTransform, outBoneData);
	}

	void BakeAnimation(SkeletalMesh* skMesh, Animation* anim, std::vector<KeyFrameData>& outKeyFrames)
	{
		u32 numKeyFrames = anim->frameCount + 1;
		outKeyFrames.resize(numKeyFrames);

		for (u32 i = 0; i < numKeyFrames; i++)
		{
			std::vector<glm::mat4>& frameKeys = outKeyFrames[i];
			frameKeys.resize(skMesh->GetBoneCount());

			BakeKeyFrame(&skMesh->GetRootNode(), anim, i, glm::mat4(1.0f), frameKeys.data());
		}
	}

	void GenAnimationFrame(Animation* anim, float animTime, glm::mat4* outBoneMatrices)
	{
		float frameDurationSeconds = 1.0f / anim->frameRate;
		u32 frameStart = (u32)(animTime / frameDurationSeconds);
		check(frameStart >= 0 && frameStart < anim->frameCount);

		float frameStartTime = frameDurationSeconds * frameStart;
		float frameEndTime = frameStartTime + frameDurationSeconds;
		float keyframesLerpTime = (animTime - frameStartTime) / (frameEndTime - frameStartTime);
		keyframesLerpTime = glm::fclamp(keyframesLerpTime, 0.0f, 1.0f);

		KeyFrameData& currentFrame = anim->bakedAnimData[frameStart];
		KeyFrameData& nextFrame = anim->bakedAnimData[frameStart + 1];

		u32 boneCount = currentFrame.size();
		for (u32 i = 0; i < boneCount; i++)
		{
			glm::mat4& currentTransform = currentFrame[i];
			glm::mat4& nextTransform = nextFrame[i];

			// translation (scale already applied)
			glm::vec4 translationStart = { currentTransform[3][0], currentTransform[3][1], currentTransform[3][2], 1.0f };
			glm::vec4 translationEnd = { nextTransform[3][0], nextTransform[3][1], nextTransform[3][2], 1.0f };

			glm::vec4 currentFrameTranslation = Lerp(translationStart, translationEnd, keyframesLerpTime);

			// rotation
			glm::quat rotationStart = glm::quat_cast(currentTransform);
			glm::quat rotationEnd = glm::quat_cast(nextTransform);

			glm::quat currentFrameRotation = Lerp(rotationStart, rotationEnd, keyframesLerpTime);

			outBoneMatrices[i] = glm::mat4_cast(currentFrameRotation);
			outBoneMatrices[i][3] = currentFrameTranslation;
		}
	}
}