#pragma once

#include <windows.h>

[[nodiscard]] auto disable_asar_integrity() noexcept -> bool;
[[nodiscard]] auto load_version_proxy() noexcept -> bool;
