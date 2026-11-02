cmake_minimum_required(VERSION 3.16)
project(vr_bootanimation_preview CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(WAYLAND_EGL    REQUIRED wayland-egl)
pkg_check_modules(EGL            REQUIRED egl)
pkg_check_modules(GLESV2         REQUIRED glesv2)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_get_variable(WP_DIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN})

function(wl_protocol name xml)
  add_custom_command(
    OUTPUT ${GEN}/${name}-client-protocol.h ${GEN}/${name}-protocol.c
    COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${GEN}/${name}-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} private-code  ${xml} ${GEN}/${name}-protocol.c
    DEPENDS ${xml})
endfunction()

wl_protocol(xdg-shell ${WP_DIR}/stable/xdg-shell/xdg-shell.xml)
set(PROTO_SRC ${GEN}/xdg-shell-protocol.c)

set(DECO_XML ${WP_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml)
if(EXISTS ${DECO_XML})
  wl_protocol(xdg-decoration-unstable-v1 ${DECO_XML})
  list(APPEND PROTO_SRC ${GEN}/xdg-decoration-unstable-v1-protocol.c)
  set(HAVE_DECO ON)
endif()

add_executable(vr_bootanimation
  src/main.cpp src/common.cpp src/renderer.cpp src/logo_animator.cpp src/gltf_impl.cpp
  src/platform_wayland.cpp src/platform_headless.cpp ${PROTO_SRC})
target_include_directories(vr_bootanimation PRIVATE src third_party ${GEN}
  ${WAYLAND_CLIENT_INCLUDE_DIRS} ${EGL_INCLUDE_DIRS} ${GLESV2_INCLUDE_DIRS})
target_link_libraries(vr_bootanimation PRIVATE
  ${WAYLAND_CLIENT_LIBRARIES} ${WAYLAND_EGL_LIBRARIES} ${EGL_LIBRARIES} ${GLESV2_LIBRARIES})
if(HAVE_DECO)
  target_compile_definitions(vr_bootanimation PRIVATE HAVE_XDG_DECORATION)
endif()
target_compile_definitions(vr_bootanimation PRIVATE TINYGLTF_NO_STB_IMAGE_WRITE)
target_compile_options(vr_bootanimation PRIVATE -Wall -Wno-unused-function)
# vendored single-header libs are noisy
set_source_files_properties(src/gltf_impl.cpp PROPERTIES COMPILE_FLAGS "-w")
install(TARGETS vr_bootanimation RUNTIME DESTINATION bin)
