// Copyright (c) 2002-2009 Nokia Corporation and/or its subsidiary(-ies).
// All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the Eclipse Public License v2.0, available at
// https://www.eclipse.org/legal/epl-2.0/ (relicensed from v1.0 under
// section 7 of the Eclipse Public License v1.0).
// This Source Code may also be made available under the following Secondary
// Licenses when the conditions for such availability set forth in the Eclipse
// Public License, v. 2.0 are satisfied: GNU General Public License, version 2
// or later.
//
// Initial Contributors:
// Nokia Corporation - initial contribution.
//
// Contributors:
// The Phorepoc Authors
//
// Description:
//

#include <w32std.h>
#include <ecom/ecom.h>
#include <ecom/implementationproxy.h>
#include <f32file.h> 
#include "TestCamera.h"
#include "phecamsource.h"
#include <e32debug.h>
#include "test_advanced_settings.h"
#include "test_snapshot.h"
#include "test_image_processing.h"
#include "TestCameraUids.hrh"
#include <ecam.h>
#include <imageconversion.h>
#include <icl/imagecodecdata.h>

//
// Main stuff
//
// 
// 2nd stage ConstructL calls called by framework
//
CTestCamera* CTestCamera::NewL()
	{
	FileDependencyUtil::CheckFileDependencyL();
   	CTestCamera* self = new(ELeave) CTestCamera;
	CleanupStack::PushL(self);
	self->ConstructL();
	CleanupStack::Pop(self);
	return self;
	}

void CTestCamera::Construct2L(MCameraObserver& aObserver,TInt aCameraIndex)
	{
	iObserver = &aObserver;
	iCameraIndex = aCameraIndex;
	PheInitL();
	}
	
void CTestCamera::Construct2DupL(MCameraObserver& aObserver,TInt aCameraHandle)
	{
	iObserver = &aObserver;
	iCameraHandle = aCameraHandle;
	}

void CTestCamera::ConstructL()	
	{
	
	TInt err;
	TRAP(err, iScreenDev = CFbsScreenDevice::NewL(_L(""),EColor16MA));
	if (err == KErrNotSupported)
		{
		TRAP(err, iScreenDev = CFbsScreenDevice::NewL(_L(""),EColor16M));
		}
	if (err == KErrNotSupported)
		{
		TRAP(err, iScreenDev = CFbsScreenDevice::NewL(_L(""),EColor64K));
		}
	if (err == KErrNotSupported)
		{
		TRAP(err, iScreenDev = CFbsScreenDevice::NewL(_L(""),EColor4K));
		}
	if (err == KErrNotSupported)
		{
		TRAP(err, iScreenDev = CFbsScreenDevice::NewL(_L(""),EColor256));
		}
	if (err == KErrNotSupported)
		{
		iScreenDev = CFbsScreenDevice::NewL(_L(""),EColor16MAP);
		}		
	else
		{
		User::LeaveIfError(err);		
		}

	User::LeaveIfError(iScreenDev->CreateContext(iScreenGc));
	// OURS (camera-a64): the capture sizes are the sensor's modes (PheInitL, once the camera index is known); the canvas the
	// template draws viewfinder frames on stays the upright viewfinder size
	iStillImageSize = TSize(480, 640);

	User::LeaveIfError(iVideoFrameSizes.Append( TSize(176, 144) )); // QCIF
	User::LeaveIfError(iVideoFrameSizes.Append( TSize(128, 96) ));  // subQCIF
	iInfo.iNumVideoFrameSizesSupported = iVideoFrameSizes.Count();
	User::LeaveIfError(iVideoFrameRates.Append( 10.00 ));
	User::LeaveIfError(iVideoFrameRates.Append(  5.00 ));
	User::LeaveIfError(iVideoFrameRates.Append(  1.00 ));
	iInfo.iNumVideoFrameRatesSupported = iVideoFrameRates.Count();
	iVideoFrameRate = iVideoFrameRates[0];
	// Still image used as view finder source if not defined otherwise
	iStillImage = new(ELeave) CFbsBitmap;
	User::LeaveIfError(iStillImage->Create(iStillImageSize, KViewFinderDisplayMode));
	iStillImageDev = CFbsBitmapDevice::NewL(iStillImage);
	User::LeaveIfError(iStillImageDev->CreateContext(iStillImageGc));
	iStillImageGc->SetPenColor(KRgbBlack);
	iStillImageGc->SetPenSize(TSize(KFrameFeatureBorderThickness, KFrameFeatureBorderThickness));
	iStillImageGc->SetBrushColor(KRgbWhite);
	iStillImageGc->SetFaded(ETrue);
	iVideoPreparedLast = EFalse;
	iTimer = CPeriodic::NewL(CActive::EPriorityHigh);
	}


void CTestCamera::Construct2L(MCameraObserver2& aObserver,TInt aCameraIndex,TInt aPriority)
	{
	iObserver2 = &aObserver;
	iCameraIndex = aCameraIndex;
	iPriority = aPriority;
	PheInitL();
	}
	
void CTestCamera::Construct2DupL(MCameraObserver2& aObserver,TInt aCameraHandle)
	{
	iObserver2 = &aObserver;
	iCameraHandle = aCameraHandle;
	}
	

CTestCamera::CTestCamera():
	iStillImageFormat(KInitialViewFinderFormat),
	iReserveAsync(CActive::EPriorityStandard),
	iPowerOnAsync(CActive::EPriorityStandard),
	iImageCaptureAsync(CActive::EPriorityStandard),
	iVideoCaptureAsync(CActive::EPriorityStandard),
	iHandleEventAsync(CActive::EPriorityStandard),
	iHandleEvent2Async(CActive::EPriorityStandard),	
	iFrameBuffer1(iVideoFrame1),
	iFrameBuffer2(iVideoFrame2),
	iFrameCameraBuffer1(iVideoFrame1),
	iFrameCameraBuffer2(iVideoFrame2),
	iContrast(0),
	iBrightness(0),
	iImgProcBrightness(0),
	iImgProcContrast(0),
	iVideoCaptureActive(EFalse),
	iImageCaptureActive(EFalse),
	iVideoPreparedLast(EFalse),
	iVideoCapturePrepared(EFalse),
	iImageCapturePrepared(EFalse),
	iECamEvent(KNullUid, KErrNone),
	iECamEvent2(KNullUid, KErrNone, 0),
	iAdvSettingsImpl(NULL),
	iSnapshotImpl(NULL),
	iImgProcImpl(NULL),
	iDriveMode(KDefaultDriveMode),
	iBurstImages(KBurstImages),
	iPheStillBuffer(iPheStill, iPheStillData)
	{
	TCallBack reserveCallBack(ReserveCallBack,this);
	iReserveAsync.Set(reserveCallBack);
	TCallBack powerOnCallBack(PowerOnCallBack,this);
	iPowerOnAsync.Set(powerOnCallBack);
	TCallBack imageCaptureCallBack(ImageCaptureCallBack,this);
	iImageCaptureAsync.Set(imageCaptureCallBack);
	TCallBack videoCaptureCallBack(VideoCaptureCallBack,this);
	iVideoCaptureAsync.Set(videoCaptureCallBack);
	TCallBack handleEventCallBack(HandleEventCallBack,this);
	iHandleEventAsync.Set(handleEventCallBack);
	TCallBack handleEvent2CallBack(HandleEvent2CallBack,this);
	iHandleEvent2Async.Set(handleEvent2CallBack);

	iInfo.iHardwareVersion.iMajor = 0;
	iInfo.iHardwareVersion.iMinor = 0;
	iInfo.iHardwareVersion.iBuild = 0;
	iInfo.iSoftwareVersion.iMajor = 0;
	iInfo.iSoftwareVersion.iMinor = 0;
	iInfo.iSoftwareVersion.iBuild = 0;
	iInfo.iOrientation = TCameraInfo::EOrientationOutwards;

	iInfo.iOptionsSupported = 0;
	iInfo.iOptionsSupported |= TCameraInfo::EViewFinderDirectSupported;
	iInfo.iOptionsSupported |= TCameraInfo::EViewFinderBitmapsSupported;
	iInfo.iOptionsSupported |= TCameraInfo::EImageCaptureSupported;
	iInfo.iOptionsSupported |= TCameraInfo::EVideoCaptureSupported;
	iInfo.iOptionsSupported |= TCameraInfo::EContrastSupported;
	iInfo.iOptionsSupported |= TCameraInfo::EBrightnessSupported;
	iInfo.iOptionsSupported |= TCameraInfo::EViewFinderClippingSupported;

	iInfo.iFlashModesSupported = 0; // Bitfield of TFlash values
	iInfo.iExposureModesSupported = 0; // Bitfield of TExposure values
	iInfo.iWhiteBalanceModesSupported = 0; // Bitfield of TWhiteBalance values

	iInfo.iMinZoom = KMinTestCameraZoom;
	// KMinTestCameraZoom is zero or negative
	// note the algorithm for creating of zoom factor
	iInfo.iMinZoomFactor = TReal32(1)/TReal32(1 << -KMinTestCameraZoom);
	iInfo.iMaxZoom = KMaxTestCameraZoom;
	iInfo.iMaxZoomFactor = 1 << KMaxTestCameraZoom;

	iInfo.iMaxDigitalZoom = KMaxTestCameraDigitalZoom;
	iInfo.iMaxDigitalZoomFactor = KMaxTestCameraDigitalZoomFactor;

	iInfo.iImageFormatsSupported = 0;
	iInfo.iImageFormatsSupported |= EFormatFbsBitmapColor4K;
	iInfo.iImageFormatsSupported |= EFormatFbsBitmapColor64K;
	iInfo.iImageFormatsSupported |= EFormatFbsBitmapColor16M;
	iInfo.iImageFormatsSupported |= EFormatFbsBitmapColor16MU | EFormatJpeg | EFormatExif;	// OURS (camera-a64)

	iInfo.iVideoFrameFormatsSupported = 0;
	iInfo.iVideoFrameFormatsSupported |= EFormatFbsBitmapColor4K;
	iInfo.iVideoFrameFormatsSupported |= EFormatFbsBitmapColor64K;
	iInfo.iVideoFrameFormatsSupported |= EFormatFbsBitmapColor16M;
	iInfo.iMaxFramesPerBufferSupported = 1;
	iInfo.iMaxBuffersSupported = 2;
	
	iScreenRect = TRect(TPoint(0,0), TPoint(0,0));
	
	iEventError = KErrNone;
	}
	
