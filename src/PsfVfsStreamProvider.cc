#include <cassert>
#include <algorithm>
#include "PsfVfsStreamProvider.h"
#include "MemStream.h"
#include "StdStreamUtils.h"

#include <libaudcore/vfs.h>
#include <libaudcore/index.h>

std::unique_ptr<CPsfStreamProvider> CreateVFSPsfStreamProvider(const std::string& uri, VFSFile *alreadyOpenMainFile)
{
	return std::unique_ptr<CPsfStreamProvider>(new CVFSPsfStreamProvider(uri, alreadyOpenMainFile));
}

//CVFSPsfStreamProvider
//----------------------------------------------------------
CVFSPsfStreamProvider::CVFSPsfStreamProvider(const std::string& uri_, VFSFile *alreadyOpenMainFile_)
: uri(uri_), alreadyOpenMainFile(alreadyOpenMainFile_)
{
	std::replace(uri.begin(), uri.end(), '\\', '/');
}

CVFSPsfStreamProvider::~CVFSPsfStreamProvider()
{
}

CPsfPathToken CVFSPsfStreamProvider::GetPathTokenFromFilePath(const std::string& filePath)
{
	return CPsfPathToken::WidenString(filePath);
}

std::string CVFSPsfStreamProvider::GetFilePathFromPathToken(const CPsfPathToken& pathToken)
{
	return pathToken.GetNarrowPath();
}

CPsfPathToken CVFSPsfStreamProvider::GetSiblingPath(const CPsfPathToken& pathToken, const std::string& siblingName)
{
	auto pathString = pathToken.GetWidePath();
	auto slashPos = pathString.find_last_of(L"/");
	if(slashPos == std::wstring::npos)
	{
		slashPos = pathString.find_last_of(L"\\");
		if(slashPos == std::wstring::npos)
		{
			slashPos = -1;
		}
	}
	slashPos++;
	auto stem = std::wstring(std::begin(pathString), std::begin(pathString) + slashPos);
	auto result = stem + CPsfPathToken::WidenString(siblingName);
	return result;
}

Framework::CStream* CVFSPsfStreamProvider::GetStreamForPath(const CPsfPathToken& pathToken)
{
	auto pathString = GetFilePathFromPathToken(pathToken);
	std::replace(pathString.begin(), pathString.end(), '\\', '/');

	Index<char> data;
	if(alreadyOpenMainFile && pathString == uri)
	{
		alreadyOpenMainFile->fseek(0, VFS_SEEK_SET);
		data = alreadyOpenMainFile->read_all();
	}
	else
	{
		VFSFile file(pathString.c_str(), "r");
		if(!file)
			throw std::runtime_error("Couldn't open PSF file: " + pathString);
		data = file.read_all();
	}

	auto result = new Framework::CMemStream();
	result->Allocate(static_cast<unsigned int>(data.len()));
	std::copy(data.begin(), data.end(), result->GetBuffer());
	return result;
}
