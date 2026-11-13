// Copyright The Phorepoc Authors
// SPDX-License-Identifier: GPL-2.0-only
//
// phe_sensor.cpp -- the sensor interface of the 2010 template camera PDD (third_party/symbian2010-camerasc/template_pdd,
// unmodified: DTemplateCameraScPddFactory / DTemplateCameraScPdd, the DCameraScPdd the generic camera LDD talks to),
// implemented over the Linux camera stack instead of the template's fake sensor. The template declares the class
// (camerasc_plat.h, DTemplateSensorIf); this file is its implementation for the PinePhone:
//   DoCreate          -> phe_camera_init(): the static DT, the Linux drivers' probes (i2c-gpio, ov5640, gc2145, sun6i-csi)
//   SetConfig         -> remember the LDD's frame size / pixel format
//   Start             -> phe_camera_configure() + phe_camera_start() (sensor link, pad formats, vb2 MMAP buffers, stream on)
//   CaptureNextImage  -> queue the LDD buffer; a DFC on the PDD's queue dequeues the next frame into it
//   Stop              -> phe_camera_stop()
// The frames come through vb2's own buffers and are copied into the LDD's chunk buffer (the Linux code path stays the
// Linux MMAP one); zero-copy (vb2 on the chunk's physical pages) is a later step.
// Unit 0 is the rear OV5640 (the template PDD has one unit).

#include "camerasc_plat.h"
#include "phe_camera.h"

void PheCameraPinmux();		// phe_kern.cpp

static TDfcQue *CameraDfcQ;
TDfcQue *PheCameraDfcQ() { return CameraDfcQ; }

// V4L2 / media-bus codes (linux/include/uapi/linux/videodev2.h, media-bus-format.h)
static const TUint32 KV4l2PixFmtUyvy = 0x59565955;	// v4l2_fourcc('U','Y','V','Y')
static const TUint32 KMediaBusUyvy8_2x8 = 0x2006;	// MEDIA_BUS_FMT_UYVY8_2X8

static const SDevCamFrameSize FrameSizes[] =
	{
		{ 640, 480, 1, 30 },		// VGA (ov5640_mode_data VGA)
		{ 1280, 720, 1, 30 },		// 720p
	};
static const TInt KNumFrameSizes = sizeof(FrameSizes) / sizeof(SDevCamFrameSize);

static const SDevCamPixelFormat PixelFormats[] =
	{
		{ EUidPixelFormatYUV_422Interleaved, KNumFrameSizes, 2 },	// image
		{ EUidPixelFormatYUV_422Interleaved, KNumFrameSizes, 2 },	// video
		{ EUidPixelFormatYUV_422Interleaved, KNumFrameSizes, 2 }	// viewfinder
	};

// the buffers the LDD gave us, in order (KTotalCameraRequests deep)
struct TPending { TLinAddr iLin; TPhysAddr iPhys; };
static TPending Pending[KTotalCameraRequests];
static TInt PendingHead, PendingCount;
static TUint FrameBytes;

TInt DSensorIf::SetConfig(const TCameraConfigV02& aConfig)
	{
	if (aConfig.iFlashMode != ECamFlashNone || aConfig.iExposureMode != ECamExposureAuto || aConfig.iZoom != 0)
		return KErrNotSupported;
	iConfig = aConfig;
	iWidth = aConfig.iFrameSize.iWidth;
	iHeight = aConfig.iFrameSize.iHeight;
	iLineOffset = iWidth * iConfig.iPixelFormat.iPixelWidthInBytes;
	return KErrNone;
	}

DTemplateSensorIf::DTemplateSensorIf(MSensorObserver& aObserver, TDfcQue* aDFCQueue)
	: iDFCQueue(aDFCQueue)
	{
	iObserver = &aObserver;
	CameraDfcQ = aDFCQueue;
	}

DTemplateSensorIf::~DTemplateSensorIf()
	{
	phe_camera_stop();
	delete iImageTimerDFCs[0];
	}

static void FrameDfc(TAny* aSensorIf)
	{
	((DTemplateSensorIf*)aSensorIf)->BufferDoneCallback(KErrNone);
	}

TInt DTemplateSensorIf::DoCreate()
	{
	iImageTimerDFCs[0] = new TDfc(FrameDfc, this, iDFCQueue, 0);
	iImageTimerDFCs[1] = NULL;
	if (!iImageTimerDFCs[0])
		return KErrNoMemory;
	PheCameraPinmux();
	TInt r = phe_camera_init();
	if (r < 0)
		{
		Kern::Printf("camera-a64: Linux probe failed (%d)", r);
		return KErrNotFound;
		}
	return phe_camera_count() > 0 ? KErrNone : KErrNotFound;
	}

