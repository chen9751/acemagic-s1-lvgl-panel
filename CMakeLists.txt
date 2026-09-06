cmake_minimum_required(VERSION 3.12.4)
project(lvgl C CXX)

# Set the correct FreeRTOS port for your system (e.g., Posix for WSL)
set(FREERTOS_PORT GCC_POSIX CACHE STRING "Port for FreeRTOS on Posix environment")

set(
    LVGL_PRO_PROJECT_DIR
    ""
    CACHE PATH "Path to LVGL Pro Project folder"
)

option(USE_FREERTOS "Enable FreeRTOS" OFF)

if(USE_FREERTOS)
    message(STATUS "FreeRTOS is enabled")

    # FreeRTOS integration when USE_FREERTOS is enabled
    add_library(freertos_config INTERFACE)

    target_include_directories(
        freertos_config
        SYSTEM INTERFACE
        ${PROJECT_SOURCE_DIR}/config
    )

    target_compile_definitions(
        freertos_config
        INTERFACE
        projCOVERAGE_TEST=0
    )

    # Add FreeRTOS as a subdirectory
    add_subdirectory(FreeRTOS)

    # FreeRTOS-specific include directories
    include_directories(
        ${PROJECT_SOURCE_DIR}/FreeRTOS/include
    )

    include_directories(
        ${PROJECT_SOURCE_DIR}/FreeRTOS/portable/ThirdParty/GCC/Posix
    )

    include_directories(
        ${PROJECT_SOURCE_DIR}/config
    )

    # Add FreeRTOS sources
    file(
        GLOB FREERTOS_SOURCES
        "${PROJECT_SOURCE_DIR}/FreeRTOS/*.c"
        "${PROJECT_SOURCE_DIR}/FreeRTOS/portable/MemMang/heap_4.c"
        "${PROJECT_SOURCE_DIR}/FreeRTOS/portable/ThirdParty/GCC/Posix/*.c"
    )

else()

    message(STATUS "FreeRTOS is disabled")

    set(FREERTOS_SOURCES "")

endif()


# Main include files of the project
include_directories(
    ${PROJECT_SOURCE_DIR}/main/inc
)


# Define options for LVGL with default values
option(LV_USE_DRAW_SDL "Use SDL draw unit" OFF)
option(LV_USE_LIBPNG "Use libpng to decode PNG" OFF)
option(LV_USE_LIBJPEG_TURBO "Use libjpeg turbo to decode JPEG" OFF)
option(LV_USE_FFMPEG "Use libffmpeg to display video using lv_ffmpeg" OFF)
option(LV_USE_FREETYPE "Use freetype library" OFF)


# Set C and C++ standards
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# Set the output path for the executable
set(
    EXECUTABLE_OUTPUT_PATH
    ${PROJECT_SOURCE_DIR}/bin
)

set(
    WORKING_DIRECTORY
    ${PROJECT_SOURCE_DIR}
)


# ============================================================
# Required libraries
# ============================================================

find_package(SDL2 REQUIRED)
find_package(CURL REQUIRED)


# Add compile definitions based on selected options
add_compile_definitions(
    $<$<BOOL:${LV_USE_DRAW_SDL}>:LV_USE_DRAW_SDL=1>
)

add_compile_definitions(
    $<$<BOOL:${LV_USE_LIBPNG}>:LV_USE_LIBPNG=1>
)

add_compile_definitions(
    $<$<BOOL:${LV_USE_LIBJPEG_TURBO}>:LV_USE_LIBJPEG_TURBO=1>
)

add_compile_definitions(
    $<$<BOOL:${LV_USE_FFMPEG}>:LV_USE_FFMPEG=1>
)


# ============================================================
# LVGL
# ============================================================

add_subdirectory(lvgl)

# Do NOT add ${PROJECT_SOURCE_DIR} here as PUBLIC.
# It caused CMake INTERFACE_INCLUDE_DIRECTORIES errors.
target_include_directories(
    lvgl
    PUBLIC
    ${SDL2_INCLUDE_DIRS}
)


# ============================================================
# S1 application sources
# ============================================================

set(
    MAIN_SOURCES

    src/mouse_cursor_icon.c
    src/hal/hal.c

    ui/s1_ui.c
    ui/fonts/s1_home_info_font_14.c
    ui/fonts/s1_home_time_font_108.c
    ui/fonts/s1_ui_font_14.c
    ui/fonts/s1_led_font_12.c

    input/w1_input.c

    services/ha_client.c
    services/led_client.c
)


# ============================================================
# Libraries linked to main
# ============================================================

set(
    MAIN_LIBS

    lvgl
    lvgl::examples
    lvgl::demos
    lvgl::thorvg

    ${SDL2_LIBRARIES}

    CURL::libcurl
)


# Create main executable depending on FreeRTOS option
if(USE_FREERTOS)

    list(
        APPEND MAIN_SOURCES

        src/freertos_main.c
        src/freertos/freertos_posix_port.c

        ${FREERTOS_SOURCES}
    )

    list(
        APPEND MAIN_LIBS

        freertos_config
        freertos_kernel
    )

else()

    list(
        APPEND MAIN_SOURCES
        src/main.c
    )

