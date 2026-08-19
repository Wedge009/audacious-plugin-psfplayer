#include "PlaybackTimer.h"

#include <string>

#include "PsfTags.h"

PlaybackTimer::PlaybackTimer(const CPsfBase::TagMap& tags, unsigned int sampleRate, bool ignoreLength)
{
	CPsfTags psfTags(tags);

	// "volume" is only a fallback for PSF sets with no replaygain tag at
	// all. When a replaygain tag IS present, plugin.cc's read_tag() feeds
	// it into Audacious's own native ReplayGain pipeline instead -- it's
	// user-configurable and clip-aware (via the accompanying peak tag),
	// which Play!'s own bare-multiply-then-hard-clamp "volume" mechanism
	// is not. Observed on real files that "volume" and
	// "replaygain_track_gain" encode the *same* underlying gain analysis
	// in different units (linear vs dB), not independent adjustments, so
	// applying both here would double up whatever the file's curator
	// intended -- hence never doing so, rather than trying to detect or
	// reconcile any mismatch between them.
	bool hasReplayGainTag = psfTags.HasTag("replaygain_track_gain") || psfTags.HasTag("replaygain_album_gain");
	if(!hasReplayGainTag && psfTags.HasTag("volume"))
	{
		try
		{
			m_baseVolume = std::stof(psfTags.GetTagValue("volume"));
		}
		catch(const std::exception&)
		{
			m_baseVolume = 1.0f;
		}
	}

	if(ignoreLength)
		return; // m_lengthSamples stays 0 -- Advance() treats that as "no limit"

	double lengthSeconds = 60.0; // CPlaybackController::Play()'s own default: 1 minute
	if(psfTags.HasTag("length"))
		lengthSeconds = CPsfTags::ConvertTimeString(psfTags.GetTagValue("length").c_str());

	double fadeSeconds = 10.0; // CPlaybackController::Play()'s own default: 10 seconds
	if(psfTags.HasTag("fade"))
		fadeSeconds = CPsfTags::ConvertTimeString(psfTags.GetTagValue("fade").c_str());

	m_lengthSamples = static_cast<uint64_t>(lengthSeconds * sampleRate);
	m_fadeSamples = static_cast<uint64_t>(fadeSeconds * sampleRate);
}

float PlaybackTimer::Advance(uint64_t sampleFrames)
{
	m_elapsedSamples += sampleFrames;

	if(m_lengthSamples == 0)
		return m_baseVolume;

	uint64_t fadeEnd = m_lengthSamples + m_fadeSamples;
	if(m_elapsedSamples >= fadeEnd)
	{
		m_done = true;
		return 0.0f;
	}
	if(m_elapsedSamples >= m_lengthSamples && m_fadeSamples > 0)
	{
		uint64_t intoFade = m_elapsedSamples - m_lengthSamples;
		float fadeRatio = 1.0f - (static_cast<float>(intoFade) / static_cast<float>(m_fadeSamples));
		return fadeRatio * m_baseVolume;
	}
	return m_baseVolume;
}
