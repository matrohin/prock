cmake_minimum_required(VERSION 3.27)
project(prock VERSION 0.0.1 LANGUAGES CXX)

set (CMAKE_CXX_STANDARD 17)

find_package(OpenGL REQUIRED)
find_package(X11 REQUIRED)
find_package(Freetype REQUIRED)

# Hardening. https://wiki.debian.org/Hardening
include(CheckPIESupported)
check_pie_supported()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_options(-fstack-protector-strong -fstack-clash-protection)
add_compile_options($<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>)
add_link_options(-Wl,-z,relro,-z,now)

set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third-party/glfw-3.4)
add_subdirectory(third-party/tracy)

# Common options:
add_library(project_warnings INTERFACE)
target_compile_options(project_warnings
  INTERFACE
    -Wall
    -Wextra # reasonable and standard
    -Wnon-virtual-dtor # warn the user if a class with virtual functions has a non-virtual destructor.
    -Wcast-align # warn for potential performance problem casts
    -Woverloaded-virtual # warn if you overload (not override) a virtual function
    -Wnull-dereference # warn if a null dereference is detected
    -Wformat-security # warn on a non-literal format with no arguments
)

# Sanitizers:
option(PROCK_SANITIZE "Build with ASan (+ UBSan on first-party code)" OFF)
add_library(project_sanitizers INTERFACE)
if(PROCK_SANITIZE)
  # vptr: excluded because prock is built -fno-rtti (Clang errors on vptr+no-rtti).
  # float-cast-overflow: excluded because of double->int narrowing in stats/UI.
  # detect_leaks is turned off at runtime in base.cpp.
  target_compile_options(project_sanitizers INTERFACE
    -fsanitize=address,undefined
    -fno-sanitize=vptr,float-cast-overflow
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer)
  target_link_options(project_sanitizers INTERFACE -fsanitize=address,undefined)
  target_compile_definitions(project_sanitizers INTERFACE PROCK_SANITIZE)
endif()

# ImGui/ImPlot static library (no project warnings):
add_library(imgui_all STATIC src/imgui_all.cpp)
target_include_directories(imgui_all PRIVATE
  third-party/imgui
  third-party/implot
)
target_compile_definitions(imgui_all PRIVATE "IMPLOT_CUSTOM_NUMERIC_TYPES=(double)")
# PUBLIC so the main executable's TUs (which include imgui_internal.h) agree with
# the library on the active font loader and stay ABI-consistent.
target_compile_definitions(imgui_all PUBLIC IMGUI_ENABLE_FREETYPE)
target_link_libraries(imgui_all PRIVATE glfw OpenGL::GLES2 Freetype::Freetype)
target_compile_options(imgui_all PRIVATE -fno-exceptions -fno-rtti)
if(PROCK_SANITIZE)
  target_compile_options(imgui_all PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

# Main executable:
include(GNUInstallDirs)

add_executable(${PROJECT_NAME} src/main.cpp)
install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

target_include_directories(${PROJECT_NAME} PRIVATE
  third-party/imgui
  third-party/implot
  third-party/material-symbols
  third-party/inter
  third-party/jetbrains-mono
  ${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui_all glfw OpenGL::GLES2 X11::X11 tracy project_warnings project_sanitizers)

target_compile_options(${PROJECT_NAME} PRIVATE -fno-exceptions -fno-rtti)

# clang-tidy is opt-in per preset via PROCK_CLANG_TIDY (empty = off): on for the
# debug preset, off for release/publish, and scoped to this target (not vendored).
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_CLANG_TIDY "${PROCK_CLANG_TIDY}")

# Version string from git, embedded as a compile define. Captured at configure
# time; the CMAKE_CONFIGURE_DEPENDS below re-runs configure (and thus git
# describe) when HEAD/index change, so build-only flows stay current.
execute_process(
  COMMAND git describe --tags --always --dirty=-dirty
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE PROCK_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT PROCK_VERSION) # not a git checkout (e.g. release tarball)
  set(PROCK_VERSION "v${PROJECT_VERSION}")
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE PROCK_VERSION="${PROCK_VERSION}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/.git/HEAD ${CMAKE_SOURCE_DIR}/.git/index)


# Test target:
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
  add_executable(prock_tests
    tests/test_main.cpp
    tests/test_base.cpp
    tests/test_views.cpp
    tests/test_serialize.cpp
    src/base/base.cpp
    src/readers/proc_util.cpp
    src/readers/username.cpp
    src/views/brief_table_logic.cpp
    src/state/state.cpp
    src/state/serialize.cpp)
  target_include_directories(prock_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    third-party/imgui
    third-party/implot
  )
  target_link_libraries(prock_tests PRIVATE tracy project_warnings project_sanitizers)

  enable_testing()
  add_test(NAME prock_tests COMMAND prock_tests WORKING_DIRECTORY ${UNIT_TEST_BIN_OUTPUT_DIR})
endif()