CTestCamera::~CTestCamera()
	{
	delete iTimer;
	delete iPheSaver;
	delete iPheDirectVf;
	delete iPheStill;
	delete iPheStillData;
	iPheStillMode.Close();
	delete iPheSource;
	delete iStillImage;
	delete iStillImageGc;
	delete iStillImageDev;
	delete iVideoFrame1;
	delete iVideoFrameGc1;
	delete iVideoFrameDev1;
	delete iVideoFrame2;
	delete iVideoFrameGc2;
	delete iVideoFrameDev2;
	delete iViewFinderBitmapGc;
	delete iViewFinderBitmapDev;
	delete iViewFinderBitmap;
	delete iDSA;
	delete iScreenGc;
	delete iScreenDev;
	delete iGc;
	delete iDev;
	delete iImage;
	delete iAdvSettingsImpl;
	delete iSnapshotImpl;
	delete iImgProcImpl;
	iImageSizes.Reset();
	iVideoFrameSizes.Reset();
	iVideoFrameRates.Reset();
	}

void CTestCamera::CameraInfo(TCameraInfo& aInfo) const
	{
	aInfo = iInfo;
	}

void CTestCamera::Reserve()
	{
	iReserved = ETrue;
	iReserveAsync.CallBack();
	}

void CTestCamera::Release()
	{
	iReserved = EFalse;
	iTimer->Cancel();
	}

void CTestCamera::PowerOn()
	{
	if (!iReserved || iPowerOn)
		{
		iPowerOnAsync.CallBack();
		return;
		}

	iPowerOn = ETrue;
	TCallBack callback(TimerCallBack,this);
	iTimer->Start(0,TInt(TReal32(1000000) / iVideoFrameRate), callback);
	iPowerOnAsync.CallBack();
	}

void CTestCamera::PowerOff()
	{
	iPowerOn = EFalse;
	iTimer->Cancel();
	}

TInt CTestCamera::Handle()
	{
	return 0;
	}

void CTestCamera::SetZoomFactorL(TInt aZoomFactor)
	{
	// it is equivalent to comparing with iInfo members as they are initialised with the same constants
	// if (aZoomFactor < iInfo.iMinZoom || aZoomFactor > iInfo.iMaxZoom)
	if (aZoomFactor < KMinTestCameraZoom || aZoomFactor > KMaxTestCameraZoom)
		{
		User::Leave(KErrNotSupported);	
		}

	iZoom = aZoomFactor;
	}

TInt CTestCamera::ZoomFactor() const
	{
	// default value is 0
	return iZoom;
	}

void CTestCamera::SetDigitalZoomFactorL(TInt aDigitalZoomFactor)
	{
	CheckReserveAndPowerL();
	// it is equivalent to comparing with iInfo member as it is initialised with the same constant
	// if (aDigitalZoomFactor < 0 || aDigitalZoomFactor > iInfo.iMaxDigitalZoom)
	if (aDigitalZoomFactor < 0 || aDigitalZoomFactor > KMaxTestCameraDigitalZoom)
		{
		User::Leave(KErrNotSupported);	
		}

	iDigitalZoom = aDigitalZoomFactor;
	}

TInt CTestCamera::DigitalZoomFactor() const
	{
	// default value is 0
	return iDigitalZoom;
	}

void CTestCamera::SetContrastL(TInt aContrast)
	{
	CheckReserveAndPowerL();

	iContrast = Min(Max(aContrast,-100),100);
	}

TInt CTestCamera::Contrast() const
	{
	return iContrast;
	}

void CTestCamera::SetBrightnessL(TInt aBrightness)
	{
	CheckReserveAndPowerL();

	iBrightness = Min(Max(aBrightness,-100),100);
	}

TInt CTestCamera::Brightness() const
	{
	return iBrightness;
	}

void CTestCamera::SetFlashL(TFlash aFlash)
	{
	CheckReserveAndPowerL();

	if (aFlash != EFlashNone)
		{
		User::Leave(KErrNotSupported);	
		}
	}

CCamera::TFlash CTestCamera::Flash() const
	{
	return EFlashNone;
	}

void CTestCamera::SetExposureL(TExposure aExposure)
	{
	CheckReserveAndPowerL();

	if (aExposure != EExposureAuto)
		{
		User::Leave(KErrNotSupported);
		}
	}

CCamera::TExposure CTestCamera::Exposure() const
	{
	return EExposureAuto;
	}

void CTestCamera::SetWhiteBalanceL(TWhiteBalance aWhiteBalance)
	{
	CheckReserveAndPowerL();
	User::LeaveIfError(PheSetWhiteBalance(aWhiteBalance));
	}

CCamera::TWhiteBalance CTestCamera::WhiteBalance() const
	{
	return iPheWhiteBalance;
	}

void CTestCamera::StartViewFinderDirectL(RWsSession& aWs,CWsScreenDevice& aScreenDevice,RWindowBase& aWindow,TRect& aScreenRect)
	{
	TRect emptyRect;
	StartViewFinderDirectL(aWs,aScreenDevice,aWindow,aScreenRect,emptyRect);
	}

void CTestCamera::StartViewFinderDirectL(RWsSession& aWs,CWsScreenDevice& aScreenDevice,RWindowBase& aWindow,TRect& aScreenRect,TRect& aClipRect)
	{
	if (!iReserved)
		{
		User::Leave(KErrInUse);		
		}
		
	if (!iPowerOn || iViewFinderActive)
		{
		User::Leave(KErrNotReady);	
		}

	iScreenRect = aScreenRect;
	iClipRect = aClipRect;

	delete iDSA;
	iDSA = NULL;
	iViewFinderActive = EFalse;
	iDSA = CDirectScreenAccess::NewL(aWs,aScreenDevice,aWindow,*this);
	iDSA->StartL();
	iViewFinderActive = ETrue;
	iDSA->Gc()->SetOrigin(); // Set Origin to top left of screen (0,0)

	if (!iClipRect.IsEmpty())
		{
		iDSA->Gc()->SetClippingRect(iClipRect);	
		}
	}

void CTestCamera::StartViewFinderBitmapsL(TSize& aSize)
	{
	TRect emptyRect;
	StartViewFinderBitmapsL(aSize,emptyRect);
	}

void CTestCamera::StartViewFinderBitmapsL(TSize& aSize, TRect& aClipRect)
	{
	if (!iReserved)
		{
		User::Leave(KErrInUse);
		}
	if (!iPowerOn || iViewFinderActive)
		{
		User::Leave(KErrNotReady);
		}

	iViewFinderActive = ETrue;
	iScreenRect = TRect(TPoint(0,0), aSize);
	iClipRect = aClipRect;
	iClipRect.Intersection(iScreenRect);

	delete iViewFinderBitmapGc;
	iViewFinderBitmapGc = NULL;
	delete iViewFinderBitmapDev;
	iViewFinderBitmapDev = NULL;
	delete iViewFinderBitmap;
	iViewFinderBitmap = NULL;

	iViewFinderBitmap = new(ELeave) CFbsBitmap;
	if (!iClipRect.IsEmpty())
		{
		User::LeaveIfError(iViewFinderBitmap->Create(iClipRect.Size(), KViewFinderDisplayMode));		
		}
	else
		{
		User::LeaveIfError(iViewFinderBitmap->Create(aSize, KViewFinderDisplayMode));		
		}
	iViewFinderBitmapDev = CFbsBitmapDevice::NewL(iViewFinderBitmap);
	User::LeaveIfError(iViewFinderBitmapDev->CreateContext(iViewFinderBitmapGc));
	}
	
void CTestCamera::StartViewFinderL(TFormat aImageFormat,TSize& aSize)
       {
       TRect emptyRect;
       StartViewFinderL(aImageFormat,aSize,emptyRect);
       }
	
void CTestCamera::StartViewFinderL(TFormat aImageFormat,TSize& /*aSize*/,TRect& aClipRect)	
       {
     	if(!iReserved)
     		{
     		User::Leave(KErrInUse);
     		}
		if(!iPowerOn || iViewFinderActive)
			{
			User::Leave(KErrNotReady);
			}
		
		iViewFinderActive = ETrue;
		iClipRect = aClipRect;
		if(!(aImageFormat & iInfo.iImageFormatsSupported))
			{
			 User::Leave(KErrNotSupported);
			}
       }
       

