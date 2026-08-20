#include "PsfLoaderCustom.h"
#include "PsfVfsStreamProvider.h"

void CPsfLoaderCustom::LoadPsfCustom(CPsfVm& virtualMachine, const std::string& uri, VFSFile *file, CPsfBase::TagMap* tags)
{
	auto streamProvider = CreateVFSPsfStreamProvider(uri, file);

    auto filePath = CVFSPsfStreamProvider::GetPathTokenFromFilePath(uri);

   	Framework::CStream* input(streamProvider->GetStreamForPath(filePath));
	CPsfBase psfFile(*input);
	delete input;

	if(psfFile.GetVersion() == CPsfBase::VERSION_PLAYSTATION2)
	{
		LoadPs2(virtualMachine, filePath, streamProvider.get(), tags);
	}
	else if(psfFile.GetVersion() == CPsfBase::VERSION_PLAYSTATIONPORTABLE)
	{
		LoadPsp(virtualMachine, filePath, streamProvider.get(), tags);
	}
	else
	{
		LoadPsx(virtualMachine, filePath, streamProvider.get(), tags);
	}
}
