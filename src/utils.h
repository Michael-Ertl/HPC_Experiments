#pragma once

#include <cstdint>
#include <format>
#include <vector>
#include <memory>
#include <string>
#include <variant>

#include <spdlog/spdlog.h>

#ifdef TRACY_ENABLE
#include "tracy/Tracy.hpp"
#define INSTRUMENT_SCOPE(name) ZoneScopedN(name)
#define TRACY_PLOT(name, value) TracyPlot(name, (int64_t)(value))
#else
#define INSTRUMENT_SCOPE(name)
#define TRACY_PLOT(name, value)
#endif

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

#define LOG_TRACE(...)   spdlog::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::trace, __VA_ARGS__)
#define LOG_DEBUG(...)   spdlog::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::debug, __VA_ARGS__)
#define LOG_DEBUG_FUN(FUN, ...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, (FUN)}, spdlog::level::debug, __VA_ARGS__)
#define LOG_INFO(...)    spdlog::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::info, __VA_ARGS__)
#define LOG_WARNING(...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...)   spdlog::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::err, __VA_ARGS__)

#define NAMESPACE_BEGIN(name) namespace name {
#define NAMESPACE_END() }

struct string_view {
	char *buffer;
	size_t length;
};

inline bool equalTo(u8 *ptr, size_t n, u8 target) {
	for (size_t i = 0; i < n; i++) {
		if (ptr[i] != target) return false;
	}
	return true;
}
