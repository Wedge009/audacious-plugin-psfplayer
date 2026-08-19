// Manual end-to-end smoke test against a REAL PSF/PSF2 file -- exercises
// PsfSetMaterializer -> VmSession -> SoundQueue -> PlaybackTimer exactly
// as plugin.cc's play() does, but standalone (no real Audacious host, no
// InputPlugin::play(), no aud_init()).
//
// This works with a plain VFSFile because libaudcore's local-file
// transport (vfs_local.cc's LocalTransport) is a static global registered
// at library-load time, not something aud_init()/the plug-in registry
// needs to set up first -- confirmed by reading vfs.cc's
// lookup_transport(), which returns it directly for the "file" scheme (or
// no scheme) without ever consulting aud_plugin_list(). So this only
// needs to link against the real libaudcore.so, with zero risk of
// touching a live Audacious instance's config or D-Bus session.
//
// Not run by ctest (add_test()) -- it needs a real PSF/PSF2 file path on
// the command line, and this repository doesn't include any.
//
// Usage: test_integration_manual <path-to-psf-or-psf2-file> [seconds-to-read]

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <libaudcore/vfs.h>

#include "PsfSetMaterializer.h"
#include "VmSession.h"
#include "PlaybackTimer.h"
#include "PsfTags.h"

// See plugin.cc's own comment on this same include -- CIoman's
// constructor references CAppConfig::GetBasePath() unconditionally for
// every PS2 load, and Play! deliberately leaves it undefined for the app
// to provide.
#include "app_shared/DefaultAppConfig.h"

namespace
{

std::string WStringToUtf8(const std::wstring& in)
{
	std::string out;
	for(wchar_t wc : in)
	{
		uint32_t cp = static_cast<uint32_t>(wc);
		if(cp <= 0x7F)
			out += static_cast<char>(cp);
		else if(cp <= 0x7FF)
		{
			out += static_cast<char>(0xC0 | (cp >> 6));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
		else
		{
			out += static_cast<char>(0xE0 | (cp >> 12));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
	}
	return out;
}

void PrintTagIfPresent(const CPsfTags& tags, const char* key)
{
	if(tags.HasTag(key))
		std::printf("  %-10s %s\n", key, WStringToUtf8(tags.GetTagValue(key)).c_str());
}

} // namespace

int main(int argc, char** argv)
{
	if(argc < 2)
	{
		std::fprintf(stderr, "Usage: %s <path-to-psf-or-psf2-file> [seconds-to-read]\n", argv[0]);
		return 2;
	}

	const char* path = argv[1];
	double secondsToRead = (argc >= 3) ? std::atof(argv[2]) : 5.0;

	try
	{
		VFSFile file(path, "r");
		if(!file)
		{
			std::fprintf(stderr, "Couldn't open %s: %s\n", path, file.error() ? file.error() : "?");
			return 1;
		}

		std::printf("Materializing (resolving _lib chain via VFS)...\n");
		PsfSetMaterializer materializer(path, file);
		std::printf("  main file materialised to: %s\n", materializer.GetMainFilePath().c_str());

		std::printf("Loading into a real CPsfVm (Pause -> Reset -> LoadPsf -> Resume)...\n");
		VmSession session;
		CPsfBase::TagMap rawTags = session.Load(materializer.GetMainFilePath());

		CPsfTags tags(rawTags);
		std::printf("Tags read:\n");
		PrintTagIfPresent(tags, "title");
		PrintTagIfPresent(tags, "artist");
		PrintTagIfPresent(tags, "game");
		PrintTagIfPresent(tags, "length");
		PrintTagIfPresent(tags, "fade");

		SoundQueue& queue = session.GetQueue();
		if(!queue.WaitForFirstWrite())
		{
			std::fprintf(stderr, "No audio was ever produced.\n");
			return 1;
		}
		unsigned int sampleRate = queue.GetSampleRate();
		std::printf("First audio produced. Reported sample rate: %u Hz\n", sampleRate);

		PlaybackTimer timer(rawTags, sampleRate, /*ignoreLength=*/false);

		int16 buffer[4096];
		uint64_t totalFrames = 0;
		uint64_t targetFrames = static_cast<uint64_t>(secondsToRead * sampleRate);
		double sumSquares = 0.0;
		int16 peak = 0;

		while(totalFrames < targetFrames)
		{
			size_t got = queue.WaitAndRead(buffer, 4096);
			if(got == 0)
			{
				std::printf("Queue shut down / ended before reaching the requested duration "
				            "(%.1fs read out of %.1fs requested -- track may just be shorter).\n",
				            static_cast<double>(totalFrames) / sampleRate, secondsToRead);
				break;
			}

			for(size_t i = 0; i < got; i++)
			{
				sumSquares += static_cast<double>(buffer[i]) * buffer[i];
				peak = std::max<int16>(peak, static_cast<int16>(std::abs(static_cast<int>(buffer[i]))));
			}

			size_t frames = got / 2;
			totalFrames += frames;
			float volume = timer.Advance(frames);
			session.SetVolumeAdjust(volume);

			if(timer.IsDone())
			{
				std::printf("PlaybackTimer signaled done (length+fade elapsed) at %.1fs.\n",
				            static_cast<double>(totalFrames) / sampleRate);
				break;
			}
		}

		double rms = (totalFrames > 0) ? std::sqrt(sumSquares / (totalFrames * 2)) : 0.0;
		std::printf("Read %.2fs of audio. RMS=%.1f Peak=%d (out of 32767)\n",
		            static_cast<double>(totalFrames) / sampleRate, rms, peak);

		// No PSF engine here exposes real seek-to-time, so plugin.cc's play()
		// restarts the whole session from zero on any seek request. Exercise
		// that same Load()-called-again path here, since nothing above has hit
		// it yet: does a second Pause->Reset->LoadPsf->Resume on the SAME
		// CPsfVm work cleanly, with no leak/hang/corruption from the first
		// load still in progress?
		std::printf("\nRestarting the same session from zero (simulates a seek)...\n");
		session.Load(materializer.GetMainFilePath());
		if(!queue.WaitForFirstWrite())
		{
			std::fprintf(stderr, "Restart produced no audio.\n");
			return 1;
		}
		double restartSumSquares = 0.0;
		uint64_t restartFrames = 0;
		uint64_t restartTarget = sampleRate * 2; // 2s is enough to prove it's alive
		while(restartFrames < restartTarget)
		{
			size_t got = queue.WaitAndRead(buffer, 4096);
			if(got == 0)
				break;
			for(size_t i = 0; i < got; i++)
				restartSumSquares += static_cast<double>(buffer[i]) * buffer[i];
			restartFrames += got / 2;
		}
		double restartRms = (restartFrames > 0) ? std::sqrt(restartSumSquares / (restartFrames * 2)) : 0.0;
		std::printf("Restart produced %.2fs of audio, RMS=%.1f\n",
		            static_cast<double>(restartFrames) / sampleRate, restartRms);
		if(restartRms < 1.0)
		{
			std::fprintf(stderr, "WARNING: restart RMS is near-zero -- restart path may be broken.\n");
			return 1;
		}

		session.Stop();
		std::printf("Stopped cleanly.\n");

		if(rms < 1.0)
		{
			std::fprintf(stderr, "WARNING: RMS is near-zero -- output may be silent.\n");
			return 1;
		}

		return 0;
	}
	catch(const std::exception& e)
	{
		std::fprintf(stderr, "FAILED: %s\n", e.what());
		return 1;
	}
}
