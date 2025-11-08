#include "Utils.h"
#include <fstream>
#include <iostream>

std::vector<char> Utils::ReadFileBinary(std::string_view path)
{
	std::ifstream file(path.data(), std::ios::binary);
	std::vector<char> data;
	
	if (file)
	{
		file.seekg(0, std::ios_base::end);
		size_t size = (size_t)file.tellg();
		data.resize(size);
		
		file.seekg(0, std::ios_base::beg);
		file.read(data.data(), size);
		file.close();
	}

	return data;
}