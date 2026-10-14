cmake_minimum_required(VERSION 3.16)
if(NOT TANMATSU_PLUGIN_SDK)
  set(TANMATSU_PLUGIN_SDK "$ENV{TANMATSU_PLUGIN_SDK}")
endif()
if(NOT EXISTS "${TANMATSU_PLUGIN_SDK}/toolchain-plugin.cmake")
  message(FATAL_ERROR "Set TANMATSU_PLUGIN_SDK to the launcher tools/plugin-sdk directory")
endif()
set(CMAKE_TOOLCHAIN_FILE "${TANMATSU_PLUGIN_SDK}/toolchain-plugin.cmake" CACHE FILEPATH "")
project(eyes C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
include("${TANMATSU_PLUGIN_SDK}/plugin-build.cmake")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# The SDK helper omits libm and permits unresolved symbols in shared objects.
# Own the link command locally so every import has a declared runtime library.
add_library(eyes_obj OBJECT src/plugin.c src/widget_display.c src/eyes_engine.c)
target_include_directories(eyes_obj PRIVATE include ${PLUGIN_API_INCLUDE_DIRS})
target_compile_options(eyes_obj PRIVATE -Wall -Wextra -Werror -Os -fPIC
  -ffunction-sections -fdata-sections -fno-common)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/eyes.plugin"
  COMMAND ${CMAKE_C_COMPILER} -shared -nostdlib -Wl,--gc-sections
    -Wl,--no-undefined -Wl,--no-as-needed
    -Wl,-T,${TANMATSU_PLUGIN_SDK}/plugin.ld
    -Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/eyes.map
    -L${BADGE_ELF_FAKELIB_DIR} $<TARGET_OBJECTS:eyes_obj>
    -lbadge -lpax-gfx -lpax-codecs -lpthread -lm -lc -lgcc
    -o "${CMAKE_CURRENT_BINARY_DIR}/eyes.plugin"
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/audit_imports.py"
    "${CMAKE_CURRENT_BINARY_DIR}/eyes.plugin" "${TANMATSU_PLUGIN_SDK}/../.."
  DEPENDS eyes_obj $<TARGET_OBJECTS:eyes_obj>
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/audit_imports.py"
    "${TANMATSU_PLUGIN_SDK}/plugin.ld" "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
  COMMAND_EXPAND_LISTS
  VERBATIM)
add_custom_target(eyes ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/eyes.plugin")
configure_file(plugin.json plugin.json COPYONLY)
