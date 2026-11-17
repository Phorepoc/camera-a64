// Copyright The Phorepoc Authors
// SPDX-License-Identifier: GPL-2.0-only
//
// phecamserver.cpp -- the camera server: the Linux camera stack (the vendored sun6i-csi, ov5640, gc2145 and i2c-gpio
// drivers with the V4L2 / videobuf2 / media-controller core, over the shim) in a user-side process. Its hardware is
// reached through the image's thin "CamHw.A64" channel (phe_user.cpp); its client is the ECam plug-in (phecam_api.h).
//
// Threads: the server's own (the active scheduler, IPC) and one worker that makes every call into the Linux stack
// (probe at start-up, configure, start, stop, and the dequeue loop while streaming), so the Linux code only ever runs
// on one thread besides the interrupt thread (phe_user.cpp). A session's request is handed to the worker as a command
// and the server thread waits for it; a pending EPheCamNextFrame is completed by the worker when a frame is dequeued.
// The process exits when its last session closes.

#include <e32base.h>
#include <e32debug.h>
#include "phecam_api.h"
#include "phe_camera.h"

TInt PheHwOpen();		// phe_user.cpp

// V4L2 / media-bus codes (videodev2.h, media-bus-format.h)
static const TUint32 KPixFmtUyvy = 0x59565955;		// v4l2_fourcc('U','Y','V','Y')
static const TUint32 KMbusUyvy8_2x8 = 0x2006;		// MEDIA_BUS_FMT_UYVY8_2X8
static const TInt KBuffers = 3;

// ---- the worker ----
class TWorker
	{
public:
	enum TCmd { ENone, EProbe, EConfigure, EStart, EStop, EQuit };
	TInt Run(TCmd aCmd);			// server thread: run a command on the worker, wait for its result
	static TInt ThreadFn(TAny* aSelf) { return ((TWorker*)aSelf)->Loop(); }
	TInt Loop();
	// command arguments / results
	TInt iUnit, iWidth, iHeight;
	TPheCamFormat iFormat;
	// frame hand-over
	RFastLock iLock;
	RMessage2 iFrameMsg;
	TBool iFrameWanted;
	RSemaphore iCmdSem, iDoneSem;
	TCmd iCmd;
	volatile TBool iCmdPending;	// set by Run() before it signals iCmdSem
	TInt iResult;
	TBool iStreaming;
	TUint8* iFrame;
	TInt iFrameSize;
	};

TInt TWorker::Run(TCmd aCmd)
	{
	iCmd = aCmd;
	iCmdPending = ETrue;
	iCmdSem.Signal();
	iDoneSem.Wait();
	return iResult;
	}

TInt TWorker::Loop()
	{
	for (;;)
		{
		if (iStreaming)
			{
			// no command pending: one frame. (A flag, not iCmdSem.Wait(0): an EKA2 semaphore timeout of 0 means "no timeout",
			// so the worker blocked there for good and never dequeued a frame.)
			if (!iCmdPending)
				{
				TInt n = phe_camera_next_frame(iFrame, iFrameSize);
				iLock.Wait();
				if (iFrameWanted)
					{
					iFrameWanted = EFalse;
					TInt r = n < 0 ? KErrGeneral : iFrameMsg.Write(0, TPtrC8(iFrame, n));
					iFrameMsg.Complete(r == KErrNone ? n : r);
					}
				iLock.Signal();
				continue;
				}
			iCmdSem.Wait();
			}
		else
			iCmdSem.Wait();
		iCmdPending = EFalse;
		TInt r = KErrNone;
		switch (iCmd)
			{
			case EProbe:
				r = PheHwOpen();
				if (r == KErrNone)
					r = phe_camera_init() < 0 ? KErrNotFound : KErrNone;
				break;
			case EConfigure:
				{
				unsigned int bpl = 0, size = 0;
				r = phe_camera_configure(iUnit, iWidth, iHeight, KPixFmtUyvy, KMbusUyvy8_2x8, &bpl, &size);
				if (r == 0)
					{
					User::Free(iFrame);
					iFrame = (TUint8*)User::Alloc(size);
					iFrameSize = size;
					iFormat.iWidth = iWidth;
					iFormat.iHeight = iHeight;
					iFormat.iBytesPerLine = bpl;
					iFormat.iSizeImage = size;
					r = iFrame ? KErrNone : KErrNoMemory;
					}
				else
					r = KErrNotSupported;
				break;
				}
			case EStart:
				r = !iFrame ? KErrNotReady : phe_camera_start(KBuffers) < 0 ? KErrGeneral : KErrNone;
				iStreaming = (r == KErrNone);
				break;
			case EStop:
				iStreaming = EFalse;
				phe_camera_stop();
				break;
			case EQuit:
				iStreaming = EFalse;
				phe_camera_stop();
				iResult = KErrNone;
				iDoneSem.Signal();
				return KErrNone;
			default:
				break;
			}
		iResult = r;
		iDoneSem.Signal();
		}
	}

