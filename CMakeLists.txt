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

add_subdirectory(third_party/Play/tools/PsfPlayer/Source PsfPlayerCore)

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
