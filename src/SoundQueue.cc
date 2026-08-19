#include "SoundQueue.h"

#include <algorithm>

void SoundQueue::Reset()
{
	auto lock = m_mutex.take();
	m_samples.clear();
}

// Never blocks: Iop_PsfSubSystem::Update() re-checks HasFreeBuffers() once
// per call and writes at most one BLOCK_SIZE*BLOCK_COUNT chunk per call
// when it does, so a caller that respects HasFreeBuffers() can overshoot
// kCapacitySamples by at most one such chunk (~2% of capacity) -- not
// worth a blocking write that would stall CPsfVm's own thread.
void SoundQueue::Write(int16* buffer, unsigned int sampleCount, unsigned int sampleRate)
{
	auto lock = m_mutex.take();
	if(m_shuttingDown)
		return;

	m_sampleRate = sampleRate;
	m_samples.insert(m_samples.end(), buffer, buffer + sampleCount);

	lock.unlock();
	m_notEmpty.notify_all();
}

bool SoundQueue::HasFreeBuffers()
{
	auto lock = m_mutex.take();
	return m_shuttingDown || m_samples.size() < kCapacitySamples;
}

void SoundQueue::RecycleBuffers()
{
	// Nothing to recycle -- our capacity check in HasFreeBuffers() reads
	// live queue depth directly, so there's no separate buffer pool to
	// free up here. Iop_PsfSubSystem::Update() calls this unconditionally
	// after a false HasFreeBuffers(), so it must remain a safe no-op.
}

size_t SoundQueue::WaitAndRead(int16* dst, size_t maxSamples)
{
	auto lock = m_mutex.take();
	m_notEmpty.wait(lock, [this]() { return m_shuttingDown || !m_samples.empty(); });

	size_t count = std::min(maxSamples, m_samples.size());
	std::copy(m_samples.begin(), m_samples.begin() + count, dst);
	m_samples.erase(m_samples.begin(), m_samples.begin() + count);

	return count;
}

bool SoundQueue::WaitForFirstWrite()
{
	auto lock = m_mutex.take();
	m_notEmpty.wait(lock, [this]() { return m_shuttingDown || !m_samples.empty(); });
	return !m_samples.empty();
}

void SoundQueue::Shutdown()
{
	auto lock = m_mutex.take();
	m_shuttingDown = true;
	lock.unlock();
	m_notEmpty.notify_all();
}

unsigned int SoundQueue::GetSampleRate() const
{
	auto lock = m_mutex.take();
	return m_sampleRate;
}
