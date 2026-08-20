#pragma once

#include "PsfStreamProvider.h"

struct VFSFile;

class CVFSPsfStreamProvider : public CPsfStreamProvider
{
	std::string uri;
	VFSFile *alreadyOpenMainFile;

public:
	CVFSPsfStreamProvider(const std::string& uri_, VFSFile *alreadyOpenMainFile_);
	virtual ~CVFSPsfStreamProvider();

	static CPsfPathToken GetPathTokenFromFilePath(const std::string& filePath);
	static std::string GetFilePathFromPathToken(const CPsfPathToken&);

	Framework::CStream* GetStreamForPath(const CPsfPathToken&) override;
	CPsfPathToken GetSiblingPath(const CPsfPathToken&, const std::string&) override;
};

struct VFSFile;
std::unique_ptr<CPsfStreamProvider> CreateVFSPsfStreamProvider(const std::string& uri, VFSFile *alreadyOpenMainFile);
