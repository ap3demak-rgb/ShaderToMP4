# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET
#
# Interface Linux / macOS : GLFW + Dear ImGui + NanoSVG (téléchargés à la configuration, versions épinglées).
# Inclus par le CMakeLists.txt racine ; définit UI_SOURCES, UI_LIBRARIES et complète EMBED_LIST / EMBED_FILES.
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.9 GIT_SHALLOW ON
    SOURCE_SUBDIR no-cmake)
FetchContent_Declare(nanosvg GIT_REPOSITORY https://github.com/memononen/nanosvg.git
    GIT_TAG 239e102ec2c691f2902e20ace2ed36ee4a35cfe6 SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(imgui nanosvg)

set(UI_SOURCES
    src/ui_imgui/gui_main.cpp
    src/ui_imgui/model.cpp
    src/ui_imgui/widgets.cpp
    src/ui_imgui/file_browser.cpp
    src/ui_imgui/icons.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
if(APPLE)
    list(APPEND UI_SOURCES src/ui_imgui/dock_mac.mm)
else()
    list(APPEND UI_SOURCES src/ui_imgui/dock.cpp)
endif()
set(UI_INCLUDE_DIRS ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${nanosvg_SOURCE_DIR}/src)
set(UI_LIBRARIES)

# Police et icônes embarquées dans l'exécutable
file(GLOB UI_ICONS RELATIVE ${CMAKE_SOURCE_DIR} CONFIGURE_DEPENDS res/icons/*.svg)
foreach(f IN LISTS UI_ICONS)
    get_filename_component(n ${f} NAME)
    string(APPEND EMBED_LIST ";icons/${n}=${f}")
    list(APPEND EMBED_FILES ${f})
endforeach()
string(APPEND EMBED_LIST ";fonts/Inter-Regular.ttf=assets/fonts/Inter-Regular.ttf;fonts/Inter-SemiBold.ttf=assets/fonts/Inter-SemiBold.ttf")
list(APPEND EMBED_FILES assets/fonts/Inter-Regular.ttf assets/fonts/Inter-SemiBold.ttf)
