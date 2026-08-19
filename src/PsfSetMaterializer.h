#pragma once

#include "filesystem_def.h"

class VFSFile;

// CPsfLoader::LoadPsf() (the only public entry point into Play!'s PSF
// loading) always constructs its own CPsfStreamProvider internally --
// CPhysicalPsfStreamProvider for real files, CArchivePsfStreamProvider for
// zip/rar archives -- and the per-format recursion that actually resolves
// _lib/_lib2..N sibling references is private to CPsfLoader. There is no
// way to plug in a VFSFile-backed CPsfStreamProvider without patching
// PsfLoader.h, which this project is currently avoiding (see project
// memory on deferred local patches).
//
// So instead of implementing CPsfStreamProvider, this materialises the
// main file and everything it transitively references (via its own
// _lib/_lib2..N tags, read the same way CPsfBase itself would) into a
// fresh temp directory through VFSFile, preserving each file's original
// basename -- then CPhysicalPsfStreamProvider's ordinary directory+
// basename sibling resolution works against that directory completely
// unmodified.
//
// The temp directory and its contents are removed when the object is
// destroyed. Throws std::runtime_error (from a failed VFS read, or a
// _lib chain deeper than kMaxDepth) on failure; the temp directory is
// still cleaned up in that case.
class PsfSetMaterializer
{
public:
	// mainUri: the Audacious VFS URI of the file passed to play().
	// mainFile: the already-open handle Audacious passed alongside it --
	// reused here to avoid a redundant VFS open of the main file.
	PsfSetMaterializer(const char* mainUri, VFSFile& mainFile);
	~PsfSetMaterializer();

	PsfSetMaterializer(const PsfSetMaterializer&) = delete;
	PsfSetMaterializer& operator=(const PsfSetMaterializer&) = delete;

	// Absolute filesystem path to the materialised copy of the main file.
	const fs::path& GetMainFilePath() const { return m_mainFilePath; }

private:
	static constexpr int kMaxDepth = 64;

	void MaterializeOne(const std::string& uri, VFSFile* alreadyOpenMainFile, int depth);
	static std::string GetBasename(const std::string& uri);
	static std::string GetSiblingUri(const std::string& uri, const std::string& siblingName);

	fs::path m_tempDir;
	fs::path m_mainFilePath;
};
