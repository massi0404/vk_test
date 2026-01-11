#pragma once

#include "glm/glm.hpp"

namespace Math {

	inline glm::vec3 Forward(glm::vec3 radiansRotation)
	{
		glm::vec3 forward;
		forward.x = cos(radiansRotation.x) * sin(radiansRotation.y);
		forward.y = -sin(radiansRotation.x);
		forward.z = cos(radiansRotation.x) * cos(radiansRotation.y);
		return forward;
	}

	inline glm::vec3 Right(glm::vec3 radiansRotation)
	{
		glm::vec3 right;
		right.x = cos(radiansRotation.y);
		right.y = 0.0f;
		right.z = -sin(radiansRotation.y);
		return right;
	}
}