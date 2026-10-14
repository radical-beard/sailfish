#include "platform/files.h"

#include <fstream>

namespace sailfish::platform {

bool replace_file(const std::filesystem::path& file, std::string_view contents) noexcept {
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  auto temporary = file;
  temporary += ".tmp";
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!out.flush()) {
      return false;
    }
  }
  std::filesystem::rename(temporary, file, ec);
  return !ec;
}

} // namespace sailfish::platform