void CTestCamera::StopViewFinder()
	{
	iViewFinderActive = EFalse;
	delete iDSA;
	iDSA = NULL;

	delete iViewFinderBitmapGc;
	iViewFinderBitmapGc = NULL;
	delete iViewFinderBitmapDev;
	iViewFinderBitmapDev = NULL;
	delete iViewFinderBitmap;
	iViewFinderBitmap = NULL;
	}

TBool CTestCamera::ViewFinderActive() const
	{
	return iViewFinderActive;
	}

void CTestCamera::SetViewFinderMirrorL(TBool aMirror)
	{
	// OURS (camera-a64): left-right in the conversion (a selfie preview); stills stay unmirrored
	iPheMirror = aMirror;
	if (iPheSource)
		iPheSource->SetMirror(aMirror);
	}

TBool CTestCamera::ViewFinderMirror() const
	{
	return iPheMirror;
	}

void CTestCamera::PrepareImageCaptureL(TFormat aImageFormat,TInt aSizeIndex)
	{
	if (!iReserved)
		{
		User::Leave(KErrInUse);
		}
		
	if (!iPowerOn || iVideoCaptureActive || iImageCaptureActive)
		{
		User::Leave(KErrNotReady);
		}
		

	if (!(aImageFormat & iInfo.iImageFormatsSupported) || aSizeIndex < 0
		|| aSizeIndex >= iInfo.iNumImageSizesSupported)
		{
		User::Leave(KErrNotSupported);
		}

	iVideoPreparedLast = EFalse;
	// OURS (camera-a64): a still is a capture in the sensor mode of that size (CPheCamSource::CaptureStillL), delivered in
	// the asked format; the template's canvas (iStillImage) stays the viewfinder's
	iPheStillIndex = aSizeIndex;
	iStillImageFormat = aImageFormat;
	iImageCapturePrepared = ETrue;
	RDebug::Printf("PH: camera-a64: plug-in still prepared: %dx%d, format %x", iImageSizes[aSizeIndex].iWidth,
		iImageSizes[aSizeIndex].iHeight, aImageFormat);
	}

void CTestCamera::PrepareImageCaptureL(TFormat /*aImageFormat*/,TInt /*aSizeIndex*/,const TRect& /*aClipRect*/)
	{
	User::Leave(KErrNotSupported);
	}

void CTestCamera::CaptureImage()
	{
	if (!iReserved)
		{
		iStillCaptureErr = KErrInUse;
		}
		
	if (!iPowerOn || iImageCaptureActive || iVideoCaptureActive || !iImageCapturePrepared)
		{
		iStillCaptureErr = KErrNotReady;
		}

	if (iStillCaptureErr == KErrNone)
		{
		TRAP(iStillCaptureErr,DoCaptureImageL());
		}

	if (iStillCaptureErr == KErrNone)
		{
		iImageCaptureActive = ETrue;
		}
	else
		{
		iImageCaptureAsync.CallBack();
		}
	}

void CTestCamera::DoCaptureImageL()
	{
	delete iGc;
	iGc = NULL;
	delete iDev;
	iDev = NULL;
	delete iImage;
	iImage = NULL;

	CFbsBitmap* image = new(ELeave) CFbsBitmap;
	CleanupStack::PushL(image);
	User::LeaveIfError(image->Create(iStillImageSize, iStillImage->DisplayMode()));
	CFbsBitmapDevice* dev = CFbsBitmapDevice::NewL(image);
	CleanupStack::PushL(dev);
	CFbsBitGc* gc = NULL;
	User::LeaveIfError(dev->CreateContext(gc));
	CleanupStack::Pop(dev);
	CleanupStack::Pop(image);

	iImage = image;
	iDev = dev;
	iGc = gc;
	}

void CTestCamera::CancelCaptureImage()
	{
	iImageCaptureActive = EFalse;
	}

void CTestCamera::EnumerateCaptureSizes(TSize& aSize,TInt aSizeIndex,TFormat aFormat) const
	{
	if (aSizeIndex < 0 || aSizeIndex >= iInfo.iNumImageSizesSupported ||
		!(aFormat & iInfo.iImageFormatsSupported) )
		{
		aSize = TSize(0,0);
		}
	else
		{
		aSize = iImageSizes[aSizeIndex];
		}
	}

void CTestCamera::PrepareVideoCaptureL(TFormat aFormat,TInt aSizeIndex,TInt aRateIndex,TInt aBuffersToUse,TInt aFramesPerBuffer)
	{
	if (!iReserved)
		{
		User::Leave(KErrInUse);
		}
		
	if (!iPowerOn || iVideoCaptureActive || iImageCaptureActive)
		{
		User::Leave(KErrNotReady);
		}

	if (!(aFormat & iInfo.iVideoFrameFormatsSupported) 
		|| aSizeIndex < 0 || aSizeIndex >= iInfo.iNumVideoFrameSizesSupported
		|| aRateIndex < 0 || aRateIndex >= iInfo.iNumVideoFrameRatesSupported
		|| aBuffersToUse != 2 || aFramesPerBuffer != 1)
		{
		User::Leave(KErrNotSupported);
		}

	iVideoPreparedLast = ETrue;

	if (iVideoCapturePrepared 
		&& iVideoFrameSize == iVideoFrameSizes[aSizeIndex]
		&& iVideoFrameFormat == aFormat)
		{
		return;
		}

	TDisplayMode displayMode = ENone;
	switch (aFormat)
		{
		case EFormatFbsBitmapColor4K :
			displayMode = EColor4K;
			break;
		case EFormatFbsBitmapColor64K :
			displayMode = EColor64K;
			break;
		case EFormatFbsBitmapColor16M :
			displayMode = EColor16M;
			break;
		default :
			User::Leave(KErrNotSupported);
			break;
		}

	iVideoFrameSize = iVideoFrameSizes[aSizeIndex];
	iVideoFrameRate = iVideoFrameRates[aRateIndex];
	iVideoFrameFormat = aFormat;
	iBuffersInUse = aBuffersToUse;

	delete iVideoFrameGc1;
	iVideoFrameGc1 = NULL;
	delete iVideoFrameDev1;
	iVideoFrameDev1 = NULL;
	delete iVideoFrame1;
	iVideoFrame1 = NULL;

	delete iVideoFrameGc2;
	iVideoFrameGc2 = NULL;
	delete iVideoFrameDev2;
	iVideoFrameDev2 = NULL;
	delete iVideoFrame2;
	iVideoFrame2 = NULL;

	iVideoFrame1 = new(ELeave) CFbsBitmap;
	User::LeaveIfError(iVideoFrame1->Create(iVideoFrameSize, displayMode));
	iVideoFrameDev1 = CFbsBitmapDevice::NewL(iVideoFrame1);
	User::LeaveIfError(iVideoFrameDev1->CreateContext(iVideoFrameGc1));
	iVideoFrameGc1->SetPenColor(KRgbBlack);
	iVideoFrameGc1->SetPenSize(TSize(KFrameFeatureBorderThickness, KFrameFeatureBorderThickness));
	iVideoFrameGc1->SetBrushColor(KRgbWhite);
	iVideoFrameGc1->SetFaded(ETrue);

	if (iBuffersInUse == 2)
		{
		iVideoFrame2 = new(ELeave) CFbsBitmap;
		User::LeaveIfError(iVideoFrame2->Create(iVideoFrameSize, displayMode));
		iVideoFrameDev2 = CFbsBitmapDevice::NewL(iVideoFrame2);
		User::LeaveIfError(iVideoFrameDev1->CreateContext(iVideoFrameGc2));
		iVideoFrameGc2->SetPenColor(KRgbBlack);
		iVideoFrameGc2->SetPenSize(TSize(KFrameFeatureBorderThickness, KFrameFeatureBorderThickness));
		iVideoFrameGc2->SetBrushColor(KRgbWhite);
		iVideoFrameGc2->SetFaded(ETrue);		
		}

	iVideoCapturePrepared = ETrue;
	}

void CTestCamera::PrepareVideoCaptureL(TFormat /*aFormat*/,TInt /*aSizeIndex*/,TInt /*aRateIndex*/,TInt /*aBuffersToUse*/,TInt /*aFramesPerBuffer*/,const TRect& /*aClipRect*/)
	{
	User::Leave(KErrNotSupported);
	}

void CTestCamera::StartVideoCapture()
	{
	if (!iReserved)
		{
		iVideoCaptureErr = KErrInUse;
		}
		
	if (!iPowerOn || iImageCaptureActive || iVideoCaptureActive || !iVideoCapturePrepared)
		{
		iVideoCaptureErr = KErrNotReady;
		}
		
	if (iVideoCaptureErr == KErrNone)
		{
		iVideoCaptureActive = ETrue;
		}
		
	else
		{
		iVideoCaptureAsync.CallBack();
		}
	}

void CTestCamera::StopVideoCapture()
	{
	iVideoCaptureActive = EFalse;
	iFrameIndex = 0;
	iElapsedTime = 0;
	}

TBool CTestCamera::VideoCaptureActive() const
	{
	return iVideoCaptureActive;
	}

void CTestCamera::EnumerateVideoFrameSizes(TSize& aSize,TInt aSizeIndex,TFormat aFormat) const
	{
	if (aSizeIndex < 0 || aSizeIndex >= iInfo.iNumVideoFrameSizesSupported ||
		!(aFormat & iInfo.iVideoFrameFormatsSupported))
		{
		aSize = TSize(0,0);
		}
	else
		{
		aSize = iVideoFrameSizes[aSizeIndex];
		}
	}

void CTestCamera::EnumerateVideoFrameRates(TReal32& aRate,TInt aRateIndex,TFormat aFormat,TInt aSizeIndex,TExposure aExposure) const
	{
	if (aRateIndex < 0 || aRateIndex >= iInfo.iNumVideoFrameRatesSupported ||
		aSizeIndex < 0 || aSizeIndex >= iInfo.iNumVideoFrameSizesSupported ||
		!(aFormat & iInfo.iVideoFrameFormatsSupported) ||
		(!(aExposure & iInfo.iExposureModesSupported) && aExposure != CCamera::EExposureAuto))
		{
		aRate = 0.0;
		}
	else
		{
		aRate = iVideoFrameRates[aRateIndex];
		}
	}

void CTestCamera::GetFrameSize(TSize& aSize) const
	{
	aSize = iVideoFrameSize;
	}

TReal32 CTestCamera::FrameRate() const
	{
	return iVideoFrameRate;
	}

TInt CTestCamera::BuffersInUse() const
	{
	return iBuffersInUse;
	}

TInt CTestCamera::FramesPerBuffer() const
	{
	return 1;
	}

void CTestCamera::SetJpegQuality(TInt aQuality)
	{
	iPheJpegQuality = Max(1, Min(aQuality, 100));
	}

TInt CTestCamera::JpegQuality() const
	{
	return iPheJpegQuality;
	}

TAny* CTestCamera::CustomInterface(TUid aInterface)
	{
	// OURS (camera-a64)
	if (aInterface == KPheCameraControlsUid)
		return static_cast<MPheCameraControls*>(this);
	if (aInterface == KECamMCameraDirectViewFinderUid)
		{
		if (!iPheDirectVf)
			iPheDirectVf = new CPheDirectViewFinder(*this);
		return iPheDirectVf ? static_cast<MCameraDirectViewFinder*>(iPheDirectVf) : NULL;
		}
	TAny* ptrIface = NULL;
	TRAPD(error,CustomInterfaceL(ptrIface, aInterface));
	
	if(error == KErrNone)
		{
		switch(aInterface.iUid)
			{
			// advanced settings interface pointers
			case KECamMCameraAdvancedSettingsUidValue:
				{
				return static_cast<MCameraAdvancedSettings*>(ptrIface);
				}
			case KECamMCameraAdvancedSettings2UidValue:
				{
				return static_cast<MCameraAdvancedSettings2*>(ptrIface);			
				}
			case KECamMCameraAdvancedSettings3UidValue:
				{
				return static_cast<MCameraAdvancedSettings3*>(ptrIface);
				}
			
			// snapshot interface pointers	
			case KECamMCameraSnapshotUidValue:
				{
				return static_cast<MCameraSnapshot*>(ptrIface);
				}
				
			// preset interface pointers	
			case KECamMCameraPresetsUidValue:
				return static_cast<MCameraPresets*>(ptrIface);
				
			// image processing interface pointers
			case KECamMCameraImageProcessingUidValue:
				{
				return static_cast<MCameraImageProcessing*>(ptrIface);
				}
				
			case KECamMCameraImageProcessing2UidValue:
				{
				return static_cast<MCameraImageProcessing2*>(ptrIface);
				}
				
			default:
				return NULL;
			}
		}
	else
		{
		return NULL;		
		}
	}

void CTestCamera::CustomInterfaceL(TAny*& aIface, TUid aInterface)
	{
	// OURS: no snapshot, presets or image processing on the camera server (the template's test implementations drew
	// synthetic data): no interface, so CCameraSnapshot::NewL, CCameraPresets::NewL and CCameraImageProcessing::NewL
	// leave KErrNotSupported cleanly (round 14). Their classes stay for the template's own paths.
	if (aInterface.iUid == KECamMCameraSnapshotUidValue || aInterface.iUid == KECamMCameraPresetsUidValue ||
		aInterface.iUid == KECamMCameraImageProcessingUidValue || aInterface.iUid == KECamMCameraImageProcessing2UidValue)
		{
		aIface = NULL;
		return;
		}
	switch(aInterface.iUid)
		{
		// advanced settings interface pointers
		case KECamMCameraAdvancedSettingsUidValue:
			{
			iAdvSettingsImpl = CTestCamAdvSet::NewL(*this);
			aIface = static_cast<MCameraAdvancedSettings*>(iAdvSettingsImpl);
			break;
			}
		case KECamMCameraAdvancedSettings2UidValue:
			{
			iAdvSettingsImpl = CTestCamAdvSet::NewL(*this);
			aIface = static_cast<MCameraAdvancedSettings2*>(iAdvSettingsImpl);			
			break;
			}
		case KECamMCameraAdvancedSettings3UidValue:
			{
			iAdvSettingsImpl = CTestCamAdvSet::NewL(*this);
			aIface = static_cast<MCameraAdvancedSettings3*>(iAdvSettingsImpl);
			break;
			}
		
		// snapshot interface pointers	
		case KECamMCameraSnapshotUidValue:
			{
			iSnapshotImpl = CTestCamSnapshot::NewL(*this);
			aIface = static_cast<MCameraSnapshot*>(iSnapshotImpl);
			break;
			}
		// preset interface pointers	
		case KECamMCameraPresetsUidValue:
			{
			aIface = static_cast<MCameraPresets*>(CTestCamPresets::NewL(*this));
			break;
			}
			
		// image processing interface pointers
		case KECamMCameraImageProcessingUidValue:
			{
			iImgProcImpl = CTestCamImgProc::NewL(*this);	
			aIface = static_cast<MCameraImageProcessing*>(iImgProcImpl);
			break;
			}
			
		case KECamMCameraImageProcessing2UidValue:
			{
			iImgProcImpl = CTestCamImgProc::NewL(*this);	
			aIface = static_cast<MCameraImageProcessing2*>(iImgProcImpl);
			break;
			}
			
		default:
			aIface = NULL;
		}
	}

void CTestCamera::AbortNow(RDirectScreenAccess::TTerminationReasons /*aReason*/)
	{
	iViewFinderActive = EFalse;
	}

void CTestCamera::Restart(RDirectScreenAccess::TTerminationReasons /*aReason*/)
	{
	TRAPD(error,iDSA->StartL());
	if (error != KErrNone)
		{
		return;
		}
	iDSA->Gc()->SetOrigin(); // Set Origin to top left of screen (0,0)

	if (!iClipRect.IsEmpty())
		{
		iDSA->Gc()->SetClippingRect(iClipRect);
		}
	else
		{
		iDSA->Gc()->CancelClippingRect();
		}

	iViewFinderActive = ETrue;
	}

TInt CTestCamera::TimerCallBack(TAny* aThis)
	{
	static_cast<CTestCamera*>(aThis)->FrameReady();
	return KErrNone;
	}

