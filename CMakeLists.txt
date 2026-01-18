cmake_minimum_required(VERSION 3.27)
project(prock VERSION 0.0.1 LANGUAGES CXX)

find_package(OpenGL REQUIRED)
find_package(X11 REQUIRED)

add_subdirectory(glfw-3.4)

add_executable(${PROJECT_NAME} src/main.cpp src/imgui_all.cpp)

target_link_libraries(${PROJECT_NAME} glfw OpenGL::GLES2 X11::X11)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/implot)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Test target (no GUI dependencies, but needs imgui headers for types)
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
  add_executable(prock_tests
    tests/test_main.cpp
    tests/test_views.cpp
    src/views/brief_table_logic.cpp)
  target_include_directories(prock_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
  target_compile_options(prock_tests PRIVATE -Wall)

  enable_testing()
  add_test(NAME prock_tests COMMAND prock_tests)
endif()

