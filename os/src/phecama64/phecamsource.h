/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: Apache-2.0 */

// phecamsource.h -- the pixel source of the camera-a64 ECam plug-in: the camera server (phecamserver.exe, the Linux
// camera stack of this package) through RPheCamera. Opens camera `aUnit` (0 rear OV5640, 1 front GC2145) at 640x480,
// streams, and keeps the latest frame converted to an EColor16MU CFbsBitmap, turned to the screen's portrait
// orientation, that the plug-in draws where the template plug-in drew its test square.
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
	CFbsBitmap* Frame() const { return iFrames > 0 ? iBitmap : NULL; }	// NULL until the first frame
	TInt FrameCount() const { return iFrames; }
private:
	CPheCamSource() : CActive(EPriorityLow), iBuf(NULL, 0) { CActiveScheduler::Add(this); }
	void ConstructL(TInt aUnit);
	void RunL();
	void DoCancel();
	void Convert(const TUint8* aUyvy);
	void Next();

	RPheCamera iCam;
	TPheCamFormat iFmt;
	HBufC8* iFrame;
	TPtr8 iBuf;
	CFbsBitmap* iBitmap;
	TInt iFrames, iLoggedErr;
	TBool iConnected, iStarted;
	};

#endif
