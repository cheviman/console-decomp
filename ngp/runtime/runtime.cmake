# Name:        runtime.cmake
# Description: builds the Neo Geo Pocket Color's runtime: the console's model and what its tools share
#              (ngp_runtime: the board, the TMP95C061's devices, the K2GE, the T6W28, the controls scripts),
#              and the reader of the player's cartridge (ngp_cartridge, for the tools that run the original code)
# Version:     0.1.0
# Created:     26-11-2026
# Modified:    27-11-2026
# Author:      Sebastian Toyos
#
# A game's CMakeLists.txt includes it and links ngp_runtime (and, for its reference tools, ngp_cartridge).

set(NGP_RUNTIME ${CMAKE_CURRENT_LIST_DIR})

add_library(ngp_runtime STATIC ${NGP_RUNTIME}/src/script.c ${NGP_RUNTIME}/src/k2ge.c ${NGP_RUNTIME}/src/t6w28.c
	${NGP_RUNTIME}/src/machine.c ${NGP_RUNTIME}/src/tmp95c061.c)
target_include_directories(ngp_runtime PUBLIC ${NGP_RUNTIME}/include)

# The reader of the player's cartridge is the Mega Drive's (md/cartridge.h: a ROM file, a .zip holding it or
# a folder, known by its CRC32), which knows nothing of the console it reads for
set(NGP_MD_RUNTIME ${NGP_RUNTIME}/../../megadrive/runtime)
add_library(ngp_cartridge STATIC
	${NGP_MD_RUNTIME}/src/cartridge.c
	${NGP_MD_RUNTIME}/src/inflate.c
)
target_include_directories(ngp_cartridge PUBLIC ${NGP_MD_RUNTIME}/include PRIVATE ${NGP_MD_RUNTIME}/src)
