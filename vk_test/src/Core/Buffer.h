#pragma once

#include "Core/CoreMinimal.h"

struct BufferView
{
	void* Data;
	u64 Size;

	template<typename T, u32 ArrayCount>
	constexpr BufferView(T (&array)[ArrayCount])
	{
		Data = array;
		Size = sizeof(array);
	}
};

template<typename T>
struct TBufferView
{
	T* Data;
	u32 Count;

	template<u32 ArrayCount>
	constexpr TBufferView(T (&array)[ArrayCount])
	{
		Data = array;
		Count = ArrayCount;
	}

	T& operator[](u32 index) { check(index < Count); return Data[index]; }
};