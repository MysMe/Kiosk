cmake_minimum_required(VERSION 3.19)
set(PROJECT_NAME Kiosk)

project(${PROJECT_NAME})

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
#Make this project the default VS project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

add_executable(${PROJECT_NAME} "")
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

add_subdirectory(dependencies)
add_subdirectory(src)

target_link_libraries (${PROJECT_NAME} PRIVATE osmanip::osmanip)