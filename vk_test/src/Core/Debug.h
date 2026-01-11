#pragma once

#define ENGINE_DEBUG _DEBUG

#if ENGINE_DEBUG

	#ifdef _MSC_VER
		#define DEBUG_BREAK() __debugbreak()
	#else
		#error Debug break Not implemented!
	#endif

	#define check(Condition) { if(!(Condition)) DEBUG_BREAK(); }
	#define verify(Stmt) check(Stmt)	

#else

	#define check(Condition)
	#define verify(Stmt) Stmt

#endif

void EngineCrash(const char* file, int line, const char* proc, const char* errMsg, ...);

#define CORE_ASSERT(Condition, ErrorMsg, ...) \
{ \
	bool cond = (Condition); \
	if(!cond) \
	{ \
		/* just for debugbreak if debug*/ check(0); \
		EngineCrash(__FILE__, __LINE__, __FUNCTION__, ErrorMsg, __VA_ARGS__); \
	} \
}
	
enum ELogSeverity
{
	Info,
	Warning,
	Error
};

#define DEBUG_LOG_ENABLED 1
#if DEBUG_LOG_ENABLED
	const char* LogSeverityToString(ELogSeverity severity);

	typedef void(*DebugLogHandler)(ELogSeverity severity, const char* msg);
	void SetDebugLogHandler(DebugLogHandler handler);

	void DebugLog(ELogSeverity severity, const char* msg, ...);

	#define DEBUG_LOG(Severity, Msg, ...) DebugLog(Severity, Msg, __VA_ARGS__)
#else
	#define DEBUG_LOG(Severity, Msg, ...)
#endif

#define LOG_INFO(Msg, ...) DEBUG_LOG(ELogSeverity::Info, Msg, __VA_ARGS__)
#define LOG_WARN(Msg, ...) DEBUG_LOG(ELogSeverity::Warning, Msg, __VA_ARGS__)
#define LOG_ERR(Msg, ...)  DEBUG_LOG(ELogSeverity::Error, Msg, __VA_ARGS__)