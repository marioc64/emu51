# SPDX-License-Identifier: GPL-2.0-only
if(NOT TARGET emu51)
    return()
endif()

set(icon_dir "${PROJECT_SOURCE_DIR}/resources/icons")
if(APPLE)
    set_target_properties(emu51 PROPERTIES MACOSX_BUNDLE_ICON_FILE emu51.icns)
    set_source_files_properties("${icon_dir}/emu51.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(emu51 PRIVATE "${icon_dir}/emu51.icns")
else()
    # Embed the window icons so relocated executables need no image loader or asset path.
    set(header "// Generated from resources/icons/*.rgba by AppIcon.cmake.\n#pragma once\n")
    foreach(size IN ITEMS 32 64)
        set(rgba "${icon_dir}/emu51-${size}.rgba")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${rgba}")
        file(READ "${rgba}" pixels HEX)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])"
            "0x\\1,0x\\2,0x\\3,0x\\4,\n" pixels "${pixels}")
        string(APPEND header "static unsigned char emu51_icon_${size}[] = {${pixels}};\n")
    endforeach()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(header_path "${CMAKE_CURRENT_BINARY_DIR}/generated/emu51_app_icon.h")
    # configure_file avoids recompilation if only unrelated CMake settings changed.
    file(WRITE "${header_path}.in" "${header}")
    configure_file("${header_path}.in" "${header_path}" COPYONLY)
    target_include_directories(emu51 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

    if(WIN32)
        enable_language(RC)
        target_sources(emu51 PRIVATE "${PROJECT_SOURCE_DIR}/emu51/imgui/windows/emu51.rc")
        target_include_directories(emu51 PRIVATE "${icon_dir}")
        set_source_files_properties("${PROJECT_SOURCE_DIR}/emu51/imgui/windows/emu51.rc" PROPERTIES
            OBJECT_DEPENDS "${icon_dir}/emu51.ico")
    elseif(UNIX)
        install(FILES "${PROJECT_SOURCE_DIR}/resources/emu51.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
        foreach(size IN ITEMS 16 24 32 48 64 128 256 512 1024)
            install(FILES "${icon_dir}/emu51-${size}.png"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps"
                RENAME emu51.png)
        endforeach()
    endif()
endif()
