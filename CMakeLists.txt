cmake_minimum_required(VERSION 3.27)
project(prock VERSION 0.0.1 LANGUAGES CXX)

find_package(OpenGL REQUIRED)
find_package(X11 REQUIRED)
find_package(Freetype REQUIRED)

##############################################################################
# Common options
##############################################################################

set(CMAKE_CXX_STANDARD 17)

# Hardening. https://wiki.debian.org/Hardening
include(CheckPIESupported)
check_pie_supported()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_options(
  -fstack-protector-strong
  -fstack-clash-protection
)
add_compile_options($<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>)
add_link_options(-Wl,-z,relro,-z,now)

# No exceptions / RTTI scoped to C++
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>")

set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third-party/glfw-3.4)
add_subdirectory(third-party/tracy)

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

option(BUILD_TESTS "Build tests" ON)

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

##############################################################################
# Dear ImGui (no project warnings)
##############################################################################

set(IMGUI_TARGETS imgui_all)
if(BUILD_TESTS)
  list(APPEND IMGUI_TARGETS imgui_all_te)
endif()

foreach(target ${IMGUI_TARGETS})
  add_library(${target} STATIC src/imgui_all.cpp)
  target_include_directories(${target} PUBLIC
    third-party/imgui
    third-party/implot
  )
  target_compile_definitions(${target} PRIVATE "IMPLOT_CUSTOM_NUMERIC_TYPES=(double)")
  # PUBLIC so the main executable's TUs (which include imgui_internal.h) agree with
  # the library on the active font loader and stay ABI-consistent.
  target_compile_definitions(${target} PUBLIC IMGUI_ENABLE_FREETYPE)
  target_link_libraries(${target}
    PRIVATE
    glfw
    OpenGL::GLES2
    Freetype::Freetype
    project_sanitizers
  )
endforeach()

##############################################################################
# UI Dependencies (for prock and prock_ui_tests)
##############################################################################
add_library(project_ui_deps INTERFACE)
target_include_directories(project_ui_deps
  INTERFACE
  third-party/material-symbols
  third-party/inter
  third-party/jetbrains-mono
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(project_ui_deps
  INTERFACE
  glfw
  OpenGL::GLES2
  X11::X11
  tracy
)

##############################################################################
# Main Executable
##############################################################################
include(GNUInstallDirs)

add_executable(${PROJECT_NAME} src/main.cpp src/app.cpp)
install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
target_link_libraries(${PROJECT_NAME} PRIVATE
  imgui_all
  project_ui_deps
  project_warnings
  project_sanitizers
)

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


##############################################################################
# All Tests
##############################################################################
if(BUILD_TESTS)
  enable_testing()

  ############################################################################
  # Unit Tests
  ############################################################################
  add_executable(prock_tests
    tests/unit/test_main.cpp
    tests/unit/test_base.cpp
    tests/unit/test_views.cpp
    tests/unit/test_serialize.cpp
    src/base/base.cpp
    src/readers/proc_util.cpp
    src/readers/username.cpp
    src/views/brief_table_logic.cpp
    src/views/path_elide.cpp
    src/state/state.cpp
    src/state/serialize.cpp)
  target_include_directories(prock_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    third-party/imgui
    third-party/implot
    third-party/doctest
  )
  target_compile_options(prock_tests PRIVATE -fexceptions)
  target_link_libraries(prock_tests
    PRIVATE
    tracy
    project_warnings
    project_sanitizers
  )

  add_test(
    NAME prock_tests
    COMMAND prock_tests
  )

  ############################################################################
  # UI Tests
  ############################################################################
  target_include_directories(imgui_all_te PUBLIC third-party/imgui_test_engine)
  target_compile_definitions(imgui_all_te PUBLIC PROCK_UI_TESTS)
  target_sources(imgui_all_te PRIVATE
    third-party/imgui_test_engine/imgui_te_context.cpp
    third-party/imgui_test_engine/imgui_capture_tool.cpp
    third-party/imgui_test_engine/imgui_te_coroutine.cpp
    third-party/imgui_test_engine/imgui_te_engine.cpp
    third-party/imgui_test_engine/imgui_te_exporters.cpp
    third-party/imgui_test_engine/imgui_te_perftool.cpp
    third-party/imgui_test_engine/imgui_te_ui.cpp
    third-party/imgui_test_engine/imgui_te_utils.cpp
  )

  add_executable(prock_ui_tests
    tests/ui/main.cpp
    tests/ui/ui_test_replay.cpp
    tests/ui/ui_tests_brief_table.cpp
    tests/ui/ui_tests_replay_controls.cpp
    tests/common/fake_recording.cpp
    src/app.cpp
  )
  target_include_directories(prock_ui_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
  )
  target_link_libraries(prock_ui_tests
    PRIVATE
    imgui_all_te
    project_ui_deps
    project_warnings
    project_sanitizers
  )

  add_test(
    NAME prock_ui_tests
    COMMAND prock_ui_tests
  )
endif()

