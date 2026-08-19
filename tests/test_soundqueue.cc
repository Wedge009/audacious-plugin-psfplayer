// Standalone synthetic test for the CPsfVm<->play() threading bridge.
// No real Audacious host, no real PSF file -- just two real threads
// exercising SoundQueue exactly the way Iop_PsfSubSystem::Update() (the
// producer) and InputPlugin::play() (the consumer) actually do.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "SoundQueue.h"

namespace
{

int g_failures = 0;

#define CHECK(cond)                                                          \
	do                                                                         \
	{                                                                          \
		if(!(cond))                                                             \
		{                                                                        \
			std::fprintf(stderr, "CHECK FAILED at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			g_failures++;                                                         \
		}                                                                        \
	} while(0)

void TestBasicRoundtrip()
{
	SoundQueue queue;

	std::vector<int16> source(10000);
	for(size_t i = 0; i < source.size(); i++)
		source[i] = static_cast<int16>(i);

	// Write in irregular chunks, like Iop_PsfSubSystem::Update() does
	// (BLOCK_SIZE*BLOCK_COUNT at a time) rather than all at once.
	size_t offset = 0;
	while(offset < source.size())
	{
		size_t chunk = std::min<size_t>(880, source.size() - offset);
		queue.Write(source.data() + offset, static_cast<unsigned int>(chunk), 44100);
		offset += chunk;
	}

	std::vector<int16> result;
	result.reserve(source.size());
	int16 buf[4096];
	while(result.size() < source.size())
	{
		size_t got = queue.WaitAndRead(buf, 4096);
		CHECK(got > 0);
		result.insert(result.end(), buf, buf + got);
	}

	CHECK(result.size() == source.size());
	CHECK(result == source);
	CHECK(queue.GetSampleRate() == 44100);
}

void TestBackpressureThrottlesProducer()
{
	SoundQueue queue;
	int16 chunk[880] = {0};

	// Fill past capacity without ever consuming -- mirrors
	// Iop_PsfSubSystem::Update() writing without play() draining.
	size_t written = 0;
	while(queue.HasFreeBuffers() && written < SoundQueue::kCapacitySamples * 2)
	{
		queue.Write(chunk, 880, 44100);
		written += 880;
	}

	// HasFreeBuffers() must eventually say no -- this is the actual
	// mechanism that throttles CPsfVm's own thread to real-time pace
	// (Iop_PsfSubSystem::Update() skips CPU stepping entirely while
	// false). If this never goes false, emulation would race ahead of
	// whatever play() can consume, unbounded.
	CHECK(!queue.HasFreeBuffers());
	CHECK(written >= SoundQueue::kCapacitySamples);
	CHECK(written < SoundQueue::kCapacitySamples * 2); // didn't run away

	// Draining should free it back up.
	int16 buf[4096];
	while(queue.HasFreeBuffers() == false)
		queue.WaitAndRead(buf, 4096);
	CHECK(queue.HasFreeBuffers());
}

void TestShutdownUnblocksWaitingReader()
{
	SoundQueue queue;
	std::atomic<bool> readerReturned{false};
	std::atomic<size_t> readerResult{999};

	std::thread reader([&]() {
		int16 buf[16];
		size_t got = queue.WaitAndRead(buf, 16); // blocks: queue is empty
		readerResult = got;
		readerReturned = true;
	});

	// Give the reader a real chance to actually be blocked inside
	// WaitAndRead() before we shut down, not just about to call it.
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	CHECK(!readerReturned);

	queue.Shutdown();

	auto start = std::chrono::steady_clock::now();
	while(!readerReturned && (std::chrono::steady_clock::now() - start) < std::chrono::seconds(2))
		std::this_thread::sleep_for(std::chrono::milliseconds(5));

	CHECK(readerReturned.load());
	CHECK(readerResult.load() == 0);

	reader.join();
}

void TestWriteAfterShutdownIsNoop()
{
	SoundQueue queue;
	queue.Shutdown();

	int16 chunk[880] = {0};
	queue.Write(chunk, 880, 44100); // must not resurrect the queue

	CHECK(queue.HasFreeBuffers()); // shut-down always reports free
	int16 buf[16];
	size_t got = queue.WaitAndRead(buf, 16);
	CHECK(got == 0); // nothing was actually queued
}

void TestWaitForFirstWrite()
{
	SoundQueue queue;
	std::atomic<bool> sawData{false};

	std::thread reader([&]() { sawData = queue.WaitForFirstWrite(); });

	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	int16 chunk[880] = {0};
	queue.Write(chunk, 880, 48000);

	reader.join();
	CHECK(sawData.load());
	CHECK(queue.GetSampleRate() == 48000);

	// Data should still be there -- WaitForFirstWrite() must not consume.
	int16 buf[4096];
	size_t got = queue.WaitAndRead(buf, 4096);
	CHECK(got == 880);
}

} // namespace

int main()
{
	TestBasicRoundtrip();
	TestBackpressureThrottlesProducer();
	TestShutdownUnblocksWaitingReader();
	TestWriteAfterShutdownIsNoop();
	TestWaitForFirstWrite();

	if(g_failures == 0)
	{
		std::printf("All SoundQueue tests passed.\n");
		return 0;
	}
	std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
	return 1;
}
