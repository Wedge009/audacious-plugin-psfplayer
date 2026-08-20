#include "PsfLoader.h"

struct VFSFile;

class CPsfLoaderCustom : public CPsfLoader
{
public:
	static void LoadPsfCustom(CPsfVm&, const std::string& uri, VFSFile *, CPsfBase::TagMap* = NULL);
};