void CTestCamera::FrameReady()
	{
	// OURS (phecam): deliver only real driver frames -- no test pattern and no repeated frame, so a client never counts
	// (or saves a still of) anything the sensor did not send
	if (!iPheSource || (!iPheSource->Streaming() && iPheSource->Start() != KErrNone))
		{
		if (iPheFallback++ == 0)
			RDebug::Printf("PH: camera-a64: plug-in camera driver unavailable: no frames");
		return;
		}
	const TInt pheFrames = iPheSource->FrameCount();
	if (pheFrames == iPheDelivered)
		{
		if (iPheFallback++ % 100 == 0)
			RDebug::Printf("PH: camera-a64: plug-in no new frame (tick %d, %d real frames so far)", iPheFallback, pheFrames);
		return;
		}
	iPheDelivered = pheFrames;
	if (iVideoPreparedLast)
		{
		iVideoFramePos.iX += KFramePosIncrement;
		iVideoFramePos.iY += KFramePosIncrement;

		if (iVideoFramePos.iX >= iVideoFrameSize.iWidth)
			{
			iVideoFramePos.iX = 0;
			}
			
		if (iVideoFramePos.iY >= iVideoFrameSize.iHeight)
			{
			iVideoFramePos.iY = 0;
			}
		}
	else
		{
		iStillImagePos.iX += KFramePosIncrement;
		iStillImagePos.iY += KFramePosIncrement;

		if (iStillImagePos.iX >= iStillImageSize.iWidth)
			{
			iStillImagePos.iX = 0;
			}
			
		if (iStillImagePos.iY >= iStillImageSize.iHeight)
			{
			iStillImagePos.iY = 0;
			}
		}

	if (iImageCaptureActive)
		{
		if (!iImageCapturePrepared)
			{
			iImageCaptureActive = EFalse;
			if (iObserver2)
				{
				iObserver2->ImageBufferReady(iFrameCameraBuffer1,KErrNotReady);
				}
			else
				{
				iObserver->ImageReady(NULL,NULL,KErrNotReady);
				}
			}
		else
			{
			if(iDriveMode == CCamera::CCameraAdvancedSettings::EDriveModeBurst)
				{
				for (TInt index =0; index < iBurstImages -1; index++)
					{
					if(iSnapshotImpl != NULL)
						{
						if(iSnapshotImpl->iSnapshotActive)
							{
							GetFrame(*(iSnapshotImpl->iSnapshotImageGc), iStillImagePos);
							iSnapshotImpl->ServiceBurstImageSnapshot(*(iSnapshotImpl->iSnapshotImageGc));
							}
						}
					
					GetImgProcessedFrame(*iStillImageGc, iStillImagePos);
					ServiceBurstImageCapture(*iStillImageGc);
					}
				}
				
			// normal case, single shot and last image from burst mode
			if(iSnapshotImpl != NULL)
				{
				if(iSnapshotImpl->iSnapshotActive)
					{
					GetFrame(*iSnapshotImpl->iSnapshotImageGc, iStillImagePos);
					iSnapshotImpl->ServiceSnapshot(*(iSnapshotImpl->iSnapshotImageGc));
					}
				}
				
			PheCaptureStill();	// OURS (camera-a64): a capture in the sensor mode of the prepared size	
							
			if (iVideoPreparedLast)
				{
				GetFrame(*iVideoFrameGc1, iVideoFramePos);
				if (iViewFinderActive)
					{
					ServiceViewFinder(iVideoFrame1);
					}	
				}
			else
				{
				if (iViewFinderActive)
					{
					ServiceViewFinder(iStillImage);
					}
				}
			}
		}

	else if (iVideoCaptureActive)
		{
		if (!iVideoCapturePrepared)
			{
			iVideoCaptureActive = EFalse;
			if (iObserver2)
				{
				iObserver2->VideoBufferReady(iFrameCameraBuffer1,KErrNotReady);
				}
			else
				{
				iObserver->FrameBufferReady(NULL,KErrNotReady);
				}
			}
		else
			{
			if (iObserver2)
				{
				if (iFrameCameraBuffer1.iAvailable)
					{
					GetFrame(*iVideoFrameGc1, iVideoFramePos);
					ServiceVideo(1);
					if (iViewFinderActive)
						{
						ServiceViewFinder(iVideoFrame1);
						}
					}
				else if (iFrameCameraBuffer2.iAvailable && iBuffersInUse == 2)
					{
					GetFrame(*iVideoFrameGc2, iVideoFramePos);
					ServiceVideo(2);
					if (iViewFinderActive)
						{
						ServiceViewFinder(iVideoFrame2);
						}
					}
				}
			else
				{
				if (iFrameBuffer1.iAvailable)
					{
					GetFrame(*iVideoFrameGc1, iVideoFramePos);
					ServiceVideo(1);
					if (iViewFinderActive)
						{
						ServiceViewFinder(iVideoFrame1);
						}
					}
				else if (iFrameBuffer2.iAvailable && iBuffersInUse == 2)
					{
					GetFrame(*iVideoFrameGc2, iVideoFramePos);
					ServiceVideo(2);
					if (iViewFinderActive)
						{
						ServiceViewFinder(iVideoFrame2);
						}
					}
				}
			}
	}

	else if (iViewFinderActive)
		{
		if (iVideoPreparedLast)
			{
			GetFrame(*iVideoFrameGc1, iVideoFramePos);
			ServiceViewFinder(iVideoFrame1);
			}
			else
			{
			GetFrame(*iStillImageGc, iStillImagePos);
			ServiceViewFinder(iStillImage);
			}
		}
	}

void CTestCamera::GetFrame(CFbsBitGc& aGc, TPoint& aPos) const
	{
	// OURS (phecam): the latest frame of the camera driver, scaled to the target; the moving square below is the fallback
	CFbsBitmap* frame = iPheSource ? iPheSource->Frame() : NULL;
	if (frame)
		{
		// the stock target GCs draw FADED (SetFaded(ETrue) + the contrast/brightness fading map below): a real frame must
		// not be faded (boot 938: washed-out, low-saturation live view)
		aGc.SetFaded(EFalse);
		aGc.DrawBitmap(TRect(TPoint(0, 0), aGc.Device()->SizeInPixels()), frame);
		aGc.SetFaded(ETrue);
		return;
		}
	TInt black = (100 - iContrast) * 127 / 200;
	TInt white = ((100 + iContrast) * 128 / 200) + 127;

	const TInt brightAdjust = iBrightness * 255 / 100;
	black += brightAdjust;
	white += brightAdjust;

	black = Min(Max(black,0),255);
	white = Min(Max(white,0),255);

	aGc.SetFadingParameters(TUint8(black),TUint8(white));
	aGc.Clear();
	aGc.DrawRect(TRect(aPos,TSize(KFrameFeatureSize,KFrameFeatureSize)));
	}
	
void CTestCamera::GetImgProcessedFrame(CFbsBitGc& aGc, TPoint& aPos) const
	{
	if (iPheSource && iPheSource->Frame())	// OURS (phecam): a still = the latest driver frame
		{
		GetFrame(aGc, aPos);
		return;
		}
	TInt contrast = iContrast;
	TInt brightness = iBrightness;
	
	if(iImgProcImpl != NULL)
		{
		for(TInt index=0; index<iImgProcImpl->iActiveTransformations.Count(); index++)
			{
			switch(iImgProcImpl->iActiveTransformations[index].iUid)
				{
				case KUidECamEventImageProcessingAdjustBrightnessUidValue:
					{
					brightness = iImgProcBrightness;
					break;
					}
				case KUidECamEventImageProcessingAdjustContrastUidValue:
					{
					contrast = iImgProcContrast;
					break;
					}
				default:
					{
					break;
					}
				}
			}
		}
	
	TInt black = (100 - contrast) * 127 / 200;
	TInt white = ((100 + contrast) * 128 / 200) + 127;

	const TInt brightAdjust = brightness * 255 / 100;
	black += brightAdjust;
	white += brightAdjust;

	black = Min(Max(black,0),255);
	white = Min(Max(white,0),255);

	aGc.SetFadingParameters(TUint8(black),TUint8(white));
	aGc.Clear();
	aGc.DrawRect(TRect(aPos,TSize(KFrameFeatureSize,KFrameFeatureSize)));
	}
	
void CTestCamera::ServiceVideo(TInt aBufferNum)
	{
	iFrameIndex++;
	iElapsedTime = iElapsedTime.Int64() + (TInt64(1000000) / TInt64(iVideoFrameRate));

	if (aBufferNum == 1)
		{
		if (iObserver2)
			{
			iFrameCameraBuffer1.iIndexOfFirstFrameInBuffer = iFrameIndex;
			iFrameCameraBuffer1.iElapsedTime = iElapsedTime;
			iFrameCameraBuffer1.iAvailable = EFalse;
			}
		else
			{
			iFrameBuffer1.iIndexOfFirstFrameInBuffer = iFrameIndex;
			iFrameBuffer1.iElapsedTime = iElapsedTime;
			iFrameBuffer1.iAvailable = EFalse;
			}
			
		if (iObserver2)
			{
			iObserver2->VideoBufferReady(iFrameCameraBuffer1,KErrNone);
			}
		else
			{
			iObserver->FrameBufferReady(&iFrameBuffer1,KErrNone);
			}
		}
	else if (aBufferNum == 2)
		{
		if (iObserver2)
			{
			iFrameCameraBuffer2.iIndexOfFirstFrameInBuffer = iFrameIndex;
			iFrameCameraBuffer2.iElapsedTime = iElapsedTime;
			iFrameCameraBuffer2.iAvailable = EFalse;
			}
		else
			{
			iFrameBuffer2.iIndexOfFirstFrameInBuffer = iFrameIndex;
			iFrameBuffer2.iElapsedTime = iElapsedTime;
			iFrameBuffer2.iAvailable = EFalse;
			}
			
		if (iObserver2)
			{
			iObserver2->VideoBufferReady(iFrameCameraBuffer2,KErrNone);
			}
		else
			{
			iObserver->FrameBufferReady(&iFrameBuffer2,KErrNone);
			}
		}
	}

void CTestCamera::ServiceViewFinder(CFbsBitmap* aSourceFrame)
	{
	if (iPheVfPaused)	// OURS: CCameraDirectViewFinder::PauseViewFinderDirectL
		return;
	if (iDSA)
		{
		iDSA->Gc()->DrawBitmap(iScreenRect,aSourceFrame);
		iDSA->ScreenDevice()->Update();
		}
	else if (iViewFinderBitmap)
		{
		if (iViewFinderBitmapGc)
			{
			if (!iClipRect.IsEmpty())
				{
				TReal32 xRatio = static_cast<TReal32>(aSourceFrame->SizeInPixels().iWidth) / 
								 static_cast<TReal32>(iClipRect.Size().iWidth);

				TReal32 yRatio = static_cast<TReal32>(aSourceFrame->SizeInPixels().iHeight) / 
								 static_cast<TReal32>(iClipRect.Size().iHeight);

				TRect sourceRect(TPoint(static_cast<TInt>(xRatio*iClipRect.iTl.iX),
										static_cast<TInt>(yRatio*iClipRect.iTl.iY)),
								 TPoint(static_cast<TInt>(xRatio*iClipRect.iBr.iX),
										static_cast<TInt>(yRatio*iClipRect.iBr.iY)));

				TRect destRect(TPoint(0,0), iClipRect.Size());

				iViewFinderBitmapGc->DrawBitmap(destRect,
												aSourceFrame,
												sourceRect);
				}
			else
				{
				TRect destRect(TPoint(0,0), iViewFinderBitmap->SizeInPixels());
				iViewFinderBitmapGc->DrawBitmap(destRect, aSourceFrame);
				}
				
			if (iObserver2)
				{
				iObserver2->ViewFinderReady(iFrameCameraBuffer1,KErrNone);
				}
			else
				{
				iObserver->ViewFinderFrameReady(*iViewFinderBitmap);
				}
		
			}
		}
	else
		{
		iScreenGc->DrawBitmap(iClipRect,aSourceFrame);
		iScreenDev->Update();
		}
	}

