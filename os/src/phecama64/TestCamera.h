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

#ifndef __TESTCAMERA_H__
#define __TESTCAMERA_H__


#include <ecam/ecamplugin.h>
#include <ecam/ecaminfoplugin.h>
#include <bitdev.h>
#include <ecam.h>
#include <w32std.h>
#include <ecamadvsettings.h>
#include <ecam/mcameradirectviewfinder.h>

_LIT(KTestCameraPluginName, "C:\\102070cc.txt");

const TInt KMinTestCameraZoom = -3;
const TInt KMaxTestCameraZoom = 3;
const TReal32 KMaxTestCameraDigitalZoomFactor = 1.0;
const TInt KMaxTestCameraDigitalZoom = 0;
const TInt KFramePosIncrement = 5;
const TInt KFrameFeatureSize = 25;
const TInt KFrameFeatureBorderThickness = 5;
const enum CCamera::TFormat KInitialViewFinderFormat = CCamera::EFormatFbsBitmapColor16M;
const TDisplayMode KViewFinderDisplayMode = EColor16MU;	// OURS (was EColor4K: 4 bits/channel posterised the near-grey live picture into magenta/green patches, boot 933)

class CTestCamAdvSet;
class CTestCamPresets;
class CTestCamSnapshot;
class CTestCamImgProc;
class CTestCamera;
class CImageEncoder;
class CFrameImageData;

// OURS (camera-a64): a still as ECam delivers it -- a bitmap (the EFormatFbsBitmap* formats) or encoded bytes
// (EFormatJpeg / EFormatExif); the plug-in owns both until the next capture
class RPheStillBuffer : public MCameraBuffer
	{
public:
	RPheStillBuffer(CFbsBitmap*& aBitmap, HBufC8*& aData) : iBitmap(aBitmap), iData(aData) {}
	virtual TInt NumFrames() { return 1; }
	virtual TDesC8* DataL(TInt aFrameIndex) { if (aFrameIndex != 0 || !iData) User::Leave(KErrNotSupported); return iData; }
	virtual CFbsBitmap& BitmapL(TInt aFrameIndex) { if (aFrameIndex != 0 || !iBitmap) User::Leave(KErrNotSupported); return *iBitmap; }
	virtual RChunk& ChunkL() { User::Leave(KErrNotSupported); return iChunk; }
	virtual TInt ChunkOffsetL(TInt /*aFrameIndex*/) { User::Leave(KErrNotSupported); return 0; }
	virtual TInt FrameSize(TInt aFrameIndex) { return aFrameIndex == 0 && iData ? iData->Length() : KErrNotSupported; }
	virtual void Release() {}
private:
	CFbsBitmap*& iBitmap;
	HBufC8*& iData;
	RChunk iChunk;
	};

// OURS: ICL's JPEG encoder on a still (Exif: the CJPEGExifEncoder class of the same codec, so the codec writes APP1)
class CPheJpegSaver : public CActive
	{
public:
	CPheJpegSaver(CTestCamera& aOwner) : CActive(EPriorityStandard), iOwner(aOwner) { CActiveScheduler::Add(this); }
	~CPheJpegSaver();
	void StartL(const CFbsBitmap& aBitmap, HBufC8*& aData, TBool aExif, TInt aQuality);
private:
	void RunL();
	void DoCancel();
	CTestCamera& iOwner;
	CImageEncoder* iEncoder;
	CFrameImageData* iFrameData;
	};

// OURS: CCamera::CCameraDirectViewFinder -- pause/resume of the direct viewfinder
class CPheDirectViewFinder : public CBase, public MCameraDirectViewFinder
	{
public:
	CPheDirectViewFinder(CTestCamera& aOwner) : iOwner(aOwner) {}
	void Release();
	void PauseViewFinderDirectL();
	void ResumeViewFinderDirectL();
	CCamera::CCameraDirectViewFinder::TViewFinderState ViewFinderState() const;
private:
	CTestCamera& iOwner;
	};

// OURS: the camera's own V4L2 controls, for a camera app that wants more than ECam names (phecamera): custom interface
// KPheCameraControlsUid. Control ids and values are V4L2's (phecam_api.h).
const TUid KPheCameraControlsUid = { 0xE7015C66 };
class MPheCameraControls
	{
public:
	virtual TInt SetCtrl(TUint32 aId, TInt aValue) = 0;
	virtual TInt GetCtrl(TUint32 aId, TInt& aValue) = 0;
	virtual TInt QueryCtrl(TUint32 aId, TAny* aInfo) = 0;		// a TPheCamCtrlInfo
	virtual TInt FocusAt(const TPoint& aPoint, const TSize& aFrame) = 0;	// a point of the upright viewfinder picture: single AF there
	};

