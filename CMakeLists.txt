cmake_minimum_required(VERSION 3.24)
project(sailfish VERSION 0.1.0 LANGUAGES C CXX)

if(POLICY CMP0205)
  cmake_policy(SET CMP0205 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SAILFISH_BUILD_TESTS "Build the unit tests" ON)

include(cmake/dependencies.cmake)

function(sailfish_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

find_package(Threads REQUIRED)

# Pure logic: protocol, UI model, key translation. No SDL, no fonts, no OS.
add_library(sailfish_core STATIC
  src/msgpack/value.cpp
  src/msgpack/encoder.cpp
  src/msgpack/decoder.cpp
  src/rpc/channel.cpp
  src/log/log.cpp
  src/ui/highlight.cpp
  src/ui/grid.cpp
  src/ui/ui_state.cpp
  src/ui/redraw_dispatcher.cpp
  src/ui/events_global.cpp
  src/ui/events_grid.cpp
  src/ui/events_multigrid.cpp
  src/ui/events_widgets.cpp
  src/input/key_encoding.cpp
  src/text/utf8.cpp
  src/text/char_width.cpp
)
target_include_directories(sailfish_core PUBLIC src)
target_link_libraries(sailfish_core PUBLIC Threads::Threads)
sailfish_warnings(sailfish_core)

# OS seams: processes, paths, font discovery. One implementation per OS,
# selected here at build time.
add_library(sailfish_platform STATIC
  src/platform/child_process.cpp
  src/platform/files.cpp
  src/platform/paths.cpp
)
# Bundled resources (Lua runtime, fonts) are staged in the build tree exactly
# as an installed app lays them out; the runtime is linked, not copied, so
# Lua edits apply without rebuilding.
set(SAILFISH_STAGE "${CMAKE_CURRENT_BINARY_DIR}/resources")
file(MAKE_DIRECTORY "${SAILFISH_STAGE}/fonts")
file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/runtime" "${SAILFISH_STAGE}/runtime" SYMBOLIC COPY_ON_ERROR)
configure_file("${nerd_symbols_SOURCE_DIR}/SymbolsNerdFontMono-Regular.ttf" "${SAILFISH_STAGE}/fonts/SymbolsNerdFontMono-Regular.ttf" COPYONLY)
configure_file("${nerd_symbols_SOURCE_DIR}/LICENSE" "${SAILFISH_STAGE}/fonts/SymbolsNerdFont-LICENSE" COPYONLY)
target_compile_definitions(sailfish_platform PRIVATE SAILFISH_STAGED_RESOURCE_DIR="${SAILFISH_STAGE}")
if(WIN32)
  target_sources(sailfish_platform PRIVATE
    src/platform/child_process_win.cpp
    src/platform/text_encoding_win.cpp
    src/platform/font_locator_directwrite.cpp)
  target_link_libraries(sailfish_platform PRIVATE dwrite)
elseif(APPLE)
  target_sources(sailfish_platform PRIVATE
    src/platform/child_process_posix.cpp
    src/platform/font_locator_macos.cpp)
  target_link_libraries(sailfish_platform PRIVATE "-framework CoreText" "-framework CoreFoundation")
else()
  find_package(Fontconfig REQUIRED)
  target_sources(sailfish_platform PRIVATE
    src/platform/child_process_posix.cpp
    src/platform/font_locator_fontconfig.cpp)
  target_link_libraries(sailfish_platform PRIVATE Fontconfig::Fontconfig)
endif()
target_link_libraries(sailfish_platform PUBLIC sailfish_core)
sailfish_warnings(sailfish_platform)

# Host services that run beside Neovim: config history, background tasks.
add_library(sailfish_services STATIC
  src/services/task_runner.cpp
  src/services/config_history.cpp
)
target_link_libraries(sailfish_services PUBLIC sailfish_platform)
sailfish_warnings(sailfish_services)

# Pixels: fonts, glyph atlas, compositor and its layers.
add_library(sailfish_render STATIC
  src/render/font_spec.cpp
  src/render/font_face.cpp
  src/render/font_set.cpp
  src/render/glyph_cache.cpp
  src/render/quad_batch.cpp
  src/render/shape_cache.cpp
  src/render/grid_painter.cpp
  src/render/compositor.cpp
  src/render/grid_layer.cpp
  src/render/cursor_layer.cpp
  src/render/text_layout.cpp
  src/render/cmdline_layer.cpp
  src/render/message_layer.cpp
  src/render/popupmenu_layer.cpp
  src/render/toast_layer.cpp
)
target_link_libraries(sailfish_render PUBLIC sailfish_platform SDL3::SDL3 PRIVATE sailfish_freetype sailfish_harfbuzz)
sailfish_warnings(sailfish_render)

# Editors and their supervision: Neovim instances, the host API their Lua
# calls, and the supervisor that hot-swaps them. No window: unit-testable.
add_library(sailfish_editor STATIC
  src/nvim/instance.cpp
  src/host/host_api.cpp
  src/app/supervisor.cpp
)
target_link_libraries(sailfish_editor PUBLIC sailfish_services)
sailfish_warnings(sailfish_editor)

add_executable(sailfish WIN32
  src/app/main.cpp
  src/app/application.cpp
  src/app/keyboard.cpp
  src/app/window_state.cpp
  src/host/clipboard_service.cpp
)
target_link_libraries(sailfish PRIVATE sailfish_editor sailfish_render)
sailfish_warnings(sailfish)

if(SAILFISH_BUILD_TESTS)
  enable_testing()
  add_executable(sailfish_tests
    tests/test_main.cpp
    tests/test_msgpack.cpp
    tests/test_rpc.cpp
    tests/test_nvim_process.cpp
    tests/test_ui_state.cpp
    tests/test_key_encoding.cpp
    tests/test_config_history.cpp
    tests/test_supervisor.cpp
  )
  target_link_libraries(sailfish_tests PRIVATE sailfish_editor doctest::doctest)
  sailfish_warnings(sailfish_tests)
  add_test(NAME unit COMMAND sailfish_tests)
endif()
