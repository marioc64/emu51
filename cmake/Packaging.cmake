# SPDX-License-Identifier: GPL-2.0-only
include(GNUInstallDirs)

set(EMU51_CODESIGN_IDENTITY "" CACHE STRING "macOS signing identity; empty for unsigned, - for ad-hoc")
option(EMU51_BUNDLE_GLFW "Bundle a shared GLFW on Linux (requires its license file)" OFF)
set(EMU51_GLFW_LICENSE "" CACHE FILEPATH "License/copyright file for an external bundled GLFW runtime")
set(EMU51_BUNDLE_ID "org.emu51.app" CACHE STRING "macOS bundle identifier")

# Only explicitly listed runtime files enter binary packages. In particular,
# never install the development progs/ tree or generate source archives here.
install(TARGETS emu51cli emu51_bench RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${PROJECT_SOURCE_DIR}/README.md" "${PROJECT_SOURCE_DIR}/RELEASE.md"
    "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "${PROJECT_SOURCE_DIR}/HISTORY.md"
    "${PROJECT_SOURCE_DIR}/COPYRIGHT" "${PROJECT_SOURCE_DIR}/CONTRIBUTING.md"
    "${PROJECT_SOURCE_DIR}/CYCLE_ACCURATE_EXECUTION.md"
    "${PROJECT_SOURCE_DIR}/MEMORY_BUS.md" "${PROJECT_SOURCE_DIR}/MEMORY_BUS_PLAN.md"
    "${PROJECT_SOURCE_DIR}/emu51/asm/MODULES.md"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION "${CMAKE_INSTALL_DOCDIR}")

