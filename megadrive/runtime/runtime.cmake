# Name:        runtime.cmake
# Description: builds the Mega Drive's runtime: the console as the program sees it (md_runtime), the reader
#              of the player's cartridge (md_cartridge, for the tools that run the original code), and the
#              translation scaffold of a game's 68000 program (md_translated_program)
# Version:     0.1.0
# Created:     13-11-2026
# Modified:    20-11-2026
# Author:      Sebastian Toyos
#
# A game's CMakeLists.txt includes it and links md_runtime (and, for its reference tools, md_cartridge). The
# game gives the console its frame waits (md/frames.h: md_game_frame_waits, md_game_frame_idle), which mdref
# and the program link.

set(MD_RUNTIME ${CMAKE_CURRENT_LIST_DIR})

add_library(md_runtime STATIC
	${MD_RUNTIME}/src/machine.c
	${MD_RUNTIME}/src/vdp.c
	${MD_RUNTIME}/src/script.c
	${MD_RUNTIME}/src/frames.c
)
target_include_directories(md_runtime PUBLIC ${MD_RUNTIME}/include)

add_library(md_cartridge STATIC
	${MD_RUNTIME}/src/cartridge.c
	${MD_RUNTIME}/src/inflate.c
)
target_include_directories(md_cartridge PUBLIC ${MD_RUNTIME}/include PRIVATE ${MD_RUNTIME}/src)

# The translation scaffold: the C a game's tools/recompile.py writes from the user's cartridge into GENERATED
# (program.h, program_NN.c, entries.c), an instruction at a time, on the 68000's runtime (src/m68000.c, the
# tasks' stacks), as NAME_program, and NAME_headless to run it with a control script (src/headless.c, mdref's
# options and hashes). The libraries after GENERATED are the game's: its cartridge (game/extract.h) and its
# frame waits. Nothing of MAME's goes into it. With no generated C there is no scaffold.
function(md_translated_program NAME GENERATED)
	file(GLOB MD_GENERATED_SOURCES CONFIGURE_DEPENDS ${GENERATED}/*.c)
	if(NOT MD_GENERATED_SOURCES)
		message(STATUS "No generated program in ${GENERATED} (tools/recompile.py): no ${NAME}_headless")
		return()
	endif()
	add_library(${NAME}_program STATIC ${MD_RUNTIME}/src/m68000.c ${MD_RUNTIME}/src/tasks.c ${MD_GENERATED_SOURCES})
	target_include_directories(${NAME}_program PUBLIC ${MD_RUNTIME}/include ${GENERATED})
	target_link_libraries(${NAME}_program PUBLIC md_runtime ${ARGN})
	if(MSVC)
		target_compile_options(${NAME}_program PRIVATE /bigobj)
	endif()
	add_executable(${NAME}_headless ${MD_RUNTIME}/src/headless.c)
	target_link_libraries(${NAME}_headless PRIVATE ${NAME}_program)
endfunction()
