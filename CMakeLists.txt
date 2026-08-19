cmake_minimum_required(VERSION 3.18)
project(audacious-plugin-psfplayer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(AUDACIOUS REQUIRED audacious)

# PsfCore's own build is heavier than we need (see project memory: it links
# the whole Play! emulator core unconditionally), but --gc-sections at link
# time discards the unused rest. Amazon S3 support isn't needed at all,
# and zstd's legacy-format decoders are dead weight for genuine-DEFLATE PSF
# program sections -- both are free trims with zero functional loss here.
set(ENABLE_AMAZON_S3 OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
set(TARGET_PLATFORM_UNIX ON CACHE BOOL "" FORCE)

# PsfCore/PlayCore/etc. are static libraries built without -fPIC by
# default; we're linking them into a shared module (the plug-in .so), so
# every intermediate static lib needs position-independent code too.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_options(-ffunction-sections -fdata-sections)
add_link_options(-Wl,--gc-sections)

find_package(Git QUIET)

add_subdirectory(third_party/Play/tools/PsfPlayer/Source PsfPlayerCore)

# zlib's own vendored CMakeLists.txt (third_party/Play/deps/libchdr/deps/
# zlib-1.3.1/CMakeLists.txt), pulled in transitively by the add_subdirectory
# above, unconditionally renames its own zconf.h -> zconf.h.included
# whenever it finds one already checked out (a long-standing upstream zlib
# CMake quirk -- a bare `if(EXISTS ...)`, not gated behind any option we
# could just turn off). That leaves the nested libchdr sub-module showing
# as locally modified after every configure. Undo it immediately rather
# than leaving that lying around -- restoring a tracked file beats
# patching yet another (doubly-nested) sub-module just to avoid the rename
# in the first place.
set(PSFPLAYER_LIBCHDR_DIR "${CMAKE_SOURCE_DIR}/third_party/Play/deps/libchdr")
if(GIT_FOUND AND EXISTS "${PSFPLAYER_LIBCHDR_DIR}/deps/zlib-1.3.1/zconf.h.included")
	execute_process(
		COMMAND ${GIT_EXECUTABLE} checkout -- deps/zlib-1.3.1/zconf.h
		WORKING_DIRECTORY "${PSFPLAYER_LIBCHDR_DIR}"
		RESULT_VARIABLE PSFPLAYER_ZCONF_RESTORE_RESULT
		ERROR_QUIET
	)
	if(PSFPLAYER_ZCONF_RESTORE_RESULT EQUAL 0)
		file(REMOVE "${PSFPLAYER_LIBCHDR_DIR}/deps/zlib-1.3.1/zconf.h.included")
		message(STATUS "Restored third_party/Play/deps/libchdr's zconf.h (undid zlib's own configure-time rename)")
	endif()
endif()

# Play!'s own front-ends (ui_qt/CMakeLists.txt, ui_libretro/CMakeLists.txt)
# each derive a PLAY_VERSION the same way -- git describe --always against
# their own check-out -- but tools/PsfPlayer/Source (the only part we build)
# never defines or references it, so there's no collision re-using the
# identical technique here for the plug-in's own About text, so a user can
# tell which Play! commit their installed psfplayer.so is actually built
# against.
set(PSFPLAYER_PLAY_VERSION "unknown")
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/Play/.git")
	execute_process(
		COMMAND ${GIT_EXECUTABLE} describe --always
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/Play"
		RESULT_VARIABLE GIT_DESCRIBE_RESULT
		OUTPUT_VARIABLE GIT_DESCRIBE_OUTPUT
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)
	if(GIT_DESCRIBE_RESULT EQUAL 0)
		set(PSFPLAYER_PLAY_VERSION "${GIT_DESCRIBE_OUTPUT}")
	endif()
endif()
message(STATUS "Play! PsfCore version for About text: ${PSFPLAYER_PLAY_VERSION}")

add_library(psfplayer MODULE
	src/plugin.cc
	src/SoundQueue.cc
	src/VmSession.cc
	src/PlaybackTimer.cc
	src/PsfSetMaterializer.cc
)

set_target_properties(psfplayer PROPERTIES
	PREFIX ""
	OUTPUT_NAME "psfplayer"
	CXX_VISIBILITY_PRESET hidden
)

target_include_directories(psfplayer PRIVATE ${AUDACIOUS_INCLUDE_DIRS})
target_compile_options(psfplayer PRIVATE ${AUDACIOUS_CFLAGS_OTHER})
target_compile_definitions(psfplayer PRIVATE PSFPLAYER_PLAY_VERSION="${PSFPLAYER_PLAY_VERSION}")
target_link_libraries(psfplayer PRIVATE PsfCore ${AUDACIOUS_LIBRARIES})
target_link_directories(psfplayer PRIVATE ${AUDACIOUS_LIBRARY_DIRS})

# PsfCore/PlayCore/zstd/unrar/etc. are statically linked in, and none of
# them were built with hidden visibility (CXX_VISIBILITY_PRESET only
# governs psfplayer's own sources) -- without this, their symbols (zstd's
# FSE_*, etc.) leak into this .so's dynamic export table, and could
# collide with an unrelated plug-in that happens to statically link the
# same library. Nothing outside this plug-in should ever need to see them.
target_link_options(psfplayer PRIVATE -Wl,--exclude-libs,ALL)

pkg_get_variable(AUDACIOUS_PLUGIN_DIR audacious plugin_dir)
install(TARGETS psfplayer LIBRARY DESTINATION "${AUDACIOUS_PLUGIN_DIR}/Input")

enable_testing()
add_subdirectory(tests)
