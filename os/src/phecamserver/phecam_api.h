/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: Apache-2.0 */

// phecam_api.h -- the client API of the camera server (phecamserver.exe, this package): what the ECam plug-in
// (phecama64.dll) uses. One client at a time. Frames are packed UYVY (U0 Y0 V0 Y1, 2 bytes per pixel, BT.601),
// `bytesperline` apart, in the sensor's own orientation.
#ifndef PHECAM_API_H
#define PHECAM_API_H
#include <e32std.h>

_LIT(KPheCamServerName, "PheCamera");
_LIT(KPheCamServerExe, "phecamserver.exe");
const TUint KPheCamServerUid = 0xE7015C60;

enum TPheCamOp
	{
	EPheCamCount = 0,		// -> the number of cameras the drivers found
	EPheCamConfigure = 1,		// {unit, width, height} -> TPheCamFormat in slot 3
	EPheCamStart = 2,
	EPheCamStop = 3,
	EPheCamNextFrame = 4,		// async: the next frame into the descriptor in slot 0
	EPheCamNextFrameCancel = 5
	};

struct TPheCamFormat { TInt iWidth, iHeight, iBytesPerLine, iSizeImage; };

#ifndef __KERNEL_MODE__
class RPheCamera : public RSessionBase
	{
public:
	inline TInt Connect()
		{
		TInt r = CreateSession(KPheCamServerName, TVersion(1, 0, 0), 2);
		if (r != KErrNotFound && r != KErrServerTerminated)
			return r;
		RProcess p;
		if ((r = p.Create(KPheCamServerExe, KNullDesC)) != KErrNone)
			return r;
		TRequestStatus s;
		p.Rendezvous(s);
		p.Resume();
		User::WaitForRequest(s);
		r = p.ExitType() == EExitPanic ? KErrGeneral : s.Int();
		p.Close();
		return r == KErrNone ? CreateSession(KPheCamServerName, TVersion(1, 0, 0), 2) : r;
		}
	inline TInt Count() { return SendReceive(EPheCamCount); }
	inline TInt Configure(TInt aUnit, TInt aWidth, TInt aHeight, TPheCamFormat& aFormat)
		{
		TPckg<TPheCamFormat> f(aFormat);
		return SendReceive(EPheCamConfigure, TIpcArgs(aUnit, aWidth, aHeight, &f));
		}
	inline TInt Start() { return SendReceive(EPheCamStart); }
	inline TInt Stop() { return SendReceive(EPheCamStop); }
	inline void NextFrame(TDes8& aFrame, TRequestStatus& aStatus) { SendReceive(EPheCamNextFrame, TIpcArgs(&aFrame), aStatus); }
	inline void NextFrameCancel() { SendReceive(EPheCamNextFrameCancel); }
	};
#endif

#endif
