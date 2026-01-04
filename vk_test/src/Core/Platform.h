#pragma once

#define PLATFORM_X86 1

#if PLATFORM_X86
	#define CACHELINE_SIZE 64
#else
	#error Unsupported platform!
#endif
