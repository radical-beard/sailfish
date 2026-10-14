#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>

#include <cmath>
#include <limits>

#include "platform/font_locator.h"

namespace sailfish::platform {

namespace {

// Owns one CoreFoundation reference.
template <typename T>
class CfRef {
public:
  CfRef() = default;
  explicit CfRef(T ref) : ref_(ref) {}
  CfRef(const CfRef&) = delete;
  CfRef& operator=(const CfRef&) = delete;
  CfRef(CfRef&& o) noexcept : ref_(o.ref_) { o.ref_ = nullptr; }
  ~CfRef() {
    if (ref_) {
      CFRelease(ref_);
    }
  }
  [[nodiscard]] T get() const { return ref_; }
  explicit operator bool() const { return ref_ != nullptr; }

private:
  T ref_ = nullptr;
};

CfRef<CFStringRef> cf_string(std::string_view s) {
  return CfRef<CFStringRef>(CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(s.data()),
    static_cast<CFIndex>(s.size()), kCFStringEncodingUTF8, false));
}

std::string to_std_string(CFStringRef s) {
  if (!s) {
    return {};
  }
  const CFIndex max = CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8) + 1;
  std::string out(static_cast<size_t>(max), '\0');
  if (!CFStringGetCString(s, out.data(), max, kCFStringEncodingUTF8)) {
    return {};
  }
  out.resize(std::char_traits<char>::length(out.c_str()));
  return out;
}

std::optional<FontFile> file_of(CTFontDescriptorRef desc) {
  CfRef<CFURLRef> url(static_cast<CFURLRef>(CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute)));
  if (!url) {
    return std::nullopt;
  }
  char path[PATH_MAX];
  if (!CFURLGetFileSystemRepresentation(url.get(), true, reinterpret_cast<UInt8*>(path), sizeof(path))) {
    return std::nullopt;
  }
  CfRef<CFStringRef> name(static_cast<CFStringRef>(CTFontDescriptorCopyAttribute(desc, kCTFontNameAttribute)));
  return FontFile{path, to_std_string(name.get())};
}

// How far a face is from the wanted style; weights are CoreText's -1..1 scale
// where regular is 0 and bold about 0.4.
double style_distance(CTFontDescriptorRef desc, FontStyle style) {
  CfRef<CFDictionaryRef> traits(static_cast<CFDictionaryRef>(CTFontDescriptorCopyAttribute(desc, kCTFontTraitsAttribute)));
  double weight = 0;
  CTFontSymbolicTraits symbolic = 0;
  if (traits) {
    if (auto n = static_cast<CFNumberRef>(CFDictionaryGetValue(traits.get(), kCTFontWeightTrait))) {
      CFNumberGetValue(n, kCFNumberDoubleType, &weight);
    }
    if (auto n = static_cast<CFNumberRef>(CFDictionaryGetValue(traits.get(), kCTFontSymbolicTrait))) {
      CFNumberGetValue(n, kCFNumberSInt32Type, &symbolic);
    }
  }
  const bool italic = (symbolic & kCTFontItalicTrait) != 0;
  const double wanted_weight = style.bold ? 0.4 : 0.0;
  return std::abs(weight - wanted_weight) + (italic == style.italic ? 0.0 : 10.0);
}

class CoreTextFontLocator final : public FontLocator {
public:
  std::optional<FontFile> find(std::string_view family, FontStyle style) override {
    auto name = cf_string(family);
    const void* keys[] = {kCTFontFamilyNameAttribute};
    const void* values[] = {name.get()};
    CfRef<CFDictionaryRef> attrs(CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    CfRef<CTFontDescriptorRef> wanted(CTFontDescriptorCreateWithAttributes(attrs.get()));
    CfRef<CFSetRef> mandatory(CFSetCreate(kCFAllocatorDefault, keys, 1, &kCFTypeSetCallBacks));
    CfRef<CFArrayRef> matches(CTFontDescriptorCreateMatchingFontDescriptors(wanted.get(), mandatory.get()));
    if (!matches) {
      return std::nullopt;
    }
    CTFontDescriptorRef best = nullptr;
    double best_distance = std::numeric_limits<double>::max();
    for (CFIndex i = 0; i < CFArrayGetCount(matches.get()); ++i) {
      auto desc = static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(matches.get(), i));
      if (const double d = style_distance(desc, style); d < best_distance) {
        best_distance = d;
        best = desc;
      }
    }
    return best ? file_of(best) : std::nullopt;
  }

  std::optional<FontFile> fallback_for(char32_t codepoint, FontStyle) override {
    // CoreText's cascade list picks the font the system itself would use.
    CfRef<CTFontRef> base(CTFontCreateWithName(CFSTR("Menlo-Regular"), 13.0, nullptr));
    UniChar utf16[2];
    CFIndex length = 1;
    if (codepoint > 0xffff) {
      const char32_t v = codepoint - 0x10000;
      utf16[0] = static_cast<UniChar>(0xd800 + (v >> 10));
      utf16[1] = static_cast<UniChar>(0xdc00 + (v & 0x3ff));
      length = 2;
    } else {
      utf16[0] = static_cast<UniChar>(codepoint);
    }
    CfRef<CFStringRef> text(CFStringCreateWithCharacters(kCFAllocatorDefault, utf16, length));
    CfRef<CTFontRef> font(CTFontCreateForString(base.get(), text.get(), CFRangeMake(0, length)));
    if (!font) {
      return std::nullopt;
    }
    CfRef<CTFontDescriptorRef> desc(CTFontCopyFontDescriptor(font.get()));
    return file_of(desc.get());
  }

  [[nodiscard]] std::vector<std::string> default_families() const override {
    return {"JetBrainsMono Nerd Font Mono", "JetBrainsMono Nerd Font", "JetBrains Mono", "SF Mono", "Menlo"};
  }
};

} // namespace

std::unique_ptr<FontLocator> make_font_locator() { return std::make_unique<CoreTextFontLocator>(); }

} // namespace sailfish::platform
