cmake_minimum_required(VERSION 3.27)
project(prock VERSION 0.0.1 LANGUAGES CXX)

find_package(OpenGL REQUIRED)
find_package(X11 REQUIRED)

add_subdirectory(glfw-3.4)

# Common options:
add_library(project_warnings INTERFACE)
target_compile_options(project_warnings
  INTERFACE
    -Werror
    -Wall
    -Wextra # reasonable and standard
    -Wnon-virtual-dtor # warn the user if a class with virtual functions has a non-virtual destructor.
    -Wcast-align # warn for potential performance problem casts
    -Woverloaded-virtual # warn if you overload (not override) a virtual function
    -Wpedantic # warn if non-standard C++ is used
    -Wnull-dereference # warn if a null dereference is detected
)

# Main executable:
add_executable(${PROJECT_NAME} src/main.cpp src/imgui_all.cpp)
set_source_files_properties(src/imgui_all.cpp PROPERTIES SKIP_LINTING ON)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/imgui
  ${CMAKE_CURRENT_SOURCE_DIR}/implot
  ${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(${PROJECT_NAME} PRIVATE glfw OpenGL::GLES2 X11::X11 project_warnings)
set_target_properties(${PROJECT_NAME} PROPERTIES CMAKE_CXX_CLANG_TIDY clang-tidy)


# Test target:
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
  add_executable(prock_tests
    tests/test_main.cpp
    tests/test_views.cpp
    src/views/brief_table_logic.cpp
    src/state.cpp)
  target_include_directories(prock_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
  )
  target_link_libraries(${PROJECT_NAME} PRIVATE project_warnings)

  enable_testing()
  add_test(NAME prock_tests COMMAND prock_tests WORKING_DIRECTORY ${UNIT_TEST_BIN_OUTPUT_DIR})
endif()

