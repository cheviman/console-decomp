# Name:        reference.cmake
# Description: builds the reference tools of a Mega Drive game on MAME's cores: mdref (the original program
#              on MAME's 68000 against the runtime's console, with MAME's Z80 and PSG and ymfm's YM2612)
# Version:     0.1.0
# Created:     13-11-2026
# Modified:    13-11-2026
# Author:      Sebastian Toyos
#
# The game's CMakeLists.txt includes it when its reference tools are on, after runtime.cmake and its own data
# library, and after it has found Python3::Interpreter, and sets:
#   MD_GAME_DATA     the game's library that reads its cartridge (game/extract.h, game/data.h)
#   MD_ARCADE_CORE   arcade-decomp (the game's tools/core: MAME's 68000 and Z80 cores, their stand-ins,
#                    ymfm), which this builds on
# MAME's sources here are MAME 0.289's (mame/, BSD-3-Clause); the cores are arcade-decomp's copies.

set(MD_REFERENCE ${CMAKE_CURRENT_LIST_DIR})
if(NOT MD_ARCADE_CORE)
	set(MD_ARCADE_CORE ${CMAKE_SOURCE_DIR}/tools/core)
endif()
set(MD_KONAMI_REFERENCE ${MD_ARCADE_CORE}/konami/reference)
set(MD_SYSTEM32_REFERENCE ${MD_ARCADE_CORE}/system32/reference)

# MAME's 68000 core: its handlers are written from m68000.lst at build time, as MAME's own build does
# (m68000gen.py); the direct ones only (arcade-decomp's m68000_spaces.cpp stands for the others)
set(MD_M68000 ${MD_KONAMI_REFERENCE}/mame/m68000)
set(MD_M68000_GENERATED ${CMAKE_BINARY_DIR}/generated/cpu/m68000)
set(MD_M68000_OUTPUTS "")
foreach(MD_PART header:m68000-head.h decode:m68000-decode.cpp sdf:m68000-sdf.cpp sdp:m68000-sdp.cpp)
	string(REPLACE ":" ";" MD_PART "${MD_PART}")
	list(GET MD_PART 0 MD_MODE)
	list(GET MD_PART 1 MD_FILE)
	add_custom_command(OUTPUT ${MD_M68000_GENERATED}/${MD_FILE}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${MD_M68000_GENERATED}
		COMMAND Python3::Interpreter ${MD_M68000}/m68000gen.py ${MD_MODE} ${MD_M68000}/m68000.lst
			${MD_M68000_GENERATED}/${MD_FILE}
		DEPENDS ${MD_M68000}/m68000gen.py ${MD_M68000}/m68000.lst
		VERBATIM)
	list(APPEND MD_M68000_OUTPUTS ${MD_M68000_GENERATED}/${MD_FILE})
endforeach()
add_library(md_m68000_reference STATIC
	${MD_M68000}/m68000.cpp
	${MD_M68000}/m68kdasm.cpp
	${MD_KONAMI_REFERENCE}/m68000_spaces.cpp
	${MD_M68000_OUTPUTS}
)
target_include_directories(md_m68000_reference PUBLIC ${MD_KONAMI_REFERENCE}/stub ${MD_M68000} ${MD_M68000_GENERATED})
target_compile_features(md_m68000_reference PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(md_m68000_reference PRIVATE /bigobj /wd4244 /wd4267 /wd4100 /wd4146 /wd4127)
endif()

# The sound side: MAME's Z80 (system32/reference's copy of the core) and PSG, ymfm's YM2612. The Z80 core's
# steps are written from z80.lst at build time, as MAME does
set(MD_Z80 ${MD_SYSTEM32_REFERENCE}/mame/z80)
set(MD_Z80_STEPS ${CMAKE_BINARY_DIR}/generated/cpu/z80/z80.hxx)
add_custom_command(OUTPUT ${MD_Z80_STEPS}
	COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated/cpu/z80
	COMMAND Python3::Interpreter ${MD_Z80}/z80make.py z80 ${MD_Z80}/z80.lst ${MD_Z80_STEPS}
	DEPENDS ${MD_Z80}/z80make.py ${MD_Z80}/z80.lst
	VERBATIM)
add_library(md_sound_reference STATIC
	${MD_REFERENCE}/sound/mdsound.cpp
	${MD_SYSTEM32_REFERENCE}/ymfm/ymfm_opn.cpp
	${MD_SYSTEM32_REFERENCE}/ymfm/ymfm_adpcm.cpp
	${MD_SYSTEM32_REFERENCE}/ymfm/ymfm_ssg.cpp
	${MD_Z80_STEPS}
)
target_include_directories(md_sound_reference
	PUBLIC ${MD_REFERENCE}/sound
	PRIVATE ${MD_KONAMI_REFERENCE}/sound ${MD_SYSTEM32_REFERENCE}/ymfm ${MD_Z80} ${CMAKE_BINARY_DIR}/generated
		${MD_REFERENCE}/mame/sound)
target_link_libraries(md_sound_reference PUBLIC md_runtime)
target_compile_features(md_sound_reference PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(md_sound_reference PRIVATE /bigobj /wd4244 /wd4267 /wd4100 /wd4146 /wd4127 /wd4018)
endif()

# mdref
add_executable(mdref ${MD_REFERENCE}/mdref.cpp)
target_link_libraries(mdref PRIVATE md_m68000_reference md_sound_reference md_runtime ${MD_GAME_DATA})
if(MSVC)
	target_compile_options(mdref PRIVATE /wd4244 /wd4267)
endif()