class RTestCameraFrameBuffer : public MCameraBuffer
	{
public:
	RTestCameraFrameBuffer(CFbsBitmap*& aFrame): iAvailable(ETrue), iFrame(aFrame) {}
	virtual TInt NumFrames() {return 0;}
	virtual TDesC8* DataL(TInt aFrameIndex) {if (aFrameIndex != 0) { User::Leave(KErrArgument); } return NULL; }
	virtual CFbsBitmap& BitmapL(TInt aFrameIndex) {if (aFrameIndex != 0) { User::Leave(KErrArgument); } return *iFrame;}
 	virtual	RChunk& ChunkL() { return iChunk;}
	virtual TInt ChunkOffsetL(TInt aFrameIndex) {if (aFrameIndex != 0) { User::Leave(KErrArgument); } return 0;}
	virtual TInt FrameSize(TInt aFrameIndex) {if (aFrameIndex != 0) { User::Leave(KErrArgument); } return 0; }
	virtual void Release() {iAvailable = ETrue;}
public:	
	volatile TBool iAvailable;
	CFbsBitmap*& iFrame;
	RChunk iChunk;
	};

class RTestFrameBuffer : public MFrameBuffer
	{
public:
	RTestFrameBuffer(CFbsBitmap*& aFrame): iAvailable(ETrue), iFrame(aFrame) {}
	virtual TDesC8* DataL(TInt /*aIndex*/) { User::Leave(KErrNotSupported); return NULL; }
	virtual CFbsBitmap* FrameL(TInt aIndex) { if (aIndex != 0) User::Leave(KErrNotSupported); return iFrame; }
	virtual void Release() { iAvailable = ETrue; }
public:
	TBool iAvailable;
	CFbsBitmap*& iFrame;
	};

class CDirectScreenAccess;
class CPheCamSource;	// OURS: the camera server pixel source (phecamsource.h)