void CTestCamera::ServiceImageCapture(const CFbsBitGc& aSourceBitmapGc)
	{
	iImageCaptureActive = EFalse;
	iGc->BitBlt(TPoint(0,0), aSourceBitmapGc);
	CFbsBitmap* image = iImage;
	RTestCameraFrameBuffer frameBuffer(image);
	delete iGc;
	iGc = NULL;
	delete iDev;
	iDev = NULL;
	if (iObserver2 != NULL)
		{
		iObserver2->ImageBufferReady(frameBuffer,KErrNone);
		}
	else
		{
		iImage = NULL;
		iObserver->ImageReady(image,NULL,KErrNone);
		}
	}
	
void CTestCamera::ServiceBurstImageCapture(const CFbsBitGc& aSourceBitmapGc)
	{
	iGc->BitBlt(TPoint(0,0), aSourceBitmapGc);
	if (iObserver2 != NULL)
		{
		CFbsBitmap* image = iImage;
		RTestCameraFrameBuffer frameBuffer(image);
		iObserver2->ImageBufferReady(frameBuffer,KErrNone);
		}
	else//not for MCameraObserver
		{
		return;
		}
	
	if(iSnapshotImpl)
		{
		if(iSnapshotImpl->iSnapshotActive)
			{
			return;
			}
		}
	User::After(500000);
	iStillImagePos.iX += KFramePosIncrement;
	iStillImagePos.iY += KFramePosIncrement;

	if (iStillImagePos.iX >= iStillImageSize.iWidth)
		{
		iStillImagePos.iX = 0;
		}
			
	if (iStillImagePos.iY >= iStillImageSize.iHeight)
		{
		iStillImagePos.iY = 0;
		}
	}

void CTestCamera::CheckReserveAndPowerL()
	{
	if (!iReserved)
		{
		User::Leave(KErrInUse);
		}
		
	if (!iPowerOn)
		{
		User::Leave(KErrNotReady);
		}	
	}

TInt CTestCamera::CheckReserveAndPower() const
	{
	if (!iReserved)
		{
		return KErrInUse;
		}
		
	if (!iPowerOn)
		{
		return KErrNotReady;
		}	
	
	return KErrNone;
	}

TInt CTestCamera::ReserveCallBack(TAny* aThis)
	{
	const TECAMEvent event(KUidECamEventReserveComplete,KErrNone);
	CTestCamera* testCamera = static_cast<CTestCamera*>(aThis);
	if (testCamera->iObserver2)
		{
		testCamera->iObserver2->HandleEvent(event);
		}
	else
		{
		testCamera->iObserver->ReserveComplete(KErrNone);
		}
	return KErrNone;
	}

TInt CTestCamera::PowerOnCallBack(TAny* aThis)
	{
	TECAMEvent event(KUidECamEventPowerOnComplete,KErrNone);
	CTestCamera* testCamera = static_cast<CTestCamera*>(aThis);
	if (testCamera->iObserver2)
		{
		testCamera->iObserver2->HandleEvent(event);
		}
	else
		{
		testCamera->iObserver->PowerOnComplete(testCamera->iPowerOn ? KErrNone : KErrInUse);
		}
	return KErrNone;
	}

TInt CTestCamera::ImageCaptureCallBack(TAny* aThis)
	{
	CTestCamera* testCamera = static_cast<CTestCamera*>(aThis);
	if (testCamera->iObserver2)
		{
		testCamera->iObserver2->ImageBufferReady((static_cast<CTestCamera*>(aThis))->iFrameCameraBuffer1,testCamera->iStillCaptureErr);
		}
	else
		{
		testCamera->iObserver->ImageReady(NULL,NULL,testCamera->iStillCaptureErr);
		}
	testCamera->iStillCaptureErr = KErrNone;
	return KErrNone;
	}

TInt CTestCamera::VideoCaptureCallBack(TAny* aThis)
	{
	CTestCamera* testCamera = static_cast<CTestCamera*>(aThis);
	if (testCamera->iObserver2)
		{
		testCamera->iObserver2->VideoBufferReady((static_cast<CTestCamera*>(aThis))->iFrameCameraBuffer1,testCamera->iVideoCaptureErr);
		}
	else
		{
		testCamera->iObserver->FrameBufferReady(NULL,testCamera->iVideoCaptureErr);
		}
	testCamera->iVideoCaptureErr = KErrNone;
	return KErrNone;
	}
	
TInt CTestCamera::HandleEventCallBack(TAny* aThis)
	{
	CTestCamera* testCamera = static_cast<CTestCamera*>(aThis);
	if (testCamera->iObserver2)
		{
		testCamera->iObserver2->HandleEvent(testCamera->iECamEvent);
		}
	
	return KErrNone;
	}

TInt CTestCamera::HandleEvent2CallBack(TAny* aThis)
	{
	CTestCamera* testCamera = static_cast<CTestCamera*>(aThis);
	if (testCamera->iObserver2)
		{
		testCamera->iObserver2->HandleEvent(testCamera->iECamEvent2);
		}
	
	return KErrNone;
	}

//
// CTestCameraInfo
//

CTestCameraInfo::CTestCameraInfo()
	{
	}
	
CTestCameraInfo::~CTestCameraInfo()
	{
	}
	
CTestCameraInfo* CTestCameraInfo::NewL()
	{
	FileDependencyUtil::CheckFileDependencyL();
	return new (ELeave) CTestCameraInfo;
	}
	
TInt CTestCameraInfo::CamerasAvailable()
	{
	return 2;	// OURS (camera-a64): rear OV5640 (index 0) + front GC2145 (index 1), the server's units
	}

//void CTestCamera::CheckFileDependencyL()
void FileDependencyUtil::CheckFileDependencyL()
	{	
	return;	// OURS (phecam): the stock plug-in only loads when C:\102070cc.txt exists (a guard against it running on a phone); this plug-in is the installed camera driver package's
	RFs fsSession;
  	RFile file;
    CleanupClosePushL(fsSession);
  	User::LeaveIfError(fsSession.Connect());
    TInt err = file.Open(fsSession, KTestCameraPluginName, EFileRead);
  	file.Close();
  	if(err != KErrNone)
  		{
	    User::LeaveIfError(KErrNotSupported);
  		}
  	CleanupStack::PopAndDestroy(); //fsSession 
	}

// __________________________________________________________________________
// Exported proxy for instantiation method resolution
// Define the interface UIDs
const TImplementationProxy ImplementationTable[] = 
	{
	IMPLEMENTATION_PROXY_ENTRY(KUidOnboardCameraTestPlugin,	CTestCamera::NewL),
	IMPLEMENTATION_PROXY_ENTRY(KUidOnboardCameraTestInfo,	CTestCameraInfo::NewL)
	};

EXPORT_C const TImplementationProxy* ImplementationGroupProxy(TInt& aTableCount)
	{
	aTableCount = sizeof(ImplementationTable) / sizeof(TImplementationProxy);

	return ImplementationTable;
	}


// ---- OURS (camera-a64): the real camera behind the template ----

