#include "Debug.h"

#include <stdlib.h>
#include <cstdarg>
#include <string>
#include <format>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

void EngineCrash(const char* file, int line, const char* proc, const char* errMsg, ...)
{
	char userMsg[512];

	va_list args;
	va_start(args, errMsg);
	vsnprintf((char*)userMsg, 512, errMsg, args);
	va_end(args);

	std::string finalErrorMsg = std::format("Fatal error at {} [{}:{}]\n{}", proc, file, line, errMsg);
	
	LOG_ERR(finalErrorMsg.c_str());
	MessageBoxA(nullptr, finalErrorMsg.c_str(), "Fatal error!", MB_TASKMODAL | MB_ICONERROR | MB_OK | MB_TOPMOST);

	exit(-1);
}

#if DEBUG_LOG_ENABLED

const char* LogSeverityToString(ELogSeverity severity)
{
	switch (severity)
	{
	case ELogSeverity::Info:    return "INFO";
	case ELogSeverity::Warning: return "WARNING";
	case ELogSeverity::Error:   return "ERROR";
	}

	return "???";
}

#include <iostream>

void DefaultDebugLogHandler(ELogSeverity severity, const char* msg)
{
	static HANDLE stdOutputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	static constexpr int DEFAULT_TEXT_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

	int textColor = DEFAULT_TEXT_COLOR;

	if (severity == ELogSeverity::Error)
		textColor = FOREGROUND_RED;
	else if (severity == ELogSeverity::Warning)
		textColor = FOREGROUND_RED | FOREGROUND_GREEN;

	SetConsoleTextAttribute(stdOutputHandle, textColor);
	std::cout << std::format("LOG [{}]: {}\n", LogSeverityToString(severity), msg);
}

static DebugLogHandler s_DebugHandler = DefaultDebugLogHandler;

void SetDebugLogHandler(DebugLogHandler handler)
{
	s_DebugHandler = handler;
}

void DebugLog(ELogSeverity severity, const char* msg, ...)
{
	if (!s_DebugHandler)
		return;

	char userMsg[1024];

	va_list args;
	va_start(args, msg);
	vsnprintf((char*)userMsg, 1024, msg, args);
	va_end(args);

	s_DebugHandler(severity, userMsg);
}

#endif