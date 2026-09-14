# Exercise real LVGL widgets and input dispatch without SDL, Linux devices or HA.
set(CMAKE_C_STANDARD 99)
file(READ "${PROJECT_SOURCE_DIR}/lv_conf.h" TEST_LV_CONF)
string(REPLACE "#define LV_USE_SDL 1" "#define LV_USE_SDL 0" TEST_LV_CONF "${TEST_LV_CONF}")
string(REPLACE "#define LV_BUILD_EXAMPLES 1" "#define LV_BUILD_EXAMPLES 0" TEST_LV_CONF "${TEST_LV_CONF}")
string(REPLACE "#define LV_BUILD_DEMOS 1" "#define LV_BUILD_DEMOS 0" TEST_LV_CONF "${TEST_LV_CONF}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/lv_conf.h" "${TEST_LV_CONF}")
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_BINARY_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${PROJECT_SOURCE_DIR}/lvgl" "${CMAKE_CURRENT_BINARY_DIR}/lvgl")
add_executable(s1_ui_tests test_ui.c
    ../ui/ui_second_pass.c ../ui/pages/ui_music_v2.c
    ../ui/fonts/s1_ha_font_22.c ../ui/pages/ui_home_v3.c
    ../ui/s1_ui.c ../ui/ui_router.c ../ui/pages/ui_page.c ../ui/pages/ui_light.c
    ../ui/fonts/s1_home_info_font_14.c ../ui/fonts/s1_home_time_font_108.c
    ../ui/fonts/s1_ui_font_14.c ../ui/fonts/s1_led_font_12.c
    ../ui/pages/ui_home_overlay.c ../ui/fonts/s1_nunito_extrabold_108.c)
target_include_directories(s1_ui_tests PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(s1_ui_tests PRIVATE lvgl m)
# Keep assertions active in Release builds too.
target_compile_options(s1_ui_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME s1_ui_interaction COMMAND s1_ui_tests)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_library(s1_bluez_test SHARED ../services/bluez_media_client.c)
target_compile_options(s1_bluez_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME s1_bluez_control COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_bluez.py" "$<TARGET_FILE:s1_bluez_test>")
