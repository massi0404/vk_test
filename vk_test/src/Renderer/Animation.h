#pragma once

#include "Core/CoreMinimal.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <vector>

class SkeletalMesh;
struct Animation;
struct Node;

namespace Anim
{
	template<typename Key>
	using TKey = std::pair<Key, float>;

	template<typename Key>
	static Key Lerp(const Key& start, const Key& end, float time)
	{
		// lerp: a = a * (1 - t) + b * t
		return start * (1.0f - time) + end * time; // same as glm::mix()
	}

	template<>
	static glm::quat Lerp(const glm::quat& start, const glm::quat& end, float time)
	{
		glm::quat slerpResult = glm::slerp(start, end, time);
		return glm::normalize(slerpResult); // dunno why, but apparently we need to normalize?!?...
	}

	template<typename Key>
	static Key LerpKeyframes(const std::vector<TKey<Key>>& keyFrames, u32 frameStart, float time)
	{
		if (keyFrames.size() == 1)
			return keyFrames[0].first;

		const Key& keyFrameStart = keyFrames[frameStart].first;
		const Key& keyFrameEnd = keyFrames[frameStart + 1].first;

		return Lerp(keyFrameStart, keyFrameEnd, time);
	}

	template<typename Key>
	static Key GetKey(const std::vector<TKey<Key>>& keyFrames, u32 frameIndex)
	{
		if (keyFrames.size() == 1)
			return keyFrames[0].first;

		return keyFrames[frameIndex].first;
	}

	using KeyFrameData = std::vector<glm::mat4>; // ogni elemento contiene la matrice in bone-space di quel bone

	/*
		TODO: salvare la baked animation interlacciata:
		ogni frame devo sempre interpolare tra frame corrente e frame corrente + 1...
		se invece di salvare ogni frame in un std::vector salviamo accanto al frame corrente il valore del frame dopo
		dovremmo essere piu cache-friendly.


		frame 1									frame 2
			pos1 pos1 pos1 pos1 pos1				pos2 pos2 pos2 pos2 pos2


		diventa...

		frame 1
			pos1 pos2 pos1 pos2 pos1 pos2
	*/

	void BakeKeyFrame(const Node* node, Animation* anim, u32 frameIndex, const glm::mat4& parentMatrix, glm::mat4* outBoneData);
	void BakeAnimation(SkeletalMesh* skMesh, Animation* anim, std::vector<KeyFrameData>& outKeyFrames);

	void GenAnimationFrame(Animation* anim, float animTime, glm::mat4* outBoneMatrices);

}