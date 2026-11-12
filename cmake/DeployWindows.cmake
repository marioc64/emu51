# SPDX-License-Identifier: GPL-2.0-only
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES ${runtime_executables}
    DIRECTORIES ${runtime_search_dirs}
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    PRE_EXCLUDE_REGEXES "[Aa][Pp][Ii]-[Mm][Ss]-.*" "[Ee][Xx][Tt]-[Mm][Ss]-.*"
    POST_EXCLUDE_REGEXES ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\].*")
if(unresolved)
    message(FATAL_ERROR "Unresolved package DLLs: ${unresolved}")
endif()
foreach(dll IN LISTS resolved)
    file(INSTALL "${dll}" DESTINATION "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${runtime_bindir}")
endforeach()
