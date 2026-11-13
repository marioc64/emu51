# SPDX-License-Identifier: GPL-2.0-only
# emu51 build rules.
cmake_minimum_required(VERSION 3.16)
include(GNUInstallDirs)

set(EMU51_CORE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/core")
set(EMU51_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
set(EMU51_ASM_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/asm")
include("${PROJECT_SOURCE_DIR}/cmake/AssemblerModules.cmake")
set(EMU51_ASM_MODULE_FILES "")
foreach(module IN LISTS EMU51_ASM_MODULE_NAMES)
	list(APPEND EMU51_ASM_MODULE_FILES "${EMU51_ASM_DIR}/${module}")
endforeach()

add_library(emu51_editor_files_lib "io/emu51_editor_files.cpp")
target_include_directories(emu51_editor_files_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/io")
target_link_libraries(emu51_editor_files_lib PRIVATE emu51_warnings)

add_library(emu51_kason STATIC
	"${CMAKE_SOURCE_DIR}/third_party/kason/kason.c"
	"${CMAKE_SOURCE_DIR}/third_party/kason/kason_schema.c"
)
set_target_properties(emu51_kason PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED TRUE C_EXTENSIONS FALSE)
target_include_directories(emu51_kason SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/third_party/kason")

add_library(emu51_settings_lib "imgui/emu51_execution_settings.cpp")
target_include_directories(emu51_settings_lib PUBLIC "${EMU51_IMGUI_DIR}")
target_link_libraries(emu51_settings_lib PRIVATE emu51_kason emu51_warnings)

set(EMU51_CORE_SOURCES
	"core/emu51.cpp"
	"core/emu51_opcodes.cpp"
	"core/opcode_metadata.cpp"
)

add_library(emu51_core_lib ${EMU51_CORE_SOURCES})
target_include_directories(emu51_core_lib PUBLIC "${EMU51_CORE_DIR}")
target_link_libraries(emu51_core_lib PRIVATE emu51_warnings)

set(EMU51_ASM_SOURCES
	"asm/assembler.cpp"
	"asm/assembler_resources.cpp"
	"asm/program_loader.cpp"
)

add_library(emu51_asm_lib ${EMU51_ASM_SOURCES})
target_include_directories(emu51_asm_lib PUBLIC "${EMU51_ASM_DIR}")
target_include_directories(emu51_asm_lib PRIVATE "${EMU51_CORE_DIR}")
file(RELATIVE_PATH EMU51_INSTALLED_ASM_FROM_BINDIR
	"${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_DATADIR}/emu51/asm")
target_compile_definitions(emu51_asm_lib PRIVATE
	EMU51_INSTALLED_ASM_FROM_BINDIR="${EMU51_INSTALLED_ASM_FROM_BINDIR}"
)
target_link_libraries(emu51_asm_lib PUBLIC emu51_core_lib PRIVATE emu51_warnings)

add_executable(emu51_asm "asm/main.cpp")
target_link_libraries(emu51_asm PRIVATE emu51_asm_lib emu51_warnings)
target_include_directories(emu51_asm PRIVATE "${EMU51_ASM_DIR}")

# Standalone build targets (CLI, tests, and non-bundle GUI) share an output
# directory. Generate a resource copy for every build configuration, and
# refresh it on source changes without needing to relink an executable.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${EMU51_ASM_MODULE_FILES})
foreach(module IN LISTS EMU51_ASM_MODULE_NAMES)
	file(GENERATE
		OUTPUT "$<TARGET_FILE_DIR:emu51_asm>/resources/asm/${module}"
		INPUT "${EMU51_ASM_DIR}/${module}"
	)
endforeach()
install(TARGETS emu51_asm RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES ${EMU51_ASM_MODULE_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/emu51/asm")

add_executable(emu51       "main.cpp")
add_executable(emu51_tests "tests.cpp")
add_executable(emu51_bench "bench.cpp")

target_link_libraries(emu51       PRIVATE emu51_asm_lib emu51_warnings)
target_link_libraries(emu51_tests PRIVATE emu51_asm_lib emu51_settings_lib emu51_editor_files_lib emu51_warnings)
target_link_libraries(emu51_bench PRIVATE emu51_asm_lib emu51_warnings)
target_include_directories(emu51       PRIVATE "${EMU51_CORE_DIR}")
target_include_directories(emu51_tests PRIVATE "${EMU51_CORE_DIR}" "${EMU51_ASM_DIR}")
target_compile_definitions(emu51_tests PRIVATE
	EMU51_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_include_directories(emu51_bench PRIVATE "${EMU51_CORE_DIR}")
add_test(NAME emu51_tests COMMAND emu51_tests)

if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.h")
	# Vendored ImGui + ImGuiColorTextEdit: built without our strict warnings.
	add_library(emu51_thirdparty_imgui OBJECT
		"${CMAKE_SOURCE_DIR}/third_party/ImGuiColorTextEdit/TextEditor.cpp"
		"${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.cpp"
		"${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp"
		"${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp"
		"${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp"
	)
	target_include_directories(emu51_thirdparty_imgui SYSTEM PRIVATE
		"${CMAKE_SOURCE_DIR}/third_party/imgui"
		"${CMAKE_SOURCE_DIR}/third_party/imgui/backends"
		"${CMAKE_SOURCE_DIR}/third_party/ImGuiColorTextEdit"
	)
	target_compile_definitions(emu51_thirdparty_imgui PRIVATE
		"$<$<PLATFORM_ID:Windows>:NOMINMAX;WIN32_LEAN_AND_MEAN>"
	)

	# Our own ImGui-layer sources: get the strict warning set. Third-party
	# include directories are marked SYSTEM so warnings from their headers
	# don't fire inside our translation units.
	add_library(emu51_imgui_common OBJECT
		"imgui/emu51_imgui_actions.cpp"
		"imgui/emu51_imgui_app.cpp"
		"imgui/emu51_imgui_asm_editor.cpp"
		"imgui/emu51_imgui_codicons_data.cpp"
		"imgui/emu51_imgui_dialogs.cpp"
		"imgui/emu51_imgui_debugger.cpp"
		"imgui/emu51_imgui_draw.cpp"
		"imgui/emu51_imgui_icons.cpp"
		"imgui/emu51_imgui_layout.cpp"
		"imgui/emu51_imgui_menu.cpp"
		"imgui/emu51_imgui_monitor.cpp"
		"imgui/emu51_imgui_paths.cpp"
		"imgui/emu51_imgui_seg.cpp"
	)
	target_include_directories(emu51_imgui_common SYSTEM PRIVATE
		"${CMAKE_SOURCE_DIR}/third_party/imgui"
		"${CMAKE_SOURCE_DIR}/third_party/imgui/backends"
		"${CMAKE_SOURCE_DIR}/third_party/ImGuiColorTextEdit"
		"${CMAKE_SOURCE_DIR}/third_party"
	)
	target_include_directories(emu51_imgui_common PRIVATE
		"${EMU51_CORE_DIR}"
		"${EMU51_ASM_DIR}"
		"${EMU51_IMGUI_DIR}"
	)
	target_compile_definitions(emu51_imgui_common PRIVATE
		"$<$<PLATFORM_ID:Windows>:NOMINMAX;WIN32_LEAN_AND_MEAN>"
	)
	target_link_libraries(emu51_imgui_common PRIVATE emu51_editor_files_lib emu51_warnings)

	# Exercise application workflows without a native window or graphics backend.
	add_executable(emu51_ui_tests "ui_tests.cpp"
		$<TARGET_OBJECTS:emu51_imgui_common>
		$<TARGET_OBJECTS:emu51_thirdparty_imgui>
	)
	target_include_directories(emu51_ui_tests PRIVATE "${EMU51_CORE_DIR}" "${EMU51_IMGUI_DIR}")
	target_compile_definitions(emu51_ui_tests PRIVATE
		EMU51_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
	)
	target_include_directories(emu51_ui_tests SYSTEM PRIVATE
		"${CMAKE_SOURCE_DIR}/third_party/imgui"
		"${CMAKE_SOURCE_DIR}/third_party/ImGuiColorTextEdit"
	)
	target_link_libraries(emu51_ui_tests PRIVATE
		emu51_asm_lib emu51_settings_lib emu51_editor_files_lib emu51_warnings)
	add_test(NAME emu51_ui_tests COMMAND emu51_ui_tests)
endif()

if (APPLE AND TARGET emu51_imgui_common)
	find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
	find_library(GAMECONTROLLER_FRAMEWORK GameController REQUIRED)
	find_library(METAL_FRAMEWORK Metal REQUIRED)
	find_library(METALKIT_FRAMEWORK MetalKit REQUIRED)
	find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)

	add_library(emu51_thirdparty_backends OBJECT
		"${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_metal.mm"
		"${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_osx.mm"
	)
	target_include_directories(emu51_thirdparty_backends SYSTEM PRIVATE
		"${CMAKE_SOURCE_DIR}/third_party/imgui"
		"${CMAKE_SOURCE_DIR}/third_party/imgui/backends"
	)
	target_compile_options(emu51_thirdparty_backends PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")

	add_executable(emu51_imgui
		$<TARGET_OBJECTS:emu51_imgui_common>
		$<TARGET_OBJECTS:emu51_thirdparty_imgui>
		$<TARGET_OBJECTS:emu51_thirdparty_backends>
		"imgui/macos/emu51_imgui_main.mm"
	)

	target_include_directories(emu51_imgui SYSTEM PRIVATE
		"${CMAKE_SOURCE_DIR}/third_party/imgui"
		"${CMAKE_SOURCE_DIR}/third_party/imgui/backends"
		"${CMAKE_SOURCE_DIR}/third_party/ImGuiColorTextEdit"
	)
	target_include_directories(emu51_imgui PRIVATE
		"${EMU51_CORE_DIR}"
		"${EMU51_ASM_DIR}"
		"${EMU51_IMGUI_DIR}"
	)
	target_compile_options(emu51_imgui PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
	target_link_libraries(emu51_imgui PRIVATE
		emu51_asm_lib
		emu51_settings_lib
		emu51_editor_files_lib
		emu51_warnings
		${COCOA_FRAMEWORK}
		${GAMECONTROLLER_FRAMEWORK}
		${METAL_FRAMEWORK}
		${METALKIT_FRAMEWORK}
		${QUARTZCORE_FRAMEWORK}
	)
	set_target_properties(emu51_imgui PROPERTIES
		MACOSX_BUNDLE TRUE
		XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
	)
	set_source_files_properties(${EMU51_ASM_MODULE_FILES} PROPERTIES
		MACOSX_PACKAGE_LOCATION "Resources/asm")
	target_sources(emu51_imgui PRIVATE ${EMU51_ASM_MODULE_FILES})
elseif((WIN32 OR UNIX) AND NOT APPLE AND TARGET emu51_imgui_common)
	find_package(OpenGL REQUIRED)
	find_package(glfw3 CONFIG QUIET)

	set(EMU51_GLFW_TARGET "")
	if (TARGET glfw)
		set(EMU51_GLFW_TARGET glfw)
	elseif(TARGET glfw3::glfw)
		set(EMU51_GLFW_TARGET glfw3::glfw)
	else()
		find_package(PkgConfig QUIET)
		if (PkgConfig_FOUND)
			pkg_check_modules(GLFW3 QUIET IMPORTED_TARGET glfw3)
			if (TARGET PkgConfig::GLFW3)
				set(EMU51_GLFW_TARGET PkgConfig::GLFW3)
			endif()
		endif()
	endif()

	if (NOT EMU51_GLFW_TARGET AND WIN32)
		set(EMU51_BUNDLED_GLFW_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui/examples/libs/glfw")
		if (CMAKE_SIZEOF_VOID_P EQUAL 8)
			set(EMU51_BUNDLED_GLFW_LIB_DIR "${EMU51_BUNDLED_GLFW_DIR}/lib-vc2010-64")
		else()
			set(EMU51_BUNDLED_GLFW_LIB_DIR "${EMU51_BUNDLED_GLFW_DIR}/lib-vc2010-32")
		endif()
		set(EMU51_BUNDLED_GLFW_LIB "${EMU51_BUNDLED_GLFW_LIB_DIR}/glfw3.lib")

		if (EXISTS "${EMU51_BUNDLED_GLFW_DIR}/include/GLFW/glfw3.h" AND EXISTS "${EMU51_BUNDLED_GLFW_LIB}")
			add_library(emu51_glfw_bundled STATIC IMPORTED GLOBAL)
			set_target_properties(emu51_glfw_bundled PROPERTIES
				IMPORTED_LOCATION "${EMU51_BUNDLED_GLFW_LIB}"
				INTERFACE_INCLUDE_DIRECTORIES "${EMU51_BUNDLED_GLFW_DIR}/include"
			)
			# The bundled VS2010 library uses CRT symbols supplied by this
			# compatibility library in VS2015 and later (as in the ImGui example).
			if (MSVC AND MSVC_VERSION GREATER_EQUAL 1900)
				set_property(TARGET emu51_glfw_bundled APPEND PROPERTY
					INTERFACE_LINK_LIBRARIES legacy_stdio_definitions)
			endif()
			set(EMU51_GLFW_TARGET emu51_glfw_bundled)
		endif()
	endif()

	if (EMU51_GLFW_TARGET)
		if (WIN32)
			set(EMU51_IMGUI_PLATFORM_MAIN "imgui/windows/emu51_imgui_main.cpp")
		else()
			set(EMU51_IMGUI_PLATFORM_MAIN "imgui/linux/emu51_imgui_main.cpp")
		endif()

		add_library(emu51_thirdparty_backends OBJECT
			"${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_glfw.cpp"
			"${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_opengl3.cpp"
		)
		target_include_directories(emu51_thirdparty_backends SYSTEM PRIVATE
			"${CMAKE_SOURCE_DIR}/third_party/imgui"
			"${CMAKE_SOURCE_DIR}/third_party/imgui/backends"
		)
		if (EMU51_GLFW_TARGET)
			target_link_libraries(emu51_thirdparty_backends PRIVATE ${EMU51_GLFW_TARGET})
		endif()
		target_compile_definitions(emu51_thirdparty_backends PRIVATE
			"$<$<PLATFORM_ID:Windows>:NOMINMAX;WIN32_LEAN_AND_MEAN>"
		)

		add_executable(emu51_imgui
			$<TARGET_OBJECTS:emu51_imgui_common>
			$<TARGET_OBJECTS:emu51_thirdparty_imgui>
			$<TARGET_OBJECTS:emu51_thirdparty_backends>
			"${EMU51_IMGUI_PLATFORM_MAIN}"
			"imgui/emu51_imgui_glfw.cpp"
		)

		target_include_directories(emu51_imgui SYSTEM PRIVATE
			"${CMAKE_SOURCE_DIR}/third_party/imgui"
			"${CMAKE_SOURCE_DIR}/third_party/imgui/backends"
			"${CMAKE_SOURCE_DIR}/third_party/ImGuiColorTextEdit"
		)
		target_include_directories(emu51_imgui PRIVATE
			"${EMU51_CORE_DIR}"
			"${EMU51_ASM_DIR}"
			"${EMU51_IMGUI_DIR}"
		)
		target_link_libraries(emu51_imgui PRIVATE
			emu51_asm_lib
			emu51_settings_lib
			emu51_editor_files_lib
			emu51_warnings
			${EMU51_GLFW_TARGET}
			OpenGL::GL
		)
	else()
		message(STATUS "Skipping emu51_imgui: GLFW3 not found on ${CMAKE_SYSTEM_NAME}")
	endif()
endif()

if (TARGET emu51_imgui)
	install(TARGETS emu51_imgui
		BUNDLE DESTINATION "."
		RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	)
endif()

add_test(NAME emu51_packaged_resources
	COMMAND "${CMAKE_COMMAND}"
		"-DEXECUTABLE=$<TARGET_FILE:emu51_asm>"
		"-DRESOURCE_DIR=$<TARGET_FILE_DIR:emu51_asm>/resources/asm"
		"-DINSTALLED_ASM_FROM_BINDIR=${EMU51_INSTALLED_ASM_FROM_BINDIR}"
		"-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/packaged-resource-test/$<CONFIG>"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/tests/packaged_resources.cmake"
)

add_test(NAME emu51_headless_tools
    COMMAND "${CMAKE_COMMAND}"
        "-DRUNNER=$<TARGET_FILE:emu51>"
        "-DBENCH=$<TARGET_FILE:emu51_bench>"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/headless-tools-test/$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/headless_tools_test.cmake")

include("${PROJECT_SOURCE_DIR}/cmake/AppIcon.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/Packaging.cmake")
