cmake_minimum_required(VERSION 3.22)

project(juce_plugin_renderer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED JUCE_PATH)
    set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to the JUCE checkout")
    if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt")
        set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Path to the JUCE checkout" FORCE)
    endif()
endif()

if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at '${JUCE_PATH}'.\n"
        "Clone JUCE alongside this folder, for example:\n"
        "  git submodule add https://github.com/juce-framework/JUCE.git native/JUCE\n"
        "or configure with -DJUCE_PATH=/path/to/JUCE")
endif()

add_subdirectory("${JUCE_PATH}" JUCE EXCLUDE_FROM_ALL)
add_subdirectory(plugin_renderer)
add_subdirectory(plugin_host_app)
