# Every dependency prefers an installed package and falls back to a pinned
# source build, so a fresh checkout builds the same way on every OS.
include(FetchContent)

# SDL3: window, keyboard, clipboard and the GPU-backed 2D renderer.
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.16
  GIT_SHALLOW TRUE
  FIND_PACKAGE_ARGS CONFIG)
FetchContent_MakeAvailable(SDL3)

# FreeType: glyph rasterization. The fetched build drops optional codecs,
# PNG among them, so color emoji (PNG-based sbix/CBDT fonts) only render with
# an installed FreeType (e.g. `brew install freetype`); outlines are unaffected.
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
FetchContent_Declare(freetype
  GIT_REPOSITORY https://github.com/freetype/freetype.git
  GIT_TAG VER-2-14-3
  GIT_SHALLOW TRUE
  FIND_PACKAGE_ARGS NAMES Freetype)
FetchContent_MakeAvailable(freetype)
if(TARGET Freetype::Freetype)
  add_library(sailfish_freetype ALIAS Freetype::Freetype)
else()
  add_library(sailfish_freetype ALIAS freetype)
endif()

# HarfBuzz: shaping (ligatures, combining marks). Used with its own OpenType
# font functions, so it needs no FreeType integration.
set(HB_HAVE_FREETYPE OFF CACHE BOOL "" FORCE)
set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(HB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(harfbuzz
  GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
  GIT_TAG 14.6.0
  GIT_SHALLOW TRUE
  FIND_PACKAGE_ARGS NAMES harfbuzz)
FetchContent_MakeAvailable(harfbuzz)
if(TARGET harfbuzz::harfbuzz)
  add_library(sailfish_harfbuzz ALIAS harfbuzz::harfbuzz)
else()
  add_library(sailfish_harfbuzz ALIAS harfbuzz)
endif()

if(SAILFISH_BUILD_TESTS)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.3
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS)
  FetchContent_MakeAvailable(doctest)
endif()

# Nerd Font symbols: the guaranteed fallback for icons (statuslines, file
# trees) when the user's font isn't Nerd-Font patched.
FetchContent_Declare(nerd_symbols
  URL https://github.com/ryanoasis/nerd-fonts/releases/download/v3.5.1/NerdFontsSymbolsOnly.zip
  URL_HASH SHA256=fdca3682534f6f65e1ccb2345b0362ccf67d9b8eca7c8025330946e93e2473bc
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nerd_symbols)
