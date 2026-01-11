#pragma once

#include "Core/CoreMinimal.h"
#include <chrono>

class Timer
{
public:
	using clock = std::chrono::steady_clock;

	void Start()
	{
		m_Start = clock::now();
	}

	u64 ElapsedMs()
	{
		auto now = clock::now();
		return std::chrono::duration_cast<std::chrono::milliseconds>(now - m_Start).count();
	}

private:
	std::chrono::time_point<clock> m_Start;
};