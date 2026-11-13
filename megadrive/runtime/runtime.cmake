# Name:        runtime.cmake
# Description: builds the Mega Drive's runtime: the console as the program sees it (md_runtime) and the
#              reader of the player's cartridge (md_cartridge, for the tools that run the original code)
# Version:     0.1.0
# Created:     13-11-2026
# Modified:    13-11-2026
# Author:      Sebastian Toyos
#
# A game's CMakeLists.txt includes it and links md_runtime (and, for its reference tools, md_cartridge).

set(MD_RUNTIME ${CMAKE_CURRENT_LIST_DIR})

add_library(md_runtime STATIC
	${MD_RUNTIME}/src/machine.c
	${MD_RUNTIME}/src/vdp.c
	${MD_RUNTIME}/src/script.c
)
target_include_directories(md_runtime PUBLIC ${MD_RUNTIME}/include)

add_library(md_cartridge STATIC
	${MD_RUNTIME}/src/cartridge.c
	${MD_RUNTIME}/src/inflate.c
)
target_include_directories(md_cartridge PUBLIC ${MD_RUNTIME}/include PRIVATE ${MD_RUNTIME}/src)
