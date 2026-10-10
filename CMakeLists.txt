# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET
#
# ShaderToMP4 : Windows (Direct2D), Linux et macOS (GLFW + Dear ImGui). L'exécutable est produit dans bin/.
cmake_minimum_required(VERSION 3.20)
project(ShaderToMP4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")  # runtime statique : rien à installer

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)

if(WIN32)
    file(GLOB GUI_SOURCES CONFIGURE_DEPENDS src/gui/*.cpp)
    add_executable(ShaderToMP4 WIN32 src/main.cpp ${CORE_SOURCES} ${GUI_SOURCES}
        src/platform/shared.cpp src/platform/windows/platform_win.cpp res/app.rc)
    target_include_directories(ShaderToMP4 PRIVATE src res)
    target_compile_options(ShaderToMP4 PRIVATE /utf-8 /MP /EHsc)
    target_link_options(ShaderToMP4 PRIVATE /MANIFEST:NO)
    target_link_libraries(ShaderToMP4 PRIVATE opengl32 gdi32 user32 comctl32 comdlg32 shell32)
else()
    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW ON)
    FetchContent_MakeAvailable(glfw)

    # Données embarquées dans l'exécutable (langues ; la police et les icônes de l'interface s'y ajoutent)
    set(EMBED_LIST "lang/en.json=lang/en.json;lang/fr.json=lang/fr.json")
    set(EMBED_FILES lang/en.json lang/fr.json)
    if(EXISTS ${CMAKE_SOURCE_DIR}/src/ui_imgui/CMakeLists.txt)
        include(src/ui_imgui/CMakeLists.txt)
    endif()
    if(NOT UI_SOURCES)
        add_compile_definitions(SHADERTOMP4_NO_UI)
    endif()
    set(EMBED_CPP ${CMAKE_BINARY_DIR}/embedded_data.cpp)
    list(TRANSFORM EMBED_FILES PREPEND ${CMAKE_SOURCE_DIR}/)
    add_custom_command(OUTPUT ${EMBED_CPP}
        COMMAND ${CMAKE_COMMAND} "-DOUT=${EMBED_CPP}" "-DROOT=${CMAKE_SOURCE_DIR}" "-DLIST=${EMBED_LIST}" -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
        DEPENDS ${EMBED_FILES} ${CMAKE_SOURCE_DIR}/cmake/embed.cmake VERBATIM)

    add_executable(ShaderToMP4 src/main_posix.cpp ${CORE_SOURCES}
        src/platform/shared.cpp src/platform/posix/platform_posix.cpp src/platform/posix/sha256.cpp ${EMBED_CPP} ${UI_SOURCES})
    target_include_directories(ShaderToMP4 PRIVATE src ${UI_INCLUDE_DIRS})
    find_package(Threads REQUIRED)
    target_link_libraries(ShaderToMP4 PRIVATE glfw ${UI_LIBRARIES} Threads::Threads ${CMAKE_DL_LIBS})
    if(APPLE)
        target_link_libraries(ShaderToMP4 PRIVATE "-framework CoreFoundation" "-framework OpenGL")
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(ShaderToMP4 PRIVATE OpenGL::GL)
        target_link_options(ShaderToMP4 PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    target_compile_options(ShaderToMP4 PRIVATE -Wall -Wno-unused-function)
endif()

set_target_properties(ShaderToMP4 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
