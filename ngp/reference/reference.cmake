# Name:        reference.cmake
# Description: builds the reference tools of a Neo Geo Pocket Color game on MAME's cores: ngpref (the original
#              program and the console's BIOS on MAME's TMP95C061, with MAME's Z80, T6W28 and K2GE),
#              test_k2ge and test_k2ge_registers (the runtime's K2GE against MAME's), test_flash (the
#              board's flash against MAME's), test_ngpsound (the sound side), test_t6w28 (the runtime's
#              T6W28 against MAME's), test_mix (the runtime's mix against MAME's DACs and ngp.cpp's routes) and
#              test_tmp95c061 (the runtime's TMP95C061 devices against MAME's)
# Version:     0.1.0
# Created:     26-11-2026
# Modified:    27-11-2026
# Author:      Sebastian Toyos
#
# The game's CMakeLists.txt includes it when its reference tools are on, after runtime.cmake and its own data
# library, and after it has found Python3::Interpreter, and sets:
#   NGP_GAME_DATA     the game's library that reads its cartridge and the BIOS (game/extract.h, game/data.h)
#   NGP_ARCADE_CORE   arcade-decomp (the game's tools/core: MAME's Z80 core and the sound chips' stand-in for
#                     MAME's emu.h), which this builds on
# MAME's sources here are MAME 0.289's (mame/ and ../../tlcs900/mame, BSD-3-Clause); the Z80 core is
# arcade-decomp's copy.

set(NGP_REFERENCE ${CMAKE_CURRENT_LIST_DIR})
set(NGP_TLCS900 ${NGP_REFERENCE}/../../tlcs900)
if(NOT NGP_ARCADE_CORE)
	set(NGP_ARCADE_CORE ${CMAKE_SOURCE_DIR}/tools/core)
endif()
set(NGP_KONAMI_REFERENCE ${NGP_ARCADE_CORE}/konami/reference)
set(NGP_SYSTEM32_REFERENCE ${NGP_ARCADE_CORE}/system32/reference)
set(NGP_MSVC_WARNINGS /wd4244 /wd4267 /wd4100 /wd4146 /wd4127 /wd4018 /wd4334)

