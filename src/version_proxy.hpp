#pragma once

// Loads the real system version.dll and resolves the forwarded exports.
[[nodiscard]] auto load_real_version() noexcept -> bool;
