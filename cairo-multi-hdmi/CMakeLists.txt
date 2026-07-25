cmake_minimum_required(VERSION 3.16)
project(cairo_multi_hdmi C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_GLES "Build GBM/EGL/OpenGL ES backend skeleton for Mali" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM REQUIRED libdrm)
pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)

set(APP_SOURCES
    src/main.c
    src/drm_output.c
    src/renderer.c
    src/font.c
    src/scene_digital.c
    src/scene_world.c
    src/scene_weather.c
    src/scene_dispatch.c
    src/time_util.c
    src/backend.c
    src/backend_cairo.c
)

if(ENABLE_GLES)
    pkg_check_modules(EGL REQUIRED egl)
    pkg_check_modules(GBM REQUIRED gbm)
    pkg_check_modules(GLESV2 REQUIRED glesv2)
    list(APPEND APP_SOURCES src/backend_gles.c)
endif()

add_executable(cairo-multi-hdmi ${APP_SOURCES})

target_include_directories(cairo-multi-hdmi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${DRM_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
    ${FONTCONFIG_INCLUDE_DIRS}
)

target_compile_options(cairo-multi-hdmi PRIVATE
    -Wall -Wextra -O2
    ${DRM_CFLAGS_OTHER}
    ${CAIRO_CFLAGS_OTHER}
)

target_link_libraries(cairo-multi-hdmi PRIVATE
    ${DRM_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    ${FONTCONFIG_LIBRARIES}
    m
    pthread
)

if(ENABLE_GLES)
    target_compile_definitions(cairo-multi-hdmi PRIVATE ENABLE_GLES=1)
    target_include_directories(cairo-multi-hdmi PRIVATE
        ${EGL_INCLUDE_DIRS}
        ${GBM_INCLUDE_DIRS}
        ${GLESV2_INCLUDE_DIRS}
    )
    target_link_libraries(cairo-multi-hdmi PRIVATE
        ${EGL_LIBRARIES}
        ${GBM_LIBRARIES}
        ${GLESV2_LIBRARIES}
    )
endif()

install(TARGETS cairo-multi-hdmi RUNTIME DESTINATION bin)

message(STATUS "ENABLE_GLES=${ENABLE_GLES}")
