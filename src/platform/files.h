#pragma once
#include <filesystem>
#include <string_view>

namespace sailfish::platform {

// Replaces `file` with `contents` in one step (a temporary file renamed over
// it), so a reader sees the old content or the new, never a partial write.
// Creates missing directories. Returns false, leaving `file` as it was, if
// that isn't possible.
bool replace_file(const std::filesystem::path& file, std::string_view contents) noexcept;

} // namespace sailfish::platform