// the camera index is the driver unit (0 rear OV5640, 1 front GC2145): its modes become the capture sizes, its controls
// the white-balance modes; without the camera server the template keeps empty lists (no frames, no stills)
void CTestCamera::PheInitL()
	{
	TRAPD(err, iPheSource = CPheCamSource::NewL(iCameraIndex));
	if (err != KErrNone)
		{
		RDebug::Printf("PH: camera-a64: plug-in camera %d unavailable (%d)", iCameraIndex, err);
		return;
		}
	// the server's unit 1 is the front camera: apps mirror a camera that faces the user (TCameraInfo, round 14)
	iInfo.iOrientation = iCameraIndex == 1 ? TCameraInfo::EOrientationInwards : TCameraInfo::EOrientationOutwards;
	// ECam lists capture sizes largest first
	const RArray<TSize>& sizes = iPheSource->StillSizes();
	iImageSizes.Reset();
	iPheStillMode.Reset();
	for (TInt i = 0; i < sizes.Count(); ++i)
		{
		const TInt area = sizes[i].iWidth * sizes[i].iHeight;
		TInt at = 0;
		while (at < iImageSizes.Count() && iImageSizes[at].iWidth * iImageSizes[at].iHeight >= area)
			++at;
		User::LeaveIfError(iImageSizes.Insert(sizes[i], at));
		User::LeaveIfError(iPheStillMode.Insert(i, at));
		}
	iInfo.iNumImageSizesSupported = iImageSizes.Count();
	// white balance: the preset menu (GC2145) or the AWB switch alone (OV5640: auto, or manual red/blue gains)
	iPheWhiteBalance = EWBAuto;
	iInfo.iWhiteBalanceModesSupported = 0;
	TPheCamCtrlInfo ci;
	if (iPheSource->QueryCtrl(KV4l2CidAutoNPresetWhiteBalance, ci) == KErrNone)
		{
		static const TInt map[][2] = { { EWBDaylight, EV4l2WbDaylight }, { EWBCloudy, EV4l2WbCloudy },
			{ EWBTungsten, EV4l2WbIncandescent }, { EWBFluorescent, EV4l2WbFluorescent }, { EWBFlash, EV4l2WbFlash },
			{ EWBShade, EV4l2WbShade } };
		for (TUint i = 0; i < sizeof(map) / sizeof(map[0]); ++i)
			if (map[i][1] <= ci.iMaximum && !(ci.iMenuSkipMask & (1u << map[i][1])))
				iInfo.iWhiteBalanceModesSupported |= map[i][0];
		}
	RDebug::Printf("PH: camera-a64: plug-in camera %d: %d capture sizes (largest %dx%d), white balance modes %x", iCameraIndex,
		iImageSizes.Count(), iImageSizes.Count() ? iImageSizes[0].iWidth : 0, iImageSizes.Count() ? iImageSizes[0].iHeight : 0,
		iInfo.iWhiteBalanceModesSupported);
	}

TInt CTestCamera::PheSetWhiteBalance(CCamera::TWhiteBalance aMode)
	{
	if (!iPheSource)
		return KErrNotReady;
	TPheCamCtrlInfo ci;
	TInt r;
	if (iPheSource->QueryCtrl(KV4l2CidAutoNPresetWhiteBalance, ci) == KErrNone)
		{
		TInt v = -1;
		switch (aMode)
			{
			case EWBAuto: v = EV4l2WbAuto; break;
			case EWBDaylight: v = EV4l2WbDaylight; break;
			case EWBCloudy: v = EV4l2WbCloudy; break;
			case EWBTungsten: v = EV4l2WbIncandescent; break;
			case EWBFluorescent: v = EV4l2WbFluorescent; break;
			case EWBFlash: v = EV4l2WbFlash; break;
			case EWBShade: v = EV4l2WbShade; break;
			default: break;
			}
		if (v < 0 || (aMode != EWBAuto && !(iInfo.iWhiteBalanceModesSupported & aMode)))
			return KErrNotSupported;
		r = iPheSource->SetCtrl(KV4l2CidAutoNPresetWhiteBalance, v);
		}
	else if (aMode == EWBAuto)
		r = iPheSource->SetCtrl(KV4l2CidAutoWhiteBalance, 1);
	else
		return KErrNotSupported;
	if (r == KErrNone)
		iPheWhiteBalance = aMode;
	return r;
	}

HBufC8* PheExifApp1L(const TSize& aSize, TInt aUnit);
HBufC8* PheInsertApp1L(const TDesC8& aJpeg, const TDesC8& aApp1);

void CTestCamera::PheCaptureStill()
	{
	iImageCaptureActive = EFalse;
	delete iPheStill;
	iPheStill = NULL;
	delete iPheStillData;
	iPheStillData = NULL;
	TInt err = iPheSource && iPheStillIndex < iPheStillMode.Count() ? KErrNone : KErrNotReady;
	if (err == KErrNone)
		TRAP(err, iPheStill = iPheSource->CaptureStillL(iPheStillMode[iPheStillIndex], EFalse));
	if (err == KErrNone && (iStillImageFormat == EFormatJpeg || iStillImageFormat == EFormatExif))
		{
		if (!iPheSaver)
			iPheSaver = new CPheJpegSaver(*this);
		if (!iPheSaver)
			err = KErrNoMemory;
		else
			{
			TRAP(err, iPheSaver->StartL(*iPheStill, iPheStillData, iStillImageFormat == EFormatExif, iPheJpegQuality));
			if (err == KErrNone)
				return;		// PheEncoded() delivers
			}
		}
	else if (err == KErrNone && iStillImageFormat != EFormatFbsBitmapColor16MU)
		{
		// the asked display mode: the template's formats
		const TDisplayMode mode = iStillImageFormat == EFormatFbsBitmapColor4K ? EColor4K :
			iStillImageFormat == EFormatFbsBitmapColor64K ? EColor64K : EColor16M;
		CFbsBitmap* b = new CFbsBitmap;
		err = b ? b->Create(iPheStill->SizeInPixels(), mode) : KErrNoMemory;
		CFbsBitmapDevice* dev = NULL;
		if (err == KErrNone)
			TRAP(err, dev = CFbsBitmapDevice::NewL(b));
		CFbsBitGc* gc = NULL;
		if (err == KErrNone && (err = dev->CreateContext(gc)) == KErrNone)
			gc->BitBlt(TPoint(0, 0), iPheStill);
		delete gc;
		delete dev;
		delete iPheStill;
		iPheStill = err == KErrNone ? b : NULL;
		if (err != KErrNone)
			delete b;
		}
	PheDeliver(err);
	}

void CTestCamera::PheEncoded(TInt aError)
	{
	if (aError == KErrNone && iStillImageFormat == EFormatExif && iPheStill && iPheStillData)
		{
		HBufC8* exif = NULL;
		TRAP(aError, {
			HBufC8* app1 = PheExifApp1L(iPheStill->SizeInPixels(), iPheSource ? iPheSource->Unit() : 0);
			CleanupStack::PushL(app1);
			exif = PheInsertApp1L(*iPheStillData, *app1);
			CleanupStack::PopAndDestroy(app1);
			});
		if (exif)
			{
			delete iPheStillData;
			iPheStillData = exif;
			}
		}
	delete iPheStill;		// the encoded bytes are the still
	iPheStill = NULL;
	if (aError == KErrNone && iPheStillData && iPheStillData->Length() >= 12)
		{
		const TUint8* p = iPheStillData->Ptr();
		RDebug::Printf("PH: camera-a64: plug-in still encoded: %d bytes, %02x%02x %02x%02x %c%c%c%c", iPheStillData->Length(),
			p[0], p[1], p[2], p[3], p[6], p[7], p[8], p[9]);
		}
	else
		RDebug::Printf("PH: camera-a64: plug-in still encode -> %d", aError);
	PheDeliver(aError);
	}

void CTestCamera::PheDeliver(TInt aError)
	{
	if (iObserver2)
		{
		iObserver2->ImageBufferReady(iPheStillBuffer, aError);
		return;
		}
	// MCameraObserver::ImageReady hands the bitmap or the data to the client
	CFbsBitmap* b = aError == KErrNone ? iPheStill : NULL;
	HBufC8* d = aError == KErrNone ? iPheStillData : NULL;
	if (aError == KErrNone)
		{
		iPheStill = NULL;
		iPheStillData = NULL;
		}
	iObserver->ImageReady(b, d, aError);
	}

TInt CTestCamera::PheMenuValue(TUint32 aId, TInt aIndex, TInt& aValue)
	{
	return iPheSource ? iPheSource->MenuValue(aId, aIndex, aValue) : KErrNotReady;
	}

// MPheCameraControls
TInt CTestCamera::SetCtrl(TUint32 aId, TInt aValue)
	{
	return iPheSource ? iPheSource->SetCtrl(aId, aValue) : KErrNotReady;
	}

TInt CTestCamera::GetCtrl(TUint32 aId, TInt& aValue)
	{
	return iPheSource ? iPheSource->GetCtrl(aId, aValue) : KErrNotReady;
	}

TInt CTestCamera::QueryCtrl(TUint32 aId, TAny* aInfo)
	{
	return iPheSource ? iPheSource->QueryCtrl(aId, *static_cast<TPheCamCtrlInfo*>(aInfo)) : KErrNotReady;
	}

// a point of the upright viewfinder picture (aFrame: the size it was shown at) -> the sensor frame of the viewfinder
// mode (the inverse of CPheCamSource::Convert's turn) -> the ov5640 driver's focus zone (x << 16 | y), then a single AF
TInt CTestCamera::FocusAt(const TPoint& aPoint, const TSize& aFrame)
	{
	if (!iPheSource || aFrame.iWidth <= 0 || aFrame.iHeight <= 0)
		return KErrNotReady;
	const TPheCamFormat& f = iPheSource->Format();
	const TBool quarter = f.iRotation == 90 || f.iRotation == 270;
	const TInt uw = quarter ? f.iHeight : f.iWidth, uh = quarter ? f.iWidth : f.iHeight;
	TInt ux = aPoint.iX * uw / aFrame.iWidth, uy = aPoint.iY * uh / aFrame.iHeight;
	ux = Max(0, Min(ux, uw - 1));
	uy = Max(0, Min(uy, uh - 1));
	if (iPheMirror)
		ux = uw - 1 - ux;
	TInt sx, sy;
	switch (f.iRotation)
		{
		case 90: sy = ux; sx = f.iWidth - 1 - uy; break;
		case 180: sx = f.iWidth - 1 - ux; sy = f.iHeight - 1 - uy; break;
		case 270: sx = uy; sy = f.iHeight - 1 - ux; break;
		default: sx = ux; sy = uy; break;
		}
	TInt r = iPheSource->SetCtrl(KV4l2CidOv5640FocusZone, (sx << 16) | sy);
	if (r == KErrNone)
		r = iPheSource->SetCtrl(KV4l2CidAutoFocusStart, 1);
	RDebug::Printf("PH: camera-a64: plug-in focus at %d,%d of %dx%d -> sensor %d,%d: %d", aPoint.iX, aPoint.iY, aFrame.iWidth,
		aFrame.iHeight, sx, sy, r);
	return r;
	}

