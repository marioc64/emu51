# SPDX-License-Identifier: GPL-2.0-only
# Top-level CMake project file.
cmake_minimum_required(VERSION 3.16)

project("emu51" VERSION 1.0.0 LANGUAGES C CXX)

if (APPLE)
	enable_language(OBJCXX)
	set(CMAKE_OBJCXX_STANDARD 20)
	set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
	set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

option(EMU51_WERROR "Treat warnings as errors for emu51 sources" OFF)
option(EMU51_ASAN   "Build emu51 sources with AddressSanitizer + UBSan" OFF)

# Compile options that apply only to emu51's own translation units, not to
# vendored third-party code (ImGui, ImGuiColorTextEdit). Link this interface
# library into any target whose sources we own.
add_library(emu51_warnings INTERFACE)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
	target_compile_options(emu51_warnings INTERFACE
		-Wall -Wextra -Wpedantic
		-Wnon-virtual-dtor -Wnull-dereference
	)
	if (EMU51_WERROR)
		target_compile_options(emu51_warnings INTERFACE -Werror)
	endif()
elseif (MSVC)
	target_compile_options(emu51_warnings INTERFACE /W4 /permissive-)
	if (EMU51_WERROR)
		target_compile_options(emu51_warnings INTERFACE /WX)
	endif()
endif()

if (EMU51_ASAN AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
	add_library(emu51_sanitizers INTERFACE)
	target_compile_options(emu51_sanitizers INTERFACE
		-fsanitize=address,undefined -fno-omit-frame-pointer
	)
	target_link_options(emu51_sanitizers INTERFACE
		-fsanitize=address,undefined
	)
	target_link_libraries(emu51_warnings INTERFACE emu51_sanitizers)
	message(STATUS "emu51: AddressSanitizer + UBSan enabled")
endif()

add_subdirectory("emu51")
