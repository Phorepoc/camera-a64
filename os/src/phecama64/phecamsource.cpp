// Copyright The Phorepoc Authors
// SPDX-License-Identifier: Apache-2.0

// phecamsource.cpp -- see phecamsource.h.
#include <e32debug.h>
#include "phecamsource.h"

const TInt KCamW = 640, KCamH = 480;

CPheCamSource* CPheCamSource::NewL(TInt aUnit)
	{
	CPheCamSource* s = new (ELeave) CPheCamSource;
	CleanupStack::PushL(s);
	s->ConstructL(aUnit);
	CleanupStack::Pop(s);
	return s;
	}

CPheCamSource::~CPheCamSource()
	{
	Cancel();
	if (iStarted)
		iCam.Stop();
	if (iConnected)
		iCam.Close();
	delete iFrame;
	delete iBitmap;
	}

void CPheCamSource::ConstructL(TInt aUnit)
	{
	User::LeaveIfError(iCam.Connect());
	iConnected = ETrue;
	TInt r = iCam.Configure(aUnit, KCamW, KCamH, iFmt);
	RDebug::Printf("PH: camera-a64: plug-in configure unit %d -> %d (%dx%d, %d bytes/line)", aUnit, r, iFmt.iWidth, iFmt.iHeight,
		iFmt.iBytesPerLine);
	User::LeaveIfError(r);
	iFrame = HBufC8::NewL(iFmt.iSizeImage);
	iBuf.Set(iFrame->Des());
	iBitmap = new (ELeave) CFbsBitmap;
	// the sensors are mounted landscape (DT rotation 90/270): the bitmap is the frame turned to portrait
	User::LeaveIfError(iBitmap->Create(TSize(iFmt.iHeight, iFmt.iWidth), EColor16MU));
	User::LeaveIfError(iCam.Start());
	iStarted = ETrue;
	Next();
	}

void CPheCamSource::Next()
	{
	iBuf.SetLength(0);
	iCam.NextFrame(iBuf, iStatus);
	SetActive();
	}

void CPheCamSource::DoCancel()
	{
	iCam.NextFrameCancel();
	}

void CPheCamSource::RunL()
	{
	const TInt n = iStatus.Int();
	if (n >= iFmt.iBytesPerLine * iFmt.iHeight)
		Convert(iBuf.Ptr());
	else if (n < 0 && iLoggedErr++ < 3)
		RDebug::Printf("PH: camera-a64: plug-in NextFrame -> %d", n);
	if (n != KErrCancel && n != KErrServerTerminated)
		Next();
	}

// packed UYVY (U0 Y0 V0 Y1), BT.601 studio range -> X8R8G8B8, transposed to portrait
void CPheCamSource::Convert(const TUint8* aUyvy)
	{
	const TInt w = iFmt.iWidth, h = iFmt.iHeight;
	iBitmap->LockHeap();
	TUint32* out = (TUint32*)iBitmap->DataAddress();
	const TInt wordsPerLine = iBitmap->DataStride() / 4;
	for (TInt y = 0; y < h; ++y)
		{
		const TUint8* in = aUyvy + y * iFmt.iBytesPerLine;
		for (TInt x = 0; x + 1 < w; x += 2, in += 4)
			{
			const TInt u = in[0] - 128, v = in[2] - 128;
			for (TInt k = 0; k < 2; ++k)
				{
				const TInt c = 298 * ((k ? in[3] : in[1]) - 16);
				TInt r = (c + 409 * v + 128) >> 8, g = (c - 100 * u - 208 * v + 128) >> 8, b = (c + 516 * u + 128) >> 8;
				r = r < 0 ? 0 : r > 255 ? 255 : r;
				g = g < 0 ? 0 : g > 255 ? 255 : g;
				b = b < 0 ? 0 : b > 255 ? 255 : b;
				out[(w - 1 - (x + k)) * wordsPerLine + (h - 1 - y)] = 0xFF000000u | (r << 16) | (g << 8) | b;
				}
			}
		}
	iBitmap->UnlockHeap();
	if (++iFrames <= 3 || iFrames % 50 == 0)
		RDebug::Printf("PH: camera-a64: plug-in frame %d", iFrames);
	}