static TWorker Worker;

// ---- the server ----
class CPheCamServer : public CServer2
	{
public:
	CPheCamServer() : CServer2(EPriorityStandard, ESharableSessions) {}
	CSession2* NewSessionL(const TVersion& aVersion, const RMessage2& aMessage) const;
	void SessionClosed() { if (--iSessions == 0) CActiveScheduler::Stop(); }
	mutable TInt iSessions;
	};

class CPheCamSession : public CSession2
	{
public:
	~CPheCamSession();
	void ServiceL(const RMessage2& aMessage);
	};

CSession2* CPheCamServer::NewSessionL(const TVersion& aVersion, const RMessage2&) const
	{
	if (!User::QueryVersionSupported(TVersion(1, 0, 0), aVersion))
		User::Leave(KErrNotSupported);
	if (iSessions)
		User::Leave(KErrInUse);		// one camera client at a time
	CSession2* s = new (ELeave) CPheCamSession;
	iSessions++;
	return s;
	}

CPheCamSession::~CPheCamSession()
	{
	Worker.iLock.Wait();
	if (Worker.iFrameWanted)
		{
		Worker.iFrameWanted = EFalse;
		Worker.iFrameMsg.Complete(KErrCancel);
		}
	Worker.iLock.Signal();
	Worker.Run(TWorker::EStop);
	((CPheCamServer*)Server())->SessionClosed();
	}

void CPheCamSession::ServiceL(const RMessage2& aMessage)
	{
	switch (aMessage.Function())
		{
		case EPheCamCount:
			aMessage.Complete(phe_camera_count());		// read-only after the probe
			return;
		case EPheCamConfigure:
			{
			Worker.iUnit = aMessage.Int0();
			Worker.iWidth = aMessage.Int1();
			Worker.iHeight = aMessage.Int2();
			TInt r = Worker.Run(TWorker::EConfigure);
			if (r == KErrNone)
				{
				TPckgC<TPheCamFormat> f(Worker.iFormat);
				r = aMessage.Write(3, f);
				}
			aMessage.Complete(r);
			return;
			}
		case EPheCamStart: aMessage.Complete(Worker.Run(TWorker::EStart)); return;
		case EPheCamStop: aMessage.Complete(Worker.Run(TWorker::EStop)); return;
		case EPheCamNextFrame:
			Worker.iLock.Wait();
			if (Worker.iFrameWanted)
				{
				Worker.iLock.Signal();
				aMessage.Complete(KErrInUse);
				return;
				}
			Worker.iFrameMsg = aMessage;
			Worker.iFrameWanted = ETrue;
			Worker.iLock.Signal();
			return;
		case EPheCamNextFrameCancel:
			Worker.iLock.Wait();
			if (Worker.iFrameWanted)
				{
				Worker.iFrameWanted = EFalse;
				Worker.iFrameMsg.Complete(KErrCancel);
				}
			Worker.iLock.Signal();
			aMessage.Complete(KErrNone);
			return;
		default:
			aMessage.Panic(_L("PheCamera"), 1);
		}
	}

static TInt RunServerL()
	{
	User::LeaveIfError(Worker.iLock.CreateLocal());
	User::LeaveIfError(Worker.iCmdSem.CreateLocal(0));
	User::LeaveIfError(Worker.iDoneSem.CreateLocal(0));
	RThread t;
	User::LeaveIfError(t.Create(_L("CamWorker"), TWorker::ThreadFn, 0x8000, NULL, &Worker));	// NULL heap: shared
	t.SetPriority(EPriorityMore);
	t.Resume();
	TInt r = Worker.Run(TWorker::EProbe);
	RDebug::Printf("PH: camera-a64: probe %d, %d camera(s)", r, r == KErrNone ? phe_camera_count() : 0);
	User::LeaveIfError(r);

	CActiveScheduler* as = new (ELeave) CActiveScheduler;
	CleanupStack::PushL(as);
	CActiveScheduler::Install(as);
	CPheCamServer* server = new (ELeave) CPheCamServer;
	CleanupStack::PushL(server);
	User::LeaveIfError(server->Start(KPheCamServerName));
	RProcess::Rendezvous(KErrNone);
	CActiveScheduler::Start();
	CleanupStack::PopAndDestroy(2, as);
	Worker.Run(TWorker::EQuit);
	t.Close();
	return KErrNone;
	}

GLDEF_C TInt E32Main()
	{
	CTrapCleanup* cleanup = CTrapCleanup::New();
	TRAPD(r, RunServerL());
	if (r != KErrNone)
		RProcess::Rendezvous(r);
	delete cleanup;
	return r;
	}
