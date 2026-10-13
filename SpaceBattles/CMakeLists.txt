cmake_minimum_required(VERSION 3.16)
set(IDF_TARGET esp32p4)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# ESP-IDF 6.0.2 installs its standalone-flash partition validation here.
# SpaceBattles is an AppFS image loaded by the existing Tanmatsu launcher;
# the generated factory partition table does not describe its allocation.
# Override this hook before project() creates the application build targets.
# Keep image generation/ELF validation intact and do not modify device partitions.
function(__idf_build_setup_post_build_validation)
    idf_build_get_property(build_dir BUILD_DIR)
    idf_build_get_property(project_bin PROJECT_BIN)
    add_custom_target(appfs_image_ready
        COMMAND ${CMAKE_COMMAND} -E echo
            "AppFS image: ${build_dir}/${project_bin} - install through the Tanmatsu launcher"
        DEPENDS gen_project_binary
        VERBATIM)
    add_dependencies(app appfs_image_ready)
endfunction()

# Keep the firmware metadata and About screen on the same approved release.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/components/spacebattles/release.h")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/components/spacebattles/release.h"
    SB_RELEASE_LINE REGEX "^#define SB_RELEASE_NUMBER [0-9]+$")
string(REGEX REPLACE "^#define SB_RELEASE_NUMBER " "" SB_RELEASE "${SB_RELEASE_LINE}")
project(spacebattles VERSION ${SB_RELEASE})