# MAME's TLCS-900/H core, its TMP95C061 and its disassembler, on tlcs900/stub's stand-in for MAME's emu.h
add_library(ngp_tlcs900_reference STATIC
	${NGP_TLCS900}/mame/tlcs900.cpp
	${NGP_TLCS900}/mame/tmp95c061.cpp
	${NGP_TLCS900}/mame/dasm900.cpp
)
target_include_directories(ngp_tlcs900_reference PUBLIC ${NGP_TLCS900}/stub ${NGP_TLCS900}/mame)
target_compile_features(ngp_tlcs900_reference PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(ngp_tlcs900_reference PRIVATE /bigobj ${NGP_MSVC_WARNINGS})
endif()

# The sound side: MAME's Z80 (system32/reference's copy of the core), T6W28 and two DACs (dac.cpp). The Z80
# core's steps are written from z80.lst at build time, as MAME does
set(NGP_Z80 ${NGP_SYSTEM32_REFERENCE}/mame/z80)
set(NGP_Z80_STEPS ${CMAKE_BINARY_DIR}/generated/cpu/z80/z80.hxx)
add_custom_command(OUTPUT ${NGP_Z80_STEPS}
	COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated/cpu/z80
	COMMAND Python3::Interpreter ${NGP_Z80}/z80make.py z80 ${NGP_Z80}/z80.lst ${NGP_Z80_STEPS}
	DEPENDS ${NGP_Z80}/z80make.py ${NGP_Z80}/z80.lst
	VERBATIM)
add_library(ngp_sound_reference STATIC
	${NGP_REFERENCE}/sound/ngpsound.cpp
	${NGP_Z80_STEPS}
)
target_include_directories(ngp_sound_reference
	PUBLIC ${NGP_REFERENCE}/sound
	PRIVATE ${NGP_KONAMI_REFERENCE}/sound ${NGP_Z80} ${CMAKE_BINARY_DIR}/generated ${NGP_REFERENCE}/mame)
target_compile_features(ngp_sound_reference PUBLIC cxx_std_20)
target_link_libraries(ngp_sound_reference PUBLIC ngp_runtime)
if(MSVC)
	target_compile_options(ngp_sound_reference PRIVATE /bigobj ${NGP_MSVC_WARNINGS})
endif()

# The checks of the sound side: ngpsound.cpp against what ngpsound.h says (a Z80 program in the shared RAM),
# the runtime's T6W28 (ngp/t6w28.h) against MAME's t6w28.cpp on random writes and runs of samples, and the
# runtime's mix (ngp/mix.h) against MAME's dac.cpp and ngp.cpp's routes on every T6W28 value with every byte
add_executable(test_ngpsound ${NGP_REFERENCE}/sound/test_ngpsound.cpp)
target_link_libraries(test_ngpsound PRIVATE ngp_sound_reference)
add_executable(test_t6w28 ${NGP_REFERENCE}/sound/test_t6w28.cpp)
target_include_directories(test_t6w28 PRIVATE ${NGP_KONAMI_REFERENCE}/sound ${NGP_REFERENCE}/mame)
target_link_libraries(test_t6w28 PRIVATE ngp_runtime)
target_compile_features(test_t6w28 PRIVATE cxx_std_20)
add_executable(test_mix ${NGP_REFERENCE}/sound/test_mix.cpp)
target_include_directories(test_mix PRIVATE ${NGP_KONAMI_REFERENCE}/sound ${NGP_REFERENCE}/mame)
target_link_libraries(test_mix PRIVATE ngp_runtime)
target_compile_features(test_mix PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(test_ngpsound PRIVATE ${NGP_MSVC_WARNINGS})
	target_compile_options(test_t6w28 PRIVATE /bigobj ${NGP_MSVC_WARNINGS})
	target_compile_options(test_mix PRIVATE /bigobj ${NGP_MSVC_WARNINGS})
endif()

# The video side: MAME's K2GE on video/stub's stand-in for MAME's emu.h
add_library(ngp_video_reference STATIC ${NGP_REFERENCE}/video/k2gevideo.cpp)
target_include_directories(ngp_video_reference
	PUBLIC ${NGP_REFERENCE}/video
	PRIVATE ${NGP_REFERENCE}/video/stub ${NGP_REFERENCE}/mame)
target_compile_features(ngp_video_reference PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(ngp_video_reference PRIVATE ${NGP_MSVC_WARNINGS})
endif()

# The check of the runtime's K2GE against MAME's, on dumps of the chip's memory (video/test_k2ge.cpp)
add_executable(test_k2ge ${NGP_REFERENCE}/video/test_k2ge.cpp)
target_link_libraries(test_k2ge PRIVATE ngp_video_reference ngp_runtime)
if(MSVC)
	target_compile_options(test_k2ge PRIVATE ${NGP_MSVC_WARNINGS})
endif()

# The check of the runtime's K2GE registers and lines' timing against MAME's, on random writes, reads and times
# (video/test_k2ge_registers.cpp)
add_executable(test_k2ge_registers ${NGP_REFERENCE}/video/test_k2ge_registers.cpp)
target_link_libraries(test_k2ge_registers PRIVATE ngp_video_reference ngp_runtime)
if(MSVC)
	target_compile_options(test_k2ge_registers PRIVATE ${NGP_MSVC_WARNINGS})
endif()

# The check of the board's flash (ngp/machine.h) against MAME's flash_w (flash.hxx), on random command
# sequences over cartridges of every size MAME takes
add_executable(test_flash ${NGP_REFERENCE}/test_flash.cpp)
target_link_libraries(test_flash PRIVATE ngp_runtime)
if(MSVC)
	target_compile_options(test_flash PRIVATE ${NGP_MSVC_WARNINGS})
endif()

# The check of the runtime's TMP95C061 devices (ngp/tmp95c061.h) against MAME's, on random writes, reads,
# input lines, runs, micro DMA and interrupt checks
add_executable(test_tmp95c061 ${NGP_REFERENCE}/test_tmp95c061.cpp)
target_link_libraries(test_tmp95c061 PRIVATE ngp_tlcs900_reference ngp_runtime)
if(MSVC)
	target_compile_options(test_tmp95c061 PRIVATE ${NGP_MSVC_WARNINGS})
endif()

# ngpref
add_executable(ngpref ${NGP_REFERENCE}/ngpref.cpp)
target_link_libraries(ngpref PRIVATE ngp_tlcs900_reference ngp_sound_reference ngp_video_reference ngp_runtime
	${NGP_GAME_DATA})
if(MSVC)
	target_compile_options(ngpref PRIVATE ${NGP_MSVC_WARNINGS})
endif()
