// Copyright (c) 2025-2026 dr-inf (Florian Krohs)
// SPDX-License-Identifier: MIT

#pragma once

#include <iostream>
#include <sstream>
#include <string_view>

// Platform-specific includes for terminal detection
#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace keys::log {

enum class Level { Debug, Info, Warning, Error };

// ANSI color codes for terminal output
namespace color {
constexpr const char *reset = "\033[0m";
constexpr const char *red = "\033[31m";
constexpr const char *yellow = "\033[33m";
constexpr const char *blue = "\033[34m";
constexpr const char *gray = "\033[90m";
} // namespace color

inline const char *levelPrefix(Level level) {
    switch (level) {
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO ";
    case Level::Warning:
        return "WARN ";
    case Level::Error:
        return "ERROR";
    }
    return "?????";
}

inline const char *levelColor(Level level) {
    switch (level) {
    case Level::Debug:
        return color::gray;
    case Level::Info:
        return color::blue;
    case Level::Warning:
        return color::yellow;
    case Level::Error:
        return color::red;
    }
    return color::reset;
}

inline void log(Level level, std::string_view message) {
    std::ostream &out = (level >= Level::Warning) ? std::cerr : std::cout;

    // Check if output is a terminal (for color support)
    static const bool useColor = isatty(fileno(stdout)) && isatty(fileno(stderr));

    if (useColor) {
        out << levelColor(level) << "[keys " << levelPrefix(level) << "] " << color::reset << message << "\n";
    } else {
        out << "[keys " << levelPrefix(level) << "] " << message << "\n";
    }
}

// Convenience functions
inline void debug(std::string_view message) {
#ifndef NDEBUG // Only in debug builds
    log(Level::Debug, message);
#else
    (void)message;
#endif
}

inline void info(std::string_view message) {
    log(Level::Info, message);
}

inline void warning(std::string_view message) {
    log(Level::Warning, message);
}

inline void error(std::string_view message) {
    log(Level::Error, message);
}

// Helper for formatted messages (variadic)
template <typename... Args> inline std::string format(Args &&...args) {
    std::ostringstream oss;
    (oss << ... << args);
    return oss.str();
}

} // namespace keys::log
