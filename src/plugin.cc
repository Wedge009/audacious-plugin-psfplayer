#include <cstring>
#include <stdexcept>

#include <libaudcore/plugin.h>
#include <libaudcore/preferences.h>
#include <libaudcore/runtime.h>

#include "PsfBase.h"
#include "PsfTags.h"
#include "MemStream.h"

#include "PsfSetMaterializer.h"
#include "VmSession.h"
#include "PlaybackTimer.h"

// CAppConfig::GetBasePath() is deliberately left undefined by Play!
// itself -- DefaultAppConfig.h's own comment says to include it in the
// app's main.cpp so each frontend can choose where its data lives.
// CIoman's constructor (built unconditionally into every PS2 PSF2 load,
// for its "preference directory" ioman device) references it
// unconditionally, so without this the plug-in .so has an unresolved
// symbol and can fail at dlopen() time for any PS2 file, even though
// normal PSF2 audio decode never legitimately exercises that device.
// Play!'s own default (a "Play Data Files" folder under the user's data
// directory) is a fine, harmless choice here too -- reusing it rather
// than inventing a different location for what's effectively dead code
// in this plug-in's actual usage.
#include "app_shared/DefaultAppConfig.h"

// EXPORT/PACKAGE/N_()/_() are ordinarily supplied by a plug-in's own
// generated config.h (see eg audacious-plugin-highly-advanced's
// meson.build) alongside real HAVE_GETTEXT/po/ setup. Neither exists yet
// in this project (see project memory: build system decided as plain
// CMake, i18n not yet set up), so this defines just the one symbol-
// visibility macro actually needed and uses plain string literals rather
// than i18n.h's N_()/_() -- pulling in i18n.h without HAVE_GETTEXT defined
// macro-expands dgettext/ngettext/dngettext over the real declarations in
// <libintl.h>, which breaks the build the moment anything transitively
// includes it.
#define EXPORT __attribute__((visibility("default")))

