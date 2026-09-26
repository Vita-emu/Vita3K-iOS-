#pragma once
// Only the logging sink is replaced; CPU/memory headers and adapters are real.
#define LOG_TRACE(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_CRITICAL(...) ((void)0)
