#pragma once

#include <cstdint>

#include "PsfBase.h"

// CPsfVm never signals "song ended" on its own -- the real ui_qt frontend
// imposes a length/fade/end-of-track policy entirely client-side, in
// CPlaybackController, wired to CPsfVm::OnNewFrame (once per emulated
// 60fps video frame). This is that same policy re-expressed against
// elapsed *sample* count instead, since Audacious's own clock is
// sample-based, not video-frame-based -- and because Iop_PsfSubSystem
// hard-codes 60fps regardless of a PSF's actual psf_refresh (NTSC/PAL),
// video-frame-tick timing would inherit that same NTSC-only assumption
// for no benefit here.
class PlaybackTimer
{
public:
	// tags: the raw tag map from CPsfLoader::LoadPsf(). Reads "length"/
	// "fade" via CPsfTags::ConvertTimeString(), with the same defaults
	// CPlaybackController::Play() uses (60s length, 10s fade) when absent.
	// ignoreLength disables the length/fade deadline entirely (matches the
	// existing OpenPSF plug-in's "ignore_length" preference) -- IsDone()
	// then never returns true; playback runs until externally stopped.
	PlaybackTimer(const CPsfBase::TagMap& tags, unsigned int sampleRate, bool ignoreLength);

	// Advances elapsed playback by sampleFrames (stereo frames -- half the
	// int16 count SoundQueue deals in) and returns the volume multiplier
	// the caller should apply via VmSession::SetVolumeAdjust(): 1.0 before
	// `length`, linearly ramping to 0.0 across `fade`, 0.0 once done.
	float Advance(uint64_t sampleFrames);

	// True once length+fade has fully elapsed. Always false if ignoreLength
	// was set.
	bool IsDone() const { return m_done; }

private:
	uint64_t m_lengthSamples = 0; // 0 means "no limit" (ignoreLength)
	uint64_t m_fadeSamples = 0;
	uint64_t m_elapsedSamples = 0;
	bool m_done = false;
};