// ---- CPheJpegSaver ----
CPheJpegSaver::~CPheJpegSaver()
	{
	Cancel();
	delete iEncoder;
	delete iFrameData;
	}

void CPheJpegSaver::StartL(const CFbsBitmap& aBitmap, HBufC8*& aData, TBool aExif, TInt aQuality)
	{
	Cancel();
	delete iEncoder;
	iEncoder = NULL;
	delete iFrameData;
	iFrameData = NULL;
	// aExif: the plain JPEG here; CTestCamera::PheEncoded() adds our own APP1 (the image resolves no Exif-capable
	// ICL encoder, KErrNotFound on the PinePhone 2026-11-17)
	(void)aExif;
	iEncoder = CImageEncoder::DataNewL(aData, _L8("image/jpeg"));
	if (aQuality > 0)
		{
		iFrameData = CFrameImageData::NewL();
		TJpegImageData* j = new (ELeave) TJpegImageData;
		j->iSampleScheme = TJpegImageData::EColor420;
		j->iQualityFactor = aQuality;
		CleanupStack::PushL(j);
		User::LeaveIfError(iFrameData->AppendImageData(j));
		CleanupStack::Pop(j);
		}
	iEncoder->Convert(&iStatus, aBitmap, iFrameData);
	SetActive();
	}

void CPheJpegSaver::RunL()
	{
	delete iEncoder;
	iEncoder = NULL;
	iOwner.PheEncoded(iStatus.Int());
	}

void CPheJpegSaver::DoCancel()
	{
	if (iEncoder)
		iEncoder->Cancel();
	}

// ---- CPheDirectViewFinder ----
void CPheDirectViewFinder::Release()
	{
	iOwner.iPheDirectVf = NULL;
	iOwner.iPheVfPaused = EFalse;
	delete this;
	}

void CPheDirectViewFinder::PauseViewFinderDirectL()
	{
	iOwner.iPheVfPaused = ETrue;
	}

void CPheDirectViewFinder::ResumeViewFinderDirectL()
	{
	iOwner.iPheVfPaused = EFalse;
	}

CCamera::CCameraDirectViewFinder::TViewFinderState CPheDirectViewFinder::ViewFinderState() const
	{
	if (iOwner.iPheVfPaused)
		return CCamera::CCameraDirectViewFinder::EViewFinderPause;
	return iOwner.iViewFinderActive ? CCamera::CCameraDirectViewFinder::EViewFinderActive :
		CCamera::CCameraDirectViewFinder::EViewFinderInActive;
	}

// ---- our own Exif APP1 (Exif 2.3, little-endian TIFF), so EFormatExif does not depend on an ICL Exif encoder ----

static void Put16(TUint8* aP, TUint aV) { aP[0] = (TUint8)aV; aP[1] = (TUint8)(aV >> 8); }
static void Put32(TUint8* aP, TUint aV) { Put16(aP, aV & 0xFFFF); Put16(aP + 2, aV >> 16); }

// one 12-byte IFD entry; aValue = the value itself (fits in 4 bytes) or the offset of the data from the TIFF header
static void Entry(TUint8*& aP, TUint aTag, TUint aType, TUint aCount, TUint aValue)
	{
	Put16(aP, aTag);
	Put16(aP + 2, aType);
	Put32(aP + 4, aCount);
	if (aType == 3 && aCount == 1)
		{
		Put16(aP + 8, aValue);
		Put16(aP + 10, 0);
		}
	else
		Put32(aP + 8, aValue);
	aP += 12;
	}

// the APP1 segment for a still of aSize (already upright: orientation 1). Make/Model as Megapixels' PinePhone config;
// FNumber and FocalLength from the same config (rear f/3.0 3.33 mm, front f/2.8 2.6 mm). No exposure time: the
// sensors report exposure in lines, not seconds.
HBufC8* PheExifApp1L(const TSize& aSize, TInt aUnit)
	{
	enum { EAscii = 2, EShort = 3, ELong = 4, ERational = 5, EUndefined = 7 };
	_LIT8(KMake, "PINE64");
	_LIT8(KModel, "PinePhone");
	_LIT8(KSoftware, "Phorepoc camera-a64");
	TBuf8<20> when;		// "YYYY:MM:DD HH:MM:SS" + NUL
	TTime now;
	now.HomeTime();
	const TDateTime d = now.DateTime();
	when.Format(_L8("%04d:%02d:%02d %02d:%02d:%02d"), d.Year(), (TInt)d.Month() + 1, d.Day() + 1, d.Hour(), d.Minute(), d.Second());
	when.Append(TChar(0));

	const TUint KIfd0 = 8, KIfd0Entries = 6;
	const TUint KExifIfd = KIfd0 + 2 + KIfd0Entries * 12 + 4, KExifEntries = 7;
	TUint data = KExifIfd + 2 + KExifEntries * 12 + 4;
	const TUint make = data; data += KMake().Length() + 1;
	const TUint model = data; data += KModel().Length() + 1;
	const TUint software = data; data += KSoftware().Length() + 1;
	const TUint dateTime = data; data += 20;
	const TUint fnumber = data; data += 8;
	const TUint focal = data; data += 8;
	const TUint tiffLen = data;

	HBufC8* app1 = HBufC8::NewL(4 + 6 + tiffLen);
	TPtr8 a = app1->Des();
	a.SetLength(4 + 6 + tiffLen);
	a.FillZ();
	TUint8* b = &a[0];
	b[0] = 0xFF;
	b[1] = 0xE1;
	b[2] = (TUint8)((2 + 6 + tiffLen) >> 8);	// the length counts itself, not the marker
	b[3] = (TUint8)(2 + 6 + tiffLen);
	Mem::Copy(b + 4, "Exif\0\0", 6);
	TUint8* t = b + 10;		// the TIFF header: offsets count from here
	t[0] = 'I';
	t[1] = 'I';
	Put16(t + 2, 42);
	Put32(t + 4, KIfd0);

	TUint8* p = t + KIfd0;
	Put16(p, KIfd0Entries);
	p += 2;
	Entry(p, 0x010F, EAscii, KMake().Length() + 1, make);
	Entry(p, 0x0110, EAscii, KModel().Length() + 1, model);
	Entry(p, 0x0112, EShort, 1, 1);				// Orientation: top-left (the pixels are already upright)
	Entry(p, 0x0131, EAscii, KSoftware().Length() + 1, software);
	Entry(p, 0x0132, EAscii, 20, dateTime);
	Entry(p, 0x8769, ELong, 1, KExifIfd);		// the Exif IFD
	Put32(p, 0);								// no IFD1
	p = t + KExifIfd;
	Put16(p, KExifEntries);
	p += 2;
	Entry(p, 0x829D, ERational, 1, fnumber);
	Entry(p, 0x9000, EUndefined, 4, '0' | '2' << 8 | '3' << 16 | '0' << 24);	// ExifVersion "0230"
	Entry(p, 0x9003, EAscii, 20, dateTime);	// DateTimeOriginal
	Entry(p, 0x9004, EAscii, 20, dateTime);	// DateTimeDigitized
	Entry(p, 0x920A, ERational, 1, focal);
	Entry(p, 0xA002, ELong, 1, aSize.iWidth);	// PixelXDimension
	Entry(p, 0xA003, ELong, 1, aSize.iHeight);
	Put32(p, 0);

	Mem::Copy(t + make, KMake().Ptr(), KMake().Length());
	Mem::Copy(t + model, KModel().Ptr(), KModel().Length());
	Mem::Copy(t + software, KSoftware().Ptr(), KSoftware().Length());
	Mem::Copy(t + dateTime, when.Ptr(), 20);
	Put32(t + fnumber, aUnit == 0 ? 30 : 28);
	Put32(t + fnumber + 4, 10);
	Put32(t + focal, aUnit == 0 ? 333 : 260);
	Put32(t + focal + 4, 100);
	return app1;
	}

// insert aApp1 after the JPEG's SOI and its JFIF APP0 (JFIF wants APP0 first; Exif readers find APP1 after it)
HBufC8* PheInsertApp1L(const TDesC8& aJpeg, const TDesC8& aApp1)
	{
	TInt at = 2;
	if (aJpeg.Length() > 6 && aJpeg[2] == 0xFF && aJpeg[3] == 0xE0)
		at = 4 + (aJpeg[4] << 8 | aJpeg[5]);
	HBufC8* out = HBufC8::NewL(aJpeg.Length() + aApp1.Length());
	TPtr8 o = out->Des();
	o.Copy(aJpeg.Left(at));
	o.Append(aApp1);
	o.Append(aJpeg.Mid(at));
	return out;
	}
