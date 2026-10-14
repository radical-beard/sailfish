#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwrite_2.h>

#include <stdexcept>

#include "platform/font_locator.h"
#include "platform/text_encoding.h"

namespace sailfish::platform {

namespace {

// Minimal COM owner; avoids pulling in WRL for a handful of interfaces.
template <typename T>
class Com {
public:
  Com() = default;
  Com(const Com&) = delete;
  Com& operator=(const Com&) = delete;
  ~Com() {
    if (p_) {
      p_->Release();
    }
  }
  T** out() { return &p_; }
  T* operator->() const { return p_; }
  [[nodiscard]] T* get() const { return p_; }
  explicit operator bool() const { return p_ != nullptr; }

private:
  T* p_ = nullptr;
};

// The single-character text MapCharacters() asks about.
class CharSource final : public IDWriteTextAnalysisSource {
public:
  explicit CharSource(std::wstring text) : text_(std::move(text)) {}

  HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 pos, const WCHAR** text, UINT32* len) override {
    *text = pos < text_.size() ? text_.c_str() + pos : nullptr;
    *len = pos < text_.size() ? static_cast<UINT32>(text_.size() - pos) : 0;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 pos, const WCHAR** text, UINT32* len) override {
    *text = pos > 0 && pos <= text_.size() ? text_.c_str() : nullptr;
    *len = pos <= text_.size() ? pos : 0;
    return S_OK;
  }
  DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override { return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT; }
  HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32, UINT32* len, const WCHAR** locale) override {
    *locale = L"en-us";
    *len = static_cast<UINT32>(text_.size());
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32, UINT32* len, IDWriteNumberSubstitution** sub) override {
    *sub = nullptr;
    *len = static_cast<UINT32>(text_.size());
    return S_OK;
  }
  // Stack-allocated: reference counting is a formality here.
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTextAnalysisSource)) {
      *out = this;
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }

private:
  std::wstring text_;
};

std::optional<FontFile> file_of(IDWriteFont* font) {
  Com<IDWriteFontFace> face;
  if (FAILED(font->CreateFontFace(face.out()))) {
    return std::nullopt;
  }
  UINT32 count = 1;
  Com<IDWriteFontFile> file;
  if (FAILED(face->GetFiles(&count, file.out())) || !file) {
    return std::nullopt;
  }
  const void* key = nullptr;
  UINT32 key_size = 0;
  Com<IDWriteFontFileLoader> loader;
  Com<IDWriteLocalFontFileLoader> local;
  if (FAILED(file->GetReferenceKey(&key, &key_size)) || FAILED(file->GetLoader(loader.out())) ||
      FAILED(loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader), reinterpret_cast<void**>(local.out())))) {
    return std::nullopt;  // not a font on disk
  }
  UINT32 length = 0;
  local->GetFilePathLengthFromKey(key, key_size, &length);
  std::wstring path(length + 1, L'\0');
  local->GetFilePathFromKey(key, key_size, path.data(), length + 1);
  path.resize(length);

  std::string postscript;
  Com<IDWriteLocalizedStrings> names;
  BOOL exists = FALSE;
  if (SUCCEEDED(font->GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME, names.out(), &exists)) && exists) {
    UINT32 n = 0;
    names->GetStringLength(0, &n);
    std::wstring buffer(n + 1, L'\0');
    names->GetString(0, buffer.data(), n + 1);
    buffer.resize(n);
    postscript = narrow(buffer);
  }
  return FontFile{std::filesystem::path(path), postscript};
}

class DirectWriteLocator final : public FontLocator {
public:
  DirectWriteLocator() {
    // IDWriteFactory2 (system font fallback) needs Windows 8.1 or later.
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), reinterpret_cast<IUnknown**>(factory_.out()))) ||
        FAILED(factory_->GetSystemFontCollection(collection_.out())) || FAILED(factory_->GetSystemFontFallback(fallback_.out()))) {
      throw std::runtime_error("DirectWrite is unavailable; Sailfish needs Windows 8.1 or later");
    }
  }

  std::optional<FontFile> find(std::string_view family, FontStyle style) override {
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (FAILED(collection_->FindFamilyName(widen(family).c_str(), &index, &exists)) || !exists) {
      return std::nullopt;
    }
    Com<IDWriteFontFamily> fam;
    Com<IDWriteFont> font;
    if (FAILED(collection_->GetFontFamily(index, fam.out())) ||
        FAILED(fam->GetFirstMatchingFont(weight(style), DWRITE_FONT_STRETCH_NORMAL, slant(style), font.out()))) {
      return std::nullopt;
    }
    return file_of(font.get());
  }

  std::optional<FontFile> fallback_for(char32_t codepoint, FontStyle style) override {
    std::wstring text;
    if (codepoint > 0xffff) {
      const char32_t v = codepoint - 0x10000;
      text += static_cast<wchar_t>(0xd800 + (v >> 10));
      text += static_cast<wchar_t>(0xdc00 + (v & 0x3ff));
    } else {
      text += static_cast<wchar_t>(codepoint);
    }
    CharSource source(text);
    UINT32 mapped_length = 0;
    FLOAT scale = 1;
    Com<IDWriteFont> font;
    if (FAILED(fallback_->MapCharacters(&source, 0, static_cast<UINT32>(text.size()), collection_.get(), nullptr, weight(style), slant(style),
          DWRITE_FONT_STRETCH_NORMAL, &mapped_length, font.out(), &scale)) || !font) {
      return std::nullopt;
    }
    return file_of(font.get());
  }

  [[nodiscard]] std::vector<std::string> default_families() const override {
    return {"JetBrainsMono Nerd Font Mono", "JetBrains Mono", "Cascadia Mono", "Consolas"};
  }

private:
  static DWRITE_FONT_WEIGHT weight(FontStyle s) { return s.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_REGULAR; }
  static DWRITE_FONT_STYLE slant(FontStyle s) { return s.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL; }

  Com<IDWriteFactory2> factory_;
  Com<IDWriteFontCollection> collection_;
  Com<IDWriteFontFallback> fallback_;
};

} // namespace

std::unique_ptr<FontLocator> make_font_locator() { return std::make_unique<DirectWriteLocator>(); }

} // namespace sailfish::platform
