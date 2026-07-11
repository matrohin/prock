cmake_minimum_required(VERSION 3.27)
project(prock VERSION 0.0.1 LANGUAGES CXX)

set (CMAKE_CXX_STANDARD 17)

find_package(OpenGL REQUIRED)
find_package(X11 REQUIRED)
find_package(Freetype REQUIRED)

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
    -Wpedantic # warn if non-standard C++ is used
    -Wnull-dereference # warn if a null dereference is detected
)

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
target_link_libraries(${PROJECT_NAME} PRIVATE imgui_all glfw OpenGL::GLES2 X11::X11 tracy project_warnings)

# Our code never throws/catches and uses no RTTI, so drop the unwinding tables
# and typeinfo. Scoped to this target: not the vendored libs, and not the test
# target (doctest's REQUIRE throws).
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
    src/base/base.cpp
    src/sources/proc_parsers.cpp
    src/sources/username.cpp
    src/views/brief_table_logic.cpp
    src/state.cpp)
  target_include_directories(prock_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    third-party/imgui
  )
  target_link_libraries(prock_tests PRIVATE tracy project_warnings)

  enable_testing()
  add_test(NAME prock_tests COMMAND prock_tests WORKING_DIRECTORY ${UNIT_TEST_BIN_OUTPUT_DIR})
endif()

