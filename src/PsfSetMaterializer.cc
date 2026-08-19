#include "PsfSetMaterializer.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <system_error>

#include <libaudcore/vfs.h>
#include <libaudcore/index.h>

#include "PsfBase.h"
#include "MemStream.h"

namespace
{

fs::path MakeTempDir()
{
	std::string tmpl = (fs::temp_directory_path() / "psfplayer-XXXXXX").string();
	if(!mkdtemp(tmpl.data()))
		throw std::runtime_error("Couldn't create a temporary directory for PSF loading.");
	return fs::path(tmpl);
}

void WriteFile(const fs::path& path, const Index<char>& data)
{
	FILE* f = fopen(path.c_str(), "wb");
	if(!f)
		throw std::runtime_error("Couldn't write temporary file: " + path.string());
	size_t written = data.len() ? fwrite(data.begin(), 1, static_cast<size_t>(data.len()), f) : 0;
	fclose(f);
	if(written != static_cast<size_t>(data.len()))
		throw std::runtime_error("Short write to temporary file: " + path.string());
}

} // namespace

PsfSetMaterializer::PsfSetMaterializer(const char* mainUri, VFSFile& mainFile)
{
	m_tempDir = MakeTempDir();
	try
	{
		MaterializeOne(mainUri, &mainFile, 0);
	}
	catch(...)
	{
		std::error_code ec;
		fs::remove_all(m_tempDir, ec);
		throw;
	}
}

PsfSetMaterializer::~PsfSetMaterializer()
{
	std::error_code ec;
	fs::remove_all(m_tempDir, ec);
}

// basename-only by design: this is what keeps a malicious _lib tag value
// (e.g. containing "../../../home/user/.ssh/authorized_keys") from being
// able to WRITE outside m_tempDir -- only the trailing path segment after
// the last '/' is ever used as the on-disk filename, so any directory
// traversal in the tag value only affects where GetSiblingUri() reads
// from next (a read-only concern already inherent to every PSF engine's
// sibling resolution, not something introduced here), never where this
// class writes to.
//
// Known limitation: because the on-disk name is basename-only with no
// preserved directory structure, a _lib chain that spans multiple source
// directories with colliding basenames would overwrite files in the temp
// dir. Real-world PSF sets are always distributed as one flat directory,
// so this hasn't been worth solving.
std::string PsfSetMaterializer::GetBasename(const std::string& uri)
{
	auto slashPos = uri.find_last_of('/');
	return (slashPos == std::string::npos) ? uri : uri.substr(slashPos + 1);
}

std::string PsfSetMaterializer::GetSiblingUri(const std::string& uri, const std::string& siblingName)
{
	auto slashPos = uri.find_last_of('/');
	std::string dir = (slashPos == std::string::npos) ? std::string() : uri.substr(0, slashPos + 1);
	return dir + siblingName;
}

void PsfSetMaterializer::MaterializeOne(const std::string& uri, VFSFile* alreadyOpenMainFile, int depth)
{
	if(depth > kMaxDepth)
		throw std::runtime_error("PSF _lib chain too deep (possible circular reference): " + uri);

	Index<char> data;
	if(alreadyOpenMainFile)
	{
		data = alreadyOpenMainFile->read_all();
	}
	else
	{
		VFSFile file(uri.c_str(), "r");
		if(!file)
			throw std::runtime_error("Couldn't open PSF library file: " + uri);
		data = file.read_all();
	}

	fs::path destPath = m_tempDir / GetBasename(uri);
	WriteFile(destPath, data);
	if(depth == 0)
		m_mainFilePath = destPath;

	// Parse _lib/_lib2.. directly with the same class CPsfLoader uses
	// internally, so this reads identically to how LoadPsf() will read it
	// moments later against the materialised copy.
	Framework::CMemStream stream;
	stream.Allocate(static_cast<unsigned int>(data.len()));
	memcpy(stream.GetBuffer(), data.begin(), static_cast<size_t>(data.len()));

	CPsfBase psfFile(stream);

	if(const char* libPath = psfFile.GetTagValue("_lib"))
	{
		MaterializeOne(GetSiblingUri(uri, libPath), nullptr, depth + 1);
	}

	for(unsigned int n = 2;; n++)
	{
		std::string tagName = "_lib" + std::to_string(n);
		const char* libPath = psfFile.GetTagValue(tagName.c_str());
		if(!libPath)
			break;
		MaterializeOne(GetSiblingUri(uri, libPath), nullptr, depth + 1);
	}
}
