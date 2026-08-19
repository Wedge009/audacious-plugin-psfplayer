#include "PlaybackTimer.h"

#include "PsfTags.h"

PlaybackTimer::PlaybackTimer(const CPsfBase::TagMap& tags, unsigned int sampleRate, bool ignoreLength)
{
	if(ignoreLength)
		return; // m_lengthSamples stays 0 -- Advance() treats that as "no limit"

	CPsfTags psfTags(tags);

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
		return 1.0f;

	uint64_t fadeEnd = m_lengthSamples + m_fadeSamples;
	if(m_elapsedSamples >= fadeEnd)
	{
		m_done = true;
		return 0.0f;
	}
	if(m_elapsedSamples >= m_lengthSamples && m_fadeSamples > 0)
	{
		uint64_t intoFade = m_elapsedSamples - m_lengthSamples;
		return 1.0f - (static_cast<float>(intoFade) / static_cast<float>(m_fadeSamples));
	}
	return 1.0f;
}
