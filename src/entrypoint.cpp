#include "utils.h"

int main() {
    spdlog::flush_on(spdlog::level::trace);
    spdlog::set_level(spdlog::level::trace);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] %^[%l]%$ [%s:%# %!] %v");

    LOG_INFO("Program startup.");

    return 0;
}
