#pragma once

#include "PsfVm.h"
#include "PsfBase.h"
#include "filesystem_def.h"

#include "SoundQueue.h"

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

	// Loads mainFilePath (a real filesystem path -- see PsfSetMaterializer,
	// since CPsfLoader::LoadPsf() offers no VFS-backed loading path) and
	// starts emulation. May be called again on the same session to restart
	// from zero (eg the reverse-seek pattern the existing plug-ins use, since
	// no PSF engine here exposes real seek-to-time).
	//
	// Throws std::runtime_error on a malformed/unsupported file -- safe to
	// catch here. See Load()'s definition for why this is only true
	// because of the specific Pause()-then-direct-call ordering used, not
	// in general for anything touching CPsfVm.
	CPsfBase::TagMap Load(const fs::path& mainFilePath);

	// Shuts down the sound queue (waking any blocked consumer) and pauses
	// the VM. Safe to call from any thread; idempotent; safe to call
	// before Load() has ever succeeded.
	void Stop();

	void SetVolumeAdjust(float volumeAdjust);

	// Valid for the lifetime of this VmSession. Do not retain past it.
	SoundQueue& GetQueue() { return *m_soundQueue; }

private:
	CPsfVm m_vm;

	// Created via the factory passed to CPsfVm::SetSpuHandler() in the
	// constructor. CPsfVm -- not VmSession -- owns and eventually deletes
	// it (SetSpuHandlerImpl() on replacement, or ThreadProc()'s own
	// clean-up on CPsfVm destruction; see project memory). This is a
	// borrowed pointer for the consumer side only: never delete it here,
	// and never touch it once this VmSession is being destroyed.
	SoundQueue* m_soundQueue = nullptr;
};