// one frame into the oldest pending buffer, then hand it to the PDD (which returns the LDD's next buffer)
TInt DTemplateSensorIf::BufferDoneCallback(TInt aResult)
	{
	if (!iEnabled || !PendingCount)
		return KErrNone;
	TPending p = Pending[PendingHead];
	TInt n = phe_camera_next_frame((void*)p.iLin, FrameBytes);
	PendingHead = (PendingHead + 1) % KTotalCameraRequests;
	PendingCount--;
	NKern::LockedDec(iPendingRequests);
	TInt r = iObserver->NotifyImageCaptureEvent(n < 0 ? KErrGeneral : aResult, p.iLin, p.iPhys);
	if (r == KErrNone && iEnabled)
		CaptureNextImage(p.iLin, p.iPhys);
	return r;
	}

void DTemplateSensorIf::FillBuffer(TLinAddr) {}

TInt DTemplateSensorIf::FrameSizeCaps(TDevCamCaptureMode, TUidPixelFormat, TDes8& aFrameSizeCapsBuf)
	{
	TPtrC8 sizes((const TUint8*)FrameSizes, sizeof(FrameSizes));
	if (aFrameSizeCapsBuf.MaxSize() < sizes.Size())
		return KErrArgument;
	aFrameSizeCapsBuf = sizes;
	return KErrNone;
	}

TInt DTemplateSensorIf::GetCaps(TCameraCapsV02*& aCameraCaps)
	{
	TInt size = sizeof(TCameraCapsV02) + sizeof(PixelFormats);
	TUint8* caps = new TUint8[size];
	if (!caps)
		return KErrNoMemory;
	memclr(caps, size);
	aCameraCaps = (TCameraCapsV02*)caps;
	aCameraCaps->iFlashModes = ECamFlashNone;
	aCameraCaps->iExposureModes = ECamExposureAuto;
	aCameraCaps->iWhiteBalanceModes = ECamWBAuto;
	aCameraCaps->iMinZoom = 0;
	aCameraCaps->iMaxZoom = 0;
	aCameraCaps->iCapsMisc = 0;
	aCameraCaps->iOrientation = ECamOrientationOutwards;	// unit 0 = the rear camera
	aCameraCaps->iNumImagePixelFormats = 1;
	aCameraCaps->iNumVideoPixelFormats = 1;
	aCameraCaps->iNumViewFinderPixelFormats = 1;
	for (TInt i = 0; i < ECamAttributeMax; i++)
		{
		aCameraCaps->iDynamicRange[i].iMin = 0;
		aCameraCaps->iDynamicRange[i].iMax = 0;
		aCameraCaps->iDynamicRange[i].iDefault = 0;
		}
	memcpy(caps + sizeof(TCameraCapsV02), PixelFormats, sizeof(PixelFormats));
	return size;
	}

TInt DTemplateSensorIf::RequestPower() { return KErrNone; }		// the drivers' runtime PM powers the sensor on stream
TInt DTemplateSensorIf::RelinquishPower() { return KErrNone; }

TInt DTemplateSensorIf::Start(TDevCamCaptureMode, TLinAddr aLinAddr, TPhysAddr aPhysAddr)
	{
	unsigned int bpl = 0, size = 0;
	TInt r = phe_camera_configure(0, iWidth, iHeight, KV4l2PixFmtUyvy, KMediaBusUyvy8_2x8, &bpl, &size);
	if (r < 0)
		return KErrNotSupported;
	FrameBytes = size;
	if ((r = phe_camera_start(KTotalCameraRequests + 1)) < 0)
		return KErrGeneral;
	PendingHead = PendingCount = 0;
	return CaptureNextImage(aLinAddr, aPhysAddr);
	}

TInt DTemplateSensorIf::CaptureNextImage(TLinAddr aLinAddr, TPhysAddr aPhysAddr)
	{
	if (PendingCount >= KTotalCameraRequests)
		return KErrNotReady;
	Pending[(PendingHead + PendingCount) % KTotalCameraRequests].iLin = aLinAddr;
	Pending[(PendingHead + PendingCount) % KTotalCameraRequests].iPhys = aPhysAddr;
	PendingCount++;
	NKern::LockedInc(iPendingRequests);
	iEnabled = ETrue;
	iImageTimerDFCs[0]->Enque();
	return KErrNone;
	}

TInt DTemplateSensorIf::Stop()
	{
	iEnabled = EFalse;
	iPendingRequests = iNextRequest = 0;
	PendingHead = PendingCount = 0;
	iImageTimerDFCs[0]->Cancel();
	phe_camera_stop();
	return KErrNone;
	}

TInt DTemplateSensorIf::HostPowerCallback(TAny*, TAny*) { return KErrNone; }
TInt DTemplateSensorIf::SensorClkReqCallback(TAny*) { return KErrNone; }
