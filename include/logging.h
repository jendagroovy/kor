#ifndef LOGGING_H
#define LOGGING_H

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_ERROR 3

#define LOG_LEVEL LOG_LEVEL_INFO

// LOG_DEBUG
#if (LOG_LEVEL <= LOG_LEVEL_DEBUG)
#define LOG_DEBUG(msg, ...) Serial.print(msg, ##__VA_ARGS__)
#define LOGLN_DEBUG(msg, ...) Serial.println(msg, ##__VA_ARGS__)
#else
#define LOG_DEBUG(msg, ...)
#define LOGLN_DEBUG(msg, ...)
#endif

// LOG_INFO
#if (LOG_LEVEL <= LOG_LEVEL_INFO)
#define LOG_INFO(msg, ...) Serial.print(msg, ##__VA_ARGS__)
#define LOGLN_INFO(msg, ...) Serial.println(msg, ##__VA_ARGS__)
#else
#define LOG_INFO(msg, ...)
#define LOGLN_INFO(msg, ...)
#endif

// LOG_WARN
#if (LOG_LEVEL <= LOG_LEVEL_WARN)
#define LOG_WARN(msg, ...) Serial.print(msg, ##__VA_ARGS__)
#define LOGLN_WARN(msg, ...) Serial.println(msg, ##__VA_ARGS__)
#else
#define LOG_WARN(msg, ...)
#define LOGLN_WARN(msg, ...)
#endif

// LOG_ERROR
#if (LOG_LEVEL <= LOG_LEVEL_ERROR)
#define LOG_ERROR(msg, ...) Serial.print(msg, ##__VA_ARGS__)
#define LOGLN_ERROR(msg, ...) Serial.println(msg, ##__VA_ARGS__)
#else
#define LOG_ERROR(msg, ...)
#define LOGLN_ERROR(msg, ...)
#endif

#endif