#pragma once

#ifdef _MSC_VER 
	#define DEBUG_BREAK() __debugbreak()
#else
	#include <stdlib.h>
	#define DEBUG_BREAK() abort() // ...meh
#endif

#define check(Condition) { if(!(Condition)) __debugbreak(); }