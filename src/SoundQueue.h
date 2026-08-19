#pragma once

#include <deque>

#include <libaudcore/threads.h>

#include "sound/SoundHandler.h"
#include "Types.h"

// Bridges Play!'s CPsfVm -- which owns its own background thread and
// *pushes* rendered PCM via CSoundHandler::Write(), unbounded and unpaced
// -- on to Audacious's InputPlugin::play(), which *pulls* audio on its own
// thread via a blocking loop. Write()/HasFreeBuffers()/RecycleBuffers() are
// called only from CPsfVm's own thread (see Iop_PsfSubSystem::Update());
// WaitAndRead()/Shutdown() are called only from the play() thread.
//
// HasFreeBuffers() returning false genuinely throttles CPsfVm's emulation
// (Iop_PsfSubSystem::Update() skips CPU stepping entirely while it's
// false), so the capacity chosen here paces emulation to real-time, not
// just audio output.
class SoundQueue : public CSoundHandler
{
public:
	// One second of interleaved-stereo 44.1kHz int16 samples. Comfortably
	// larger than the ~880-sample (BLOCK_SIZE*BLOCK_COUNT) chunks
	// Iop_PsfSubSystem::Update() writes at a time, so back-pressure engages
	// smoothly rather than thrashing every write.
	static constexpr size_t kCapacitySamples = 44100 * 2;

	// CSoundHandler overrides -- called from CPsfVm's own thread only.
	void Reset() override;
	void Write(int16* buffer, unsigned int sampleCount, unsigned int sampleRate) override;
	bool HasFreeBuffers() override;
	void RecycleBuffers() override;

	// Called from the play() thread only.
	//
	// Blocks until at least one sample is available or Shutdown() has been
	// called with nothing left queued. Copies up to maxSamples int16 values
	// into dst and returns the count actually copied (0 only on shut-down
	// with an empty queue).
	size_t WaitAndRead(int16* dst, size_t maxSamples);

	// Blocks until the first Write() call has happened (so GetSampleRate()
	// becomes meaningful) or shut-down. Doesn't consume anything -- the
	// data it waited for is still there for the first WaitAndRead().
	// Returns false only on shut-down with nothing ever written.
	bool WaitForFirstWrite();

	// Wakes any blocked WaitAndRead() and makes future Write() calls no-ops,
	// so CPsfVm's own thread doesn't stall for ever on a torn-down consumer
	// during shut-down. Idempotent.
	void Shutdown();

	// Sample rate most recently reported by Write() (0 before the first
	// call). CPsfVm always reports the same fixed rate for the lifetime of
	// a loaded PSF (see project memory on the hard-coded SAMPLING_RATE), but
	// this is read fresh rather than assumed, in case that ever changes.
	unsigned int GetSampleRate() const;

private:
	mutable aud::mutex m_mutex;
	aud::condvar m_notEmpty;
	std::deque<int16> m_samples;
	unsigned int m_sampleRate = 0;
	bool m_shuttingDown = false;
};
