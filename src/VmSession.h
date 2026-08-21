#pragma once

#include "PsfVm.h"
#include "PsfBase.h"
#include "filesystem_def.h"

#include "SoundQueue.h"

struct VFSFile;

// Owns one CPsfVm for the lifetime of a single InputPlugin::play() call
// (per project memory's bridge design, point 1: one CPsfVm per play()).
// Wraps the load/reset/resume sequence and the CSoundHandler that bridges
// CPsfVm's own push-model background thread to the SoundQueue our play()
// loop pulls from.
class VmSession
{
public:
	VmSession();
	~VmSession();

	VmSession(const VmSession&) = delete;
	VmSession& operator=(const VmSession&) = delete;

	// Loads mainFilePath using the VFS interface
	CPsfBase::TagMap Load(const std::string& uri, VFSFile *file);

	// Shuts down the sound queue (waking any blocked consumer) and pauses
	// the VM. Safe to call from any thread; idempotent; safe to call
	// before Load() has ever succeeded.
	void Stop();

	void SetVolumeAdjust(float volumeAdjust);

	// Valid for the lifetime of this VmSession. Do not retain past it.
	SoundQueue& GetQueue() { return *m_soundQueue; }

private:
	CPsfVm m_vm;
	Framework::CSignal<void()>::Connection m_onFaultConnection;

	// Created via the factory passed to CPsfVm::SetSpuHandler() in the
	// constructor. CPsfVm -- not VmSession -- owns and eventually deletes
	// it (SetSpuHandlerImpl() on replacement, or ThreadProc()'s own
	// clean-up on CPsfVm destruction; see project memory). This is a
	// borrowed pointer for the consumer side only: never delete it here,
	// and never touch it once this VmSession is being destroyed.
	SoundQueue* m_soundQueue = nullptr;
};
