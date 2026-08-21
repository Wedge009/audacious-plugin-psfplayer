#include "VmSession.h"

#include "PsfLoaderCustom.h"
#include "PsfVfsStreamProvider.h"

VmSession::VmSession()
{
	// SetSpuHandler() is mailbox-marshalled with waitForCompletion=true, so
	// this blocks until the VM thread has actually run the factory below
	// -- m_soundQueue is guaranteed non-null by the time this constructor
	// returns.
	SoundQueue** outPtr = &m_soundQueue;
	m_vm.SetSpuHandler([outPtr]() -> CSoundHandler* {
		auto* queue = new SoundQueue();
		*outPtr = queue;
		return queue;
	});

	// OnFault fires from CPsfVm's own background thread when Update() threw
	// (malformed/fuzzed guest code tripping a BIOS-level sanity check) --
	// see PsfVm.cpp/project memory. Without this, WaitAndRead() would block
	// for ever: the VM has stopped producing audio for good, but nothing
	// else ever calls Shutdown() to wake a blocked reader. Deliberately not
	// reusing OnRunningStateChange for this -- that also fires on the
	// ordinary Pause() calls Load()/Stop() make, which must NOT shut the
	// queue down (a seek's restart still needs it usable afterwards).
	m_onFaultConnection = m_vm.OnFault.Connect([this]() {
		if(m_soundQueue)
			m_soundQueue->Shutdown();
	});
}

VmSession::~VmSession()
{
	Stop();
	// ~CPsfVm() (via m_vm's own destructor) pauses again (harmless no-op,
	// already paused), joins the VM thread, and deletes m_soundQueue as
	// part of ThreadProc()'s own final clean-up. Not this class's pointer
	// to free.
}

CPsfBase::TagMap VmSession::Load(const std::string& uri, VFSFile *file)
{
	// CPsfVm::Pause() is mailbox-marshalled and blocks until the VM thread
	// is parked in ThreadProc()'s PAUSED branch, which only sleeps and
	// drains the mailbox -- it never touches m_subSystem/m_soundHandler
	// while parked. That guarantee is what makes it safe to call
	// Reset()/LoadPsf() directly here, on OUR OWN thread, rather than
	// wrapping them in another mailbox SendCall() the way CPsfVmJs's
	// ui_js frontend does.
	//
	// That distinction matters: CMailBox::ReceiveCall() has no try/catch
	// around the message it runs (confirmed by reading MailBox.cpp), so a
	// SendCall()-wrapped LoadPsf() that throws -- on a malformed file, or
	// one referencing a missing _lib sibling -- would crash the VM's own
	// thread, and hence the whole host process, uncaught. Calling it
	// directly here, with the VM thread guaranteed idle, keeps the
	// exception on OUR thread where a normal try/catch at the call site
	// works as expected.
	m_vm.Pause();
	m_vm.Reset(); // also clears m_soundQueue via CSoundHandler::Reset()

	CPsfBase::TagMap tags;
	CPsfLoaderCustom::LoadPsfCustom(m_vm, uri, file, &tags);

	m_vm.Resume();
	return tags;
}

void VmSession::Stop()
{
	if(m_soundQueue)
		m_soundQueue->Shutdown();
	m_vm.Pause();
}

void VmSession::SetVolumeAdjust(float volumeAdjust)
{
	m_vm.SetVolumeAdjust(volumeAdjust);
}
