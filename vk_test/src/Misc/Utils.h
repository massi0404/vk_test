#pragma once

#include "Core/CoreMinimal.h"
#include <vector>
#include <string>

namespace Utils {

	std::vector<char> ReadFileBinary(std::string_view path);
	float BytesToMegabytes(u64 sizeBytes);

}