#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <string>

#include "spdlog/spdlog.h"

#define LOG_TRACE(...)   spdlog::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::trace, __VA_ARGS__)
#define LOG_DEBUG(...)   spdlog::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::debug, __VA_ARGS__)
#define LOG_DEBUG_FUN(FUN, ...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, (FUN)}, spdlog::level::debug, __VA_ARGS__)
#define LOG_INFO(...)    spdlog::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::info, __VA_ARGS__)
#define LOG_WARNING(...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...)   spdlog::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::err, __VA_ARGS__)