class CTestCamera : public CCameraPlugin, public MDirectScreenAccess, public MPheCameraControls
	{
	friend class CTestCamAdvSet;
	friend class CTestCamPresets;
	friend class CTestCamSnapshot;
	friend class CTestCamImgProc;
	friend class CPheJpegSaver;
	friend class CPheDirectViewFinder;
public:
	// OURS (camera-a64): MPheCameraControls
	TInt SetCtrl(TUint32 aId, TInt aValue);
	TInt GetCtrl(TUint32 aId, TInt& aValue);
	TInt QueryCtrl(TUint32 aId, TAny* aInfo);
	TInt FocusAt(const TPoint& aPoint, const TSize& aFrame);
	CPheCamSource* PheSource() const { return iPheSource; }
	TInt PheMenuValue(TUint32 aId, TInt aIndex, TInt& aValue);
	TInt PheSetWhiteBalance(CCamera::TWhiteBalance aMode);
	TInt PheWhiteBalanceModes() const { return iInfo.iWhiteBalanceModesSupported; }
private:
	void PheInitL();
	void PheCaptureStill();
	void PheDeliver(TInt aError);
	void PheEncoded(TInt aError);
	
public:
	static CTestCamera* NewL();
	~CTestCamera();
	void ConstructL();
	// from CCameraPlugin
	void Construct2L(MCameraObserver& aObserver,TInt aCameraIndex);
	void Construct2DupL(MCameraObserver& aObserver,TInt aCameraHandle);
	void Construct2L(MCameraObserver2& aObserver,TInt aCameraIndex,TInt aPriority);
	void Construct2DupL(MCameraObserver2& aObserver,TInt aCameraHandle);
private:
	// From CCamera
	virtual void CameraInfo(TCameraInfo& aInfo) const;
	virtual void Reserve();
	virtual void Release();
	virtual void PowerOn();
	virtual void PowerOff();
	virtual TInt Handle();

	virtual void SetZoomFactorL(TInt aZoomFactor = 0);
	virtual TInt ZoomFactor() const;
	virtual void SetDigitalZoomFactorL(TInt aDigitalZoomFactor = 0);
	virtual TInt DigitalZoomFactor() const;

	virtual void SetContrastL(TInt aContrast);
	virtual TInt Contrast() const;
	virtual void SetBrightnessL(TInt aBrightness);
	virtual TInt Brightness() const;
	virtual void SetFlashL(TFlash aFlash = EFlashNone);
	virtual TFlash Flash() const;
	virtual void SetExposureL(TExposure aExposure = EExposureAuto);
	virtual TExposure Exposure() const;
	virtual void SetWhiteBalanceL(TWhiteBalance aWhiteBalance = EWBAuto);
	virtual TWhiteBalance WhiteBalance() const;

	virtual void StartViewFinderDirectL(RWsSession& aWs,CWsScreenDevice& aScreenDevice,RWindowBase& aWindow,TRect& aScreenRect);
	virtual void StartViewFinderDirectL(RWsSession& aWs,CWsScreenDevice& aScreenDevice,RWindowBase& aWindow,TRect& aScreenRect,TRect& aClipRect);
	virtual void StartViewFinderBitmapsL(TSize& aSize);
	virtual void StartViewFinderBitmapsL(TSize& aSize,TRect& aClipRect);
	virtual void StartViewFinderL(TFormat aImageFormat,TSize& aSize);
	virtual void StartViewFinderL(TFormat aImageFormat,TSize& aSize,TRect& aClipRect);
	virtual void StopViewFinder();
	virtual TBool ViewFinderActive() const;
	virtual void SetViewFinderMirrorL(TBool aMirror);
	virtual TBool ViewFinderMirror() const;

	virtual void PrepareImageCaptureL(TFormat aImageFormat,TInt aSizeIndex);
	virtual void PrepareImageCaptureL(TFormat aImageFormat,TInt aSizeIndex,const TRect& aClipRect);
	virtual void CaptureImage();
	virtual void CancelCaptureImage();
	virtual void EnumerateCaptureSizes(TSize& aSize,TInt aSizeIndex,TFormat aFormat) const;

	virtual void PrepareVideoCaptureL(TFormat aFormat,TInt aSizeIndex,TInt aRateIndex,TInt aBuffersToUse,TInt aFramesPerBuffer);
	virtual void PrepareVideoCaptureL(TFormat aFormat,TInt aSizeIndex,TInt aRateIndex,TInt aBuffersToUse,TInt aFramesPerBuffer,const TRect& aClipRect);
	virtual void StartVideoCapture();
	virtual void StopVideoCapture();

	virtual TBool VideoCaptureActive() const;
	virtual void EnumerateVideoFrameSizes(TSize& aSize,TInt aSizeIndex,TFormat aFormat) const;
	virtual void EnumerateVideoFrameRates(TReal32& aRate,TInt aRateIndex,TFormat aFormat,TInt aSizeIndex,TExposure aExposure = EExposureAuto) const;
	virtual void GetFrameSize(TSize& aSize) const;
	virtual TReal32 FrameRate() const;
	virtual TInt BuffersInUse() const;
	virtual TInt FramesPerBuffer() const;

	virtual void SetJpegQuality(TInt aQuality);
	virtual TInt JpegQuality() const;

	virtual TAny* CustomInterface(TUid aInterface);

	// From MAbortDirectScreenAccess
	virtual void AbortNow(RDirectScreenAccess::TTerminationReasons aReason);

	// From MDirectScreenAccess
	virtual void Restart(RDirectScreenAccess::TTerminationReasons aReason);

	// New functions
	static TInt TimerCallBack(TAny* aThis);
	void FrameReady();
	void GetFrame(CFbsBitGc& aGc, TPoint& aPos) const;
	void GetImgProcessedFrame(CFbsBitGc& aGc, TPoint& aPos) const;
	void ServiceVideo(TInt aBuferNum);
	void ServiceViewFinder(CFbsBitmap* aSourceFrame);
	void ServiceImageCapture(const CFbsBitGc& aSourceBitmapGc);
	void ServiceBurstImageCapture(const CFbsBitGc& aSourceBitmapGc);
	void DoCaptureImageL();
	void CheckReserveAndPowerL();
	TInt CheckReserveAndPower() const;
	void CustomInterfaceL(TAny*& aIface, TUid aInterface);
	static TInt ReserveCallBack(TAny* aThis);
	static TInt PowerOnCallBack(TAny* aThis);
	static TInt ImageCaptureCallBack(TAny* aThis);
	static TInt VideoCaptureCallBack(TAny* aThis);
	static TInt HandleEventCallBack(TAny* aThis);
	static TInt HandleEvent2CallBack(TAny* aThis);
	
private:
	 CTestCamera();	
private:
	MCameraObserver* iObserver; // not owned
	MCameraObserver2* iObserver2; // not owned
	TBool iReserved;
	TBool iPowerOn;

	TCameraInfo iInfo;
	CPeriodic* iTimer;
	CPheCamSource* iPheSource;	// OURS
	TInt iPheDelivered, iPheFallback;	// OURS: driver frames already delivered; timer ticks with no new driver frame
	TTimeIntervalMicroSeconds32 iFrameDelay;
	TInt iFrameIndex;
	TTimeIntervalMicroSeconds iElapsedTime;
	TPoint iStillImagePos;
	TPoint iVideoFramePos;

	TReal32 iVideoFrameRate;
	TInt iBuffersInUse;
	TSize iVideoFrameSize;
	TFormat iVideoFrameFormat;

	CFbsBitmap* iVideoFrame1;
	CFbsBitmapDevice* iVideoFrameDev1;
	CFbsBitGc* iVideoFrameGc1;
	CFbsBitmap* iVideoFrame2;
	CFbsBitmapDevice* iVideoFrameDev2;
	CFbsBitGc* iVideoFrameGc2;

	TInt iStillCaptureErr;
	TSize iStillImageSize;
	TFormat iStillImageFormat;
	CFbsBitmap* iStillImage;
	CFbsBitmapDevice* iStillImageDev;
	CFbsBitGc* iStillImageGc;

	TSize iViewFinderSize;
	TFormat iViewFinderFormat;
	CFbsBitmap* iViewFinderBitmap;
	CFbsBitmapDevice* iViewFinderBitmapDev;
	CFbsBitGc* iViewFinderBitmapGc;

	CDirectScreenAccess* iDSA;
	CFbsScreenDevice* iScreenDev;
	CFbsBitGc* iScreenGc;
	TRect iScreenRect;
	TRect iClipRect;

	CFbsBitmap* iImage;
	CFbsBitmapDevice* iDev;
	CFbsBitGc* iGc;

	CAsyncCallBack iReserveAsync;
	CAsyncCallBack iPowerOnAsync;
	CAsyncCallBack iImageCaptureAsync;
	CAsyncCallBack iVideoCaptureAsync;
	CAsyncCallBack iHandleEventAsync;
	CAsyncCallBack iHandleEvent2Async;

	RTestFrameBuffer iFrameBuffer1;
	RTestFrameBuffer iFrameBuffer2;
	
	RTestCameraFrameBuffer iFrameCameraBuffer1;
	RTestCameraFrameBuffer iFrameCameraBuffer2;

	TInt iVideoCaptureErr;
	// Members to store the current optical or digital value set with SetZoomFactorL() and SetDigitalZoomFactorL()
	// Although arguments have trailing Factor in fact they must contain values in the ranges specified by
	//  	iInfo.iMinZoom : iInfo.iMaxZoom for optical zoom and 
	//		0 : iInfo.iMaxDigitalZoom for digital zoom 
	// The factor values cannot be modified using Camera API as it is assumed 
	// that they are determined by the camera hardware.  
	TInt iZoom;
	TInt iDigitalZoom;
	
	TInt iContrast;
	TInt iBrightness;
	
	TInt iImgProcBrightness;
	TInt iImgProcContrast;
	
	volatile TBool iViewFinderActive;
	volatile TBool iVideoCaptureActive;
	volatile TBool iImageCaptureActive;
	volatile TBool iVideoPreparedLast;
	volatile TBool iVideoCapturePrepared;
	volatile TBool iImageCapturePrepared;

	RArray<TSize> iImageSizes;
	RArray<TSize> iVideoFrameSizes;
	RArray<TReal32> iVideoFrameRates;
	TInt iCameraIndex;
	TInt iCameraHandle;
	TInt iPriority;	
	
	TECAMEvent iECamEvent;
	TECAMEvent2 iECamEvent2;
	TInt iEventError;
	
	CTestCamAdvSet* iAdvSettingsImpl;
	CTestCamSnapshot* iSnapshotImpl;
	CTestCamImgProc* iImgProcImpl;
	// OURS (camera-a64): the still path
	RArray<TInt> iPheStillMode;		// ECam size index -> the source's mode index (largest first)
	TInt iPheStillIndex;
	TBool iPheMirror, iPheVfPaused;
	TInt iPheJpegQuality;		// 0 = the codec's default
	CCamera::TWhiteBalance iPheWhiteBalance;
	CFbsBitmap* iPheStill;
	HBufC8* iPheStillData;
	RPheStillBuffer iPheStillBuffer;
	CPheJpegSaver* iPheSaver;
	CPheDirectViewFinder* iPheDirectVf;
	
	CCamera::CCameraAdvancedSettings::TDriveMode iDriveMode;
	TInt iBurstImages;
	};
	
class CTestCameraInfo : public CCameraInfoPlugin
	{
public:
	static CTestCameraInfo* NewL();
	~CTestCameraInfo();
	// from CCameraInfoPlugin
	TInt CamerasAvailable();
private:
	CTestCameraInfo();	
	};
	
enum TStubPanicReasons
	{
	EPanicUnimplemented=1
	};	

class FileDependencyUtil
	{
public:
	static void CheckFileDependencyL();
	};

class CDataGlobal : public CBase
	{
public:
	CTestCamAdvSet* iTestCamAdvSet;
	CTestCamPresets* iTestCamPresets;
	CTestCamSnapshot* iTestCamSnapshot;
	CTestCamImgProc* iTestCamImgProc;
	TInt 		iAdvSetReferenceCount;	
	TInt 		iPresetsReferenceCount;
	TInt 		iSnapshotReferenceCount;	
	TInt 		iImgProcReferenceCount;
	};
	
#endif
