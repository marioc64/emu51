# SPDX-License-Identifier: GPL-2.0-only
if (NOT EXISTS "${EXECUTABLE}" OR NOT EXISTS "${RESOURCE}")
    message(FATAL_ERROR "Build emu51_asm and its packaged mod851 resource before running this test")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/input" "${WORK_DIR}/cwd" "${WORK_DIR}/standalone")
get_filename_component(executable_name "${EXECUTABLE}" NAME)
file(COPY "${EXECUTABLE}" DESTINATION "${WORK_DIR}/standalone")
set(source "${WORK_DIR}/input/program.asm")
file(WRITE "${source}" "$mod851\norg 0\nmov P1,#055h\nsetb TR0\nclr EA\nmov A,ACC\nmov ECNTRL,#1\nend\n")

function(check_assembly executable expect_success)
    set(output "${WORK_DIR}/output.bin")
    file(REMOVE "${output}")
    execute_process(
        COMMAND "${executable}" "${source}" -o "${output}" ${ARGN}
        WORKING_DIRECTORY "${WORK_DIR}/cwd"
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr
    )
    if (expect_success)
        if (NOT result EQUAL 0)
            message(FATAL_ERROR "Relocated assembler failed: ${stderr}")
        endif()
        file(READ "${output}" bytes HEX)
        if (NOT bytes STREQUAL "759055d28cc2afe5e075f601")
            message(FATAL_ERROR "Packaged mod851 produced incorrect bytes: ${bytes}")
        endif()
    else()
        if (result EQUAL 0 OR NOT stderr MATCHES "unable to resolve include 'mod851'")
            message(FATAL_ERROR "Missing packaged resource did not report the expected diagnostic: ${stderr}")
        endif()
    endif()
endfunction()

# A copied executable alone must not silently fall back to the build checkout.
set(standalone "${WORK_DIR}/standalone/${executable_name}")
check_assembly("${standalone}" FALSE)
file(COPY "${RESOURCE}" DESTINATION "${WORK_DIR}/standalone/resources/asm")
check_assembly("${standalone}" TRUE)

# The generic $INCLUDE form uses the same packaged resource resolution.
file(READ "${source}" text)
string(REPLACE "$mod851" "$INCLUDE(mod851)" text "${text}")
file(WRITE "${source}" "${text}")
check_assembly("${standalone}" TRUE)

# Model the installed bin/ + share/ tree and then relocate that entire tree.
file(MAKE_DIRECTORY "${WORK_DIR}/install-tree/bin")
file(COPY "${EXECUTABLE}" DESTINATION "${WORK_DIR}/install-tree/bin")
get_filename_component(installed_resource
    "${WORK_DIR}/install-tree/bin/${INSTALLED_ASM_FROM_BINDIR}" ABSOLUTE)
string(FIND "${installed_resource}" "${WORK_DIR}/" inside_work_dir)
if (inside_work_dir EQUAL 0)
    file(COPY "${RESOURCE}" DESTINATION "${installed_resource}")
    file(RENAME "${WORK_DIR}/install-tree" "${WORK_DIR}/relocated-install")
    check_assembly("${WORK_DIR}/relocated-install/bin/${executable_name}" TRUE)
endif()

if (CMAKE_HOST_APPLE)
    file(MAKE_DIRECTORY "${WORK_DIR}/emu51.app/Contents/MacOS")
    file(COPY "${EXECUTABLE}" DESTINATION "${WORK_DIR}/emu51.app/Contents/MacOS")
    file(COPY "${RESOURCE}" DESTINATION "${WORK_DIR}/emu51.app/Contents/Resources/asm")
    check_assembly("${WORK_DIR}/emu51.app/Contents/MacOS/${executable_name}" TRUE)
endif()

# Explicit include directories remain usable when no packaged resource exists.
file(REMOVE "${WORK_DIR}/standalone/resources/asm/mod851")
file(COPY "${RESOURCE}" DESTINATION "${WORK_DIR}/external")
check_assembly("${standalone}" TRUE -I "${WORK_DIR}/external")

# Board-specific includes are not built-ins. Even on a development checkout,
# package execution must not discover them through an implicit demo-directory path.
foreach(board_include bios.inc const.inc)
    file(WRITE "${source}" "$INCLUDE(${board_include})\nend\n")
    execute_process(
        COMMAND "${standalone}" "${source}" -o "${WORK_DIR}/board.bin"
        WORKING_DIRECTORY "${WORK_DIR}/cwd"
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr
    )
    string(FIND "${stderr}" "unable to resolve include '${board_include}'" diagnostic)
    if (result EQUAL 0 OR diagnostic EQUAL -1)
        message(FATAL_ERROR "A private board include was implicitly available: ${board_include}")
    endif()
endforeach()