function(emu51_package_license source component)
    if(NOT IS_ABSOLUTE "${source}")
        set(source "${PROJECT_SOURCE_DIR}/${source}")
    endif()
    install(FILES "${source}"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/licenses/${component}")
    if(APPLE AND TARGET emu51)
        set_source_files_properties("${source}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources/licenses/${component}")
        target_sources(emu51 PRIVATE "${source}")
    endif()
endfunction()
emu51_package_license("third_party/imgui/LICENSE.txt" imgui)
emu51_package_license("third_party/ImGuiColorTextEdit/LICENSE" ImGuiColorTextEdit)
emu51_package_license("third_party/kason/LICENSE" kason)
emu51_package_license("third_party/codicons/LICENSE" codicons)
emu51_package_license("third_party/portable-file-dialogs.COPYING" portable-file-dialogs)
emu51_package_license("third_party/imgui/examples/libs/glfw/COPYING.txt" glfw)
emu51_package_license("peripheral-sdk/LICENSE" peripheral-sdk)
emu51_package_license("peripheral-sdk/COPYING" peripheral-sdk)
if(EMU51_PERIPHERAL_HOST_AUDIO)
    emu51_package_license("${EMU51_MINIAUDIO_DIR}/LICENSE" miniaudio)
endif()

if(APPLE AND TARGET emu51)
    set_target_properties(emu51 PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "emu51"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${EMU51_BUNDLE_ID}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set_source_files_properties("${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    target_sources(emu51 PRIVATE "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md")
    foreach(notice LICENSE COPYRIGHT)
        set_source_files_properties("${PROJECT_SOURCE_DIR}/${notice}" PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources)
        target_sources(emu51 PRIVATE "${PROJECT_SOURCE_DIR}/${notice}")
    endforeach()
endif()

if(WIN32)
    # Deploy the redistributable MSVC runtime; any non-system DLLs (including a
    # dynamically linked GLFW) are resolved from the built executables below.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
    include(InstallRequiredSystemLibraries)
    set(EMU51_RUNTIME_SEARCH_DIRS "")
    if(EMU51_GLFW_TARGET AND TARGET "${EMU51_GLFW_TARGET}")
        get_target_property(glfw_type "${EMU51_GLFW_TARGET}" TYPE)
        if(NOT glfw_type STREQUAL "INTERFACE_LIBRARY")
            set(EMU51_RUNTIME_SEARCH_DIRS "$<TARGET_FILE_DIR:${EMU51_GLFW_TARGET}>")
        endif()
    endif()
    set(runtime_tool_lines "")
    if(MSVC)
        get_filename_component(linker_dir "${CMAKE_LINKER}" DIRECTORY)
        find_program(EMU51_DUMPBIN NAMES dumpbin HINTS "${linker_dir}")
        if(EMU51_DUMPBIN)
            set(runtime_tool_lines "set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL dumpbin)\nset(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND \"${EMU51_DUMPBIN}\")")
        endif()
    endif()
    set(runtime_gui_line "")
    if(TARGET emu51)
        set(runtime_gui_line "list(APPEND runtime_executables \"$<TARGET_FILE:emu51>\")")
    endif()
    file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/deploy-windows-$<CONFIG>.cmake" CONTENT
"set(runtime_executables \"$<TARGET_FILE:emu51cli>\" \"$<TARGET_FILE:emu51_asm>\" \"$<TARGET_FILE:emu51_bench>\")
${runtime_gui_line}
set(runtime_search_dirs \"${EMU51_RUNTIME_SEARCH_DIRS}\")
set(runtime_bindir \"${CMAKE_INSTALL_BINDIR}\")
${runtime_tool_lines}
include(\"${PROJECT_SOURCE_DIR}/cmake/DeployWindows.cmake\")
")
    install(SCRIPT "${PROJECT_BINARY_DIR}/deploy-windows-$<CONFIG>.cmake")
elseif(UNIX AND NOT APPLE AND TARGET emu51 AND EMU51_BUNDLE_GLFW)
    # Bundle GLFW itself. The host supplies graphics drivers and their platform
    # libraries; carrying those across Linux distributions would be unreliable.
    get_target_property(glfw_type "${EMU51_GLFW_TARGET}" TYPE)
    if(glfw_type STREQUAL "SHARED_LIBRARY")
        if(NOT EXISTS "${EMU51_GLFW_LICENSE}")
            message(FATAL_ERROR "EMU51_BUNDLE_GLFW requires EMU51_GLFW_LICENSE for that runtime")
        endif()
        emu51_package_license("${EMU51_GLFW_LICENSE}" external-glfw)
        install(CODE "file(INSTALL \"$<TARGET_FILE:${EMU51_GLFW_TARGET}>\"
            DESTINATION \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}\"
            FOLLOW_SYMLINK_CHAIN)")
        install(CODE "file(INSTALL \"$<TARGET_SONAME_FILE:${EMU51_GLFW_TARGET}>\"
            DESTINATION \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}\"
            FOLLOW_SYMLINK_CHAIN)")
        set_target_properties(emu51 PROPERTIES
            INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    else()
        message(FATAL_ERROR "EMU51_BUNDLE_GLFW requires a CMake SHARED_LIBRARY target")
    endif()
endif()

# Signing must run after every resource/runtime file is installed and before
# checksums are generated. Never sign the development build in place.
configure_file("${PROJECT_SOURCE_DIR}/cmake/FinalizePackage.cmake.in"
    "${PROJECT_BINARY_DIR}/FinalizePackage.cmake" @ONLY)
install(SCRIPT "${PROJECT_BINARY_DIR}/FinalizePackage.cmake")

set(CPACK_PACKAGE_NAME emu51)
set(CPACK_PACKAGE_VENDOR emu51)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "8051 emulator, debugger and assembler")
set(package_arch "${CMAKE_SYSTEM_PROCESSOR}")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    string(REPLACE ";" "-" package_arch "${CMAKE_OSX_ARCHITECTURES}")
elseif(WIN32 AND CMAKE_VS_PLATFORM_NAME)
    set(package_arch "${CMAKE_VS_PLATFORM_NAME}")
endif()
set(CPACK_PACKAGE_FILE_NAME "emu51-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${package_arch}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
# Use git archive for source releases, excluding untracked local files.
set(CPACK_SOURCE_GENERATOR "")
foreach(format 7Z CYGWIN RPM TBZ2 TGZ TXZ TZ ZIP)
    set(CPACK_SOURCE_${format} OFF)
endforeach()
include(CPack)
# Guard even an explicit `cpack -G ... --config CPackSourceConfig.cmake`.
file(WRITE "${CPACK_SOURCE_OUTPUT_CONFIG_FILE}"
    "message(FATAL_ERROR \"Source packaging is disabled in CPack. Use git archive from this public repository.\")\n")

add_test(NAME emu51_installed_package
    COMMAND "${CMAKE_COMMAND}"
        "-DBUILD_DIR=${PROJECT_BINARY_DIR}"
        "-DCONFIG=$<CONFIG>"
        "-DWORK_DIR=${PROJECT_BINARY_DIR}/installed-package-test/$<CONFIG>"
        "-DBINDIR=${CMAKE_INSTALL_BINDIR}"
        "-DDOCDIR=${CMAKE_INSTALL_DOCDIR}"
        "-DDATADIR=${CMAKE_INSTALL_DATADIR}"
        "-DINCLUDEDIR=${CMAKE_INSTALL_INCLUDEDIR}"
        "-DHOST_AUDIO_EXPECTED=${EMU51_PERIPHERAL_HOST_AUDIO}"
        "-DGUI_EXPECTED=$<TARGET_EXISTS:emu51>"
        -P "${PROJECT_SOURCE_DIR}/cmake/VerifyPackage.cmake")