namespace
{

// wchar_t is UTF-32 on this target (TARGET_PLATFORM_UNIX), so this is a
// direct per-codepoint UTF-8 encode -- no charset-name/iconv guesswork.
std::string WStringToUtf8(const std::wstring& in)
{
	std::string out;
	out.reserve(in.size());
	for(wchar_t wc : in)
	{
		uint32_t cp = static_cast<uint32_t>(wc);
		if(cp <= 0x7F)
		{
			out += static_cast<char>(cp);
		}
		else if(cp <= 0x7FF)
		{
			out += static_cast<char>(0xC0 | (cp >> 6));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
		else if(cp <= 0xFFFF)
		{
			out += static_cast<char>(0xE0 | (cp >> 12));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
		else
		{
			out += static_cast<char>(0xF0 | (cp >> 18));
			out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
	}
	return out;
}

bool ProbeSignature(const char* magic, int64_t len)
{
	if(len < 4)
		return false;
	return !memcmp(magic, "PSF\x01", 4) || !memcmp(magic, "PSF\x02", 4);
}

// Reads the top-level file's own tags only. title/artist/game/length/fade
// etc. are always on the top-level file by PSF convention, never on a
// _lib sibling -- so unlike play(), this never needs the _lib chain, VFS
// materialization, or a real CPsfVm. Throws std::runtime_error (from
// CPsfBase's constructor) if the signature doesn't match.
CPsfBase::TagMap ReadTopLevelTags(const Index<char>& data)
{
	Framework::CMemStream stream;
	stream.Allocate(static_cast<unsigned int>(data.len()));
	memcpy(stream.GetBuffer(), data.begin(), static_cast<size_t>(data.len()));

	CPsfBase psfFile(stream);
	CPsfBase::TagMap tags;
	tags.insert(psfFile.GetTagsBegin(), psfFile.GetTagsEnd());
	return tags;
}

} // namespace

class PsfPlayerPlugin : public InputPlugin
{
public:
	static const char* const exts[];
	static const char* const defaults[];
	static const PreferencesWidget widgets[];
	static const PluginPreferences prefs;

	static constexpr PluginInfo info = {"Play! PSF/PSF2 Decoder", nullptr, nullptr, &prefs};

	constexpr PsfPlayerPlugin() : InputPlugin(info, InputInfo().with_exts(exts)) {}

	bool init() override;

	bool is_our_file(const char* filename, VFSFile& file) override;
	bool read_tag(const char* filename, VFSFile& file, Tuple& tuple, Index<char>* image) override;
	bool play(const char* filename, VFSFile& file) override;
};

EXPORT PsfPlayerPlugin aud_plugin_instance;

const char* const PsfPlayerPlugin::defaults[] = {"ignore_length", "FALSE", nullptr};

bool PsfPlayerPlugin::init()
{
	aud_config_set_defaults("psfplayer", defaults);
	return true;
}

bool PsfPlayerPlugin::is_our_file(const char* filename, VFSFile& file)
{
	char magic[4];
	if(file.fread(magic, 1, 4) < 4)
		return false;
	return ProbeSignature(magic, 4);
}

bool PsfPlayerPlugin::read_tag(const char* filename, VFSFile& file, Tuple& tuple, Index<char>* image)
{
	Index<char> buf = file.read_all();
	if(!buf.len())
		return false;

	CPsfBase::TagMap rawTags;
	try
	{
		rawTags = ReadTopLevelTags(buf);
	}
	catch(const std::exception&)
	{
		return false;
	}

	CPsfTags tags(rawTags);

	if(tags.HasTag("title"))
		tuple.set_str(Tuple::Title, WStringToUtf8(tags.GetTagValue("title")).c_str());
	if(tags.HasTag("artist"))
		tuple.set_str(Tuple::Artist, WStringToUtf8(tags.GetTagValue("artist")).c_str());
	if(tags.HasTag("game"))
		tuple.set_str(Tuple::Album, WStringToUtf8(tags.GetTagValue("game")).c_str());
	if(tags.HasTag("copyright"))
		tuple.set_str(Tuple::Copyright, WStringToUtf8(tags.GetTagValue("copyright")).c_str());

	double lengthSeconds = 60.0;
	if(tags.HasTag("length"))
		lengthSeconds = CPsfTags::ConvertTimeString(tags.GetTagValue("length").c_str());
	double fadeSeconds = 10.0;
	if(tags.HasTag("fade"))
		fadeSeconds = CPsfTags::ConvertTimeString(tags.GetTagValue("fade").c_str());
	tuple.set_int(Tuple::Length, static_cast<int>((lengthSeconds + fadeSeconds) * 1000.0));

	tuple.set_str(Tuple::Codec, "PlayStation 1/2 Audio (Play!)");
	tuple.set_str(Tuple::Quality, "sequenced");
	tuple.set_int(Tuple::Channels, 2);

	return true;
}

bool PsfPlayerPlugin::play(const char* filename, VFSFile& file)
{
	bool ignoreLength = aud_get_bool("psfplayer", "ignore_length");

	try
	{
		// LoadPsf()'s only public entry point always builds its own
		// physical/archive stream provider internally and offers no way
		// to inject a VFS-backed one (the per-format recursion that would
		// need it is private -- see project memory). So the main file and
		// everything its _lib/_lib2.. chain transitively references gets
		// materialised to a real temp directory first, then handed to the
		// stock physical loader unmodified.
		PsfSetMaterializer materializer(filename, file);

		VmSession session;
		CPsfBase::TagMap tags = session.Load(materializer.GetMainFilePath());

		SoundQueue& queue = session.GetQueue();

		if(!queue.WaitForFirstWrite())
		{
			AUDERR("PSF playback produced no audio: %s\n", filename);
			return false;
		}

		unsigned int sampleRate = queue.GetSampleRate();
		set_stream_bitrate(static_cast<int>(sampleRate) * 2 * 2 * 8);
		open_audio(FMT_S16_NE, static_cast<int>(sampleRate), 2);

		PlaybackTimer timer(tags, sampleRate, ignoreLength);

		static constexpr size_t kReadChunkSamples = 4096; // interleaved-stereo int16 values
		int16 buffer[kReadChunkSamples];

		uint64_t elapsedFrames = 0;
		uint64_t targetFrames = 0; // > elapsedFrames while fast-forwarding past a seek

		while(!check_stop())
		{
			int seekMs = check_seek();
			if(seekMs >= 0)
			{
				// No PSF engine here exposes real seek-to-time -- restart from
				// zero and fast-forward internally, discarding audio (but
				// still advancing the fade timer) until reaching the target.
				tags = session.Load(materializer.GetMainFilePath());
				timer = PlaybackTimer(tags, sampleRate, ignoreLength);
				elapsedFrames = 0;
				targetFrames = static_cast<uint64_t>(seekMs) * sampleRate / 1000;
				continue;
			}

			size_t got = queue.WaitAndRead(buffer, kReadChunkSamples);
			if(got == 0)
				break; // shut-down with nothing left queued

			size_t frames = got / 2;
			elapsedFrames += frames;
			float volume = timer.Advance(frames);

			if(elapsedFrames >= targetFrames)
			{
				write_audio(buffer, static_cast<int>(got) * static_cast<int>(sizeof(int16)));
				session.SetVolumeAdjust(volume);
			}

			if(timer.IsDone())
				break;
		}

		session.Stop();
		return true;
	}
	catch(const std::exception& e)
	{
		AUDERR("PSF playback failed for %s: %s\n", filename, e.what());
		return false;
	}
}

// PSP support (CPsfLoader::LoadPsf() dispatches it via a trailing 'p' in
// the path) is deliberately out of scope -- this project's mandate (see
// README) is PSF/PSF2 (PS1/PS2), and PSP's own extension convention
// wasn't worth guessing at without a real PSP-format sample to verify
// against. SPU/SPX (Highly Experimental-specific modes OpenPSF also
// handles) aren't a thing Play! implements at all.
const char* const PsfPlayerPlugin::exts[] = {"psf", "minipsf", "psf2", "minipsf2", nullptr};

const PreferencesWidget PsfPlayerPlugin::widgets[] = {
    WidgetLabel("<b>Play! PSF/PSF2 Configuration</b>"),
    WidgetCheck("Ignore length from file", WidgetBool("psfplayer", "ignore_length")),
};

const PluginPreferences PsfPlayerPlugin::prefs = {{widgets}};
