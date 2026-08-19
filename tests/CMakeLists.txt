# SoundQueue is the one piece of the threading bridge that's meaningfully
# testable without a real Audacious host or a real PSF file: it's pure
# producer/consumer logic (CPsfVm's push side vs. play()'s pull side)
# behind a small, self-contained interface. This intentionally doesn't
# link PsfCore/PlayCore at all -- CSoundHandler is a pure-virtual interface
# with no .cc of its own, and the aud:: primitives SoundQueue uses
# (aud::mutex, aud::condvar) are header-only wrappers around std::mutex/
# std::condition_variable -- so this only needs the audacious headers for
# threads.h, not the library itself.
add_executable(test_soundqueue
	test_soundqueue.cc
	${CMAKE_SOURCE_DIR}/src/SoundQueue.cc
)
target_include_directories(test_soundqueue PRIVATE
	${AUDACIOUS_INCLUDE_DIRS}
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/third_party/Play/tools/PsfPlayer/Source
	${CMAKE_SOURCE_DIR}/third_party/Play/Source
	${CMAKE_SOURCE_DIR}/third_party/Play/deps/Framework/include
)
target_compile_options(test_soundqueue PRIVATE ${AUDACIOUS_CFLAGS_OTHER})

add_test(NAME soundqueue COMMAND test_soundqueue)

# Manual smoke test against a real PSF/PSF2 file -- not run by ctest (no
# sample file ships with this repository). Exercises the full bridge
# (PsfSetMaterializer -> VmSession -> SoundQueue -> PlaybackTimer)
# end-to-end. See the file's own header comment for why this is safe to
# run without touching a live Audacious instance.
add_executable(test_integration_manual
	test_integration_manual.cc
	${CMAKE_SOURCE_DIR}/src/SoundQueue.cc
	${CMAKE_SOURCE_DIR}/src/VmSession.cc
	${CMAKE_SOURCE_DIR}/src/PlaybackTimer.cc
	${CMAKE_SOURCE_DIR}/src/PsfSetMaterializer.cc
)
target_include_directories(test_integration_manual PRIVATE
	${AUDACIOUS_INCLUDE_DIRS}
	${CMAKE_SOURCE_DIR}/src
)
target_compile_options(test_integration_manual PRIVATE ${AUDACIOUS_CFLAGS_OTHER})
target_link_libraries(test_integration_manual PRIVATE PsfCore ${AUDACIOUS_LIBRARIES})
target_link_directories(test_integration_manual PRIVATE ${AUDACIOUS_LIBRARY_DIRS})