endif()


if(NOT MSVC)

    list(
        APPEND MAIN_LIBS
        m
        pthread
    )

endif()


# Custom target to run executable
add_custom_target(
    run
    COMMAND ${EXECUTABLE_OUTPUT_PATH}/main
    DEPENDS main
)


# ============================================================
# SDL2_image
# ============================================================

if(LV_USE_DRAW_SDL)

    set(
        CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        "${CMAKE_SOURCE_DIR}/cmake"
    )

    find_package(
        SDL2_image
        REQUIRED
    )

    target_include_directories(
        lvgl
        PUBLIC
        ${SDL2_IMAGE_INCLUDE_DIRS}
    )

    list(
        APPEND MAIN_LIBS
        ${SDL2_IMAGE_LIBRARIES}
    )

endif()


# ============================================================
# PNG
# ============================================================

if(LV_USE_LIBPNG)

    find_package(
        PNG
        REQUIRED
    )

    target_include_directories(
        lvgl
        PUBLIC
        ${PNG_INCLUDE_DIRS}
    )

    list(
        APPEND MAIN_LIBS
        ${PNG_LIBRARIES}
    )

endif()


# ============================================================
# JPEG
# ============================================================

if(LV_USE_LIBJPEG_TURBO)

    find_package(
        JPEG
        REQUIRED
    )

    target_include_directories(
        lvgl
        PUBLIC
        ${JPEG_INCLUDE_DIRS}
    )

    list(
        APPEND MAIN_LIBS
        ${JPEG_LIBRARIES}
    )

endif()


# ============================================================
# FFmpeg
# ============================================================

if(LV_USE_FFMPEG)

    list(
        APPEND MAIN_LIBS
        avformat
        avcodec
        avutil
        swscale
        z
    )

endif()


# ============================================================
# FreeType
# ============================================================

if(LV_USE_FREETYPE)

    find_package(
        Freetype
        REQUIRED
    )

    target_include_directories(
        lvgl
        PUBLIC
        ${FREETYPE_INCLUDE_DIRS}
    )

    list(
        APPEND MAIN_LIBS
        ${FREETYPE_LIBRARIES}
    )

endif()


# ============================================================
# LVGL Pro project
# ============================================================

if(LVGL_PRO_PROJECT_DIR)

    if(EXISTS "${LVGL_PRO_PROJECT_DIR}/CMakeLists.txt")

        add_subdirectory(
            ${LVGL_PRO_PROJECT_DIR}
            ui
        )

        list(
            APPEND MAIN_LIBS
            lib-ui
        )

        target_compile_definitions(
            lib-ui
            PUBLIC
            LV_LVGL_H_INCLUDE_SIMPLE
        )

        target_include_directories(
            lib-ui
            PUBLIC
            ${LVGL_PRO_PROJECT_DIR}
            ${CMAKE_SOURCE_DIR}/lvgl
        )

        message(
            STATUS
            "LVGL Pro Project enabled from: ${LVGL_PRO_PROJECT_DIR}"
        )

    else()

        message(
            WARNING
            "LVGL_PRO_PROJECT_DIR specified but no CMakeLists.txt found at: ${LVGL_PRO_PROJECT_DIR}"
        )

    endif()

endif()


# ============================================================
# Main executable
# ============================================================

add_executable(
    main
    ${MAIN_SOURCES}
)

target_compile_definitions(
    main
    PRIVATE
    LV_CONF_INCLUDE_SIMPLE
)

target_link_libraries(
    main
    ${MAIN_LIBS}
)


# ============================================================
# Windows console
# ============================================================

if(WIN32)

    if(MSVC)

        target_link_options(
            main
            PRIVATE
            "/SUBSYSTEM:CONSOLE"
        )

    else()

        target_link_options(
            main
            PRIVATE
            "-mconsole"
        )

    endif()

endif()


# ============================================================
# Debug
# ============================================================

if(CMAKE_BUILD_TYPE STREQUAL "Debug")

    message(
        STATUS
        "Debug mode enabled"
    )

    target_compile_options(
        lvgl
        PRIVATE

        -pedantic-errors
        -Wall
        -Wclobbered
        -Wdeprecated
        -Wdouble-promotion
        -Wempty-body
        -Wextra
        -Wformat-security
        -Wmaybe-uninitialized
        -Wpointer-arith
        -Wmultichar
        -Wno-pedantic
        -Wreturn-type
        -Wshadow
        -Wshift-negative-value
        -Wsizeof-pointer-memaccess
        -Wtype-limits
        -Wundef
        -Wuninitialized
        -Wunreachable-code
        -Wfloat-conversion
        -Wstrict-aliasing
    )


    if(ASAN)

        message(
            STATUS
            "AddressSanitizer enabled"
        )

        set(
            CMAKE_CXX_FLAGS
            "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer"
        )

        set(
            CMAKE_C_FLAGS
            "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer"
        )

        set(
            CMAKE_EXE_LINKER_FLAGS
            "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address"
        )

        set(
            CMAKE_SHARED_LINKER_FLAGS
            "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address"
        )

    else()

        message(
            STATUS
            "AddressSanitizer disabled"
        )

    endif()

endif()
