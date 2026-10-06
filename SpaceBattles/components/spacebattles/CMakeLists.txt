idf_component_register(
    SRCS "app/menu.c" "platform/settings.c" "services/preferences.c" "app/local.c" "app/local_run.c" "platform/turn_led.c" "game/rules.c" "game/board.c" "game/placement.c" "game/combat.c"
         "game/ai.c" "game/classic.c" "game/escalated.c" "game/match.c"
         "services/config.c" "services/storage.c" "app/application.c" "platform/tanmatsu.c" "platform/sdcard.c" "input/input.c" "render/renderer.c" "render/typography.c" "render/font_assets.c" "render/ui.c" "validation/faults.c" "validation/performance.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES badge-bsp pax-gfx esp_lcd fatfs sdmmc esp_driver_sdmmc esp_driver_gpio esp_app_format esp_timer esp_driver_jpeg
    EMBED_FILES "assets/loading.jpg" "assets/logoff.jpg"
)

set(SB_VALIDATION_FAULT "none" CACHE STRING "M0a fault: none, i2c, display, audio, transfer")
if(NOT SB_VALIDATION_FAULT MATCHES "^(none|i2c|display|audio|transfer)$")
    message(FATAL_ERROR "Unknown SB_VALIDATION_FAULT")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE SB_VALIDATION_NAME="${SB_VALIDATION_FAULT}")
if(NOT SB_VALIDATION_FAULT STREQUAL "none")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SB_FAULT_${SB_VALIDATION_FAULT}=1)
endif()
if(SB_VALIDATION_FAULT STREQUAL "i2c")
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=bsp_i2c_primary_bus_initialize")
elseif(SB_VALIDATION_FAULT STREQUAL "display")
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=bsp_display_initialize")
elseif(SB_VALIDATION_FAULT STREQUAL "audio")
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=bsp_audio_initialize")
endif()

option(SB_PERFORMANCE "Run bounded M7 benchmark before interactive preview" OFF)
set(SB_COLOR_BITS "16" CACHE STRING "Display comparison: 16 or 24 bits")
if(NOT SB_COLOR_BITS MATCHES "^(16|24)$")
    message(FATAL_ERROR "SB_COLOR_BITS must be 16 or 24")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE SB_PERFORMANCE=$<BOOL:${SB_PERFORMANCE}> SB_COLOR_BITS=${SB_COLOR_BITS})

option(SB_LOCAL_GAME "Open M8 local game instead of M6 preview" ON)
target_compile_definitions(${COMPONENT_LIB} PRIVATE SB_LOCAL_GAME=$<BOOL:${SB_LOCAL_GAME}>)
