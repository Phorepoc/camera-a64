/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: Apache-2.0 */

// phecamsource.h -- the pixel source of the camera-a64 ECam plug-in: the camera server (phecamserver.exe, the Linux
// camera stack of this package) through RPheCamera. Opens camera `aUnit` (0 rear OV5640, 1 front GC2145) in a small
// viewfinder mode, streams, and keeps the latest frame converted to an EColor16MU CFbsBitmap, turned upright by the
// sensor's rotation control, that the plug-in draws where the template plug-in drew its test square. A still in
// another mode switches the sensor the Megapixels way (io_pipeline.c capture()): stop, the capture mode, start, the
// first non-blank frame, stop, the viewfinder mode again. The V4L2 controls of the sensor pass straight through.
#ifndef PHECAMSOURCE_H
#define PHECAMSOURCE_H

#include <e32base.h>
#include <fbs.h>
#include "phecam_api.h"

class CPheCamSource : public CActive
	{
public:
	static CPheCamSource* NewL(TInt aUnit = 0);
	~CPheCamSource();
	TInt Start();					// the viewfinder stream (once): the sensor is powered while it runs
	TBool Streaming() const { return iStarted; }
	CFbsBitmap* Frame() const { return iFrames > 0 ? iBitmap : NULL; }	// NULL until the first frame
	TInt FrameCount() const { return iFrames; }
	// the sensor's frame sizes (VIDIOC_SUBDEV_ENUM_FRAME_SIZE) in its list order, turned upright (sides swapped at 90/270)
	const RArray<TSize>& StillSizes() const { return iUprightSizes; }
	// a still of StillSizes()[aIndex], upright (and mirrored left-right if aMirror), as a new EColor16MU bitmap the caller owns
	CFbsBitmap* CaptureStillL(TInt aIndex, TBool aMirror);
	void SetMirror(TBool aMirror) { iMirror = aMirror; }
	TInt SetCtrl(TUint32 aId, TInt aValue) { return iCam.SetCtrl(iUnit, aId, aValue); }
	TInt GetCtrl(TUint32 aId, TInt& aValue) { return iCam.GetCtrl(iUnit, aId, aValue); }
	TInt QueryCtrl(TUint32 aId, TPheCamCtrlInfo& aInfo) { return iCam.QueryCtrl(iUnit, aId, aInfo); }
	TInt MenuValue(TUint32 aId, TInt aIndex, TInt& aValue) { return iCam.MenuValue(iUnit, aId, aIndex, aValue); }
	const TPheCamFormat& Format() const { return iFmt; }
	TInt Unit() const { return iUnit; }
private:
	CPheCamSource() : CActive(EPriorityLow), iBuf(NULL, 0) { CActiveScheduler::Add(this); }
	void ConstructL(TInt aUnit);
	void RunL();
	void DoCancel();
	void Next();
	TInt StartViewFinder();
	static void Convert(const TUint8* aUyvy, const TPheCamFormat& aFmt, CFbsBitmap& aOut, TBool aMirror);
	static void ConvertBggr(const TUint8* aRaw, const TPheCamFormat& aFmt, CFbsBitmap& aOut, TBool aMirror);
	TBool RawStill(const TSize& aSensorSize) const;

	RPheCamera iCam;
	TInt iUnit;
	TPheCamFormat iFmt;
	TSize iVfSize;
	RArray<TSize> iSensorSizes, iUprightSizes;
	HBufC8* iFrame;
	TPtr8 iBuf;
	CFbsBitmap* iBitmap;
	TInt iFrames, iLoggedErr;
	TBool iConnected, iStarted, iMirror;
	};

#endif
