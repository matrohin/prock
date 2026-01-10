cmake_minimum_required(VERSION 3.27)
project(prock VERSION 0.0.1 LANGUAGES CXX)

find_package(OpenGL REQUIRED)
find_package(X11 REQUIRED)

add_subdirectory(glfw-3.4)

add_executable(${PROJECT_NAME} src/main.cpp src/imgui_all.cpp)

target_link_libraries(${PROJECT_NAME} glfw OpenGL::GLES2 X11::X11)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/implot)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

