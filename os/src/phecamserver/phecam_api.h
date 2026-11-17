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
	EPheCamStart = 2,		// {buffers (0 = the default 3)}
	EPheCamStop = 3,		// {keep power}: 1 = the sensor stays powered until the next Start
	EPheCamNextFrame = 4,		// async: the next frame into the descriptor in slot 0
	EPheCamNextFrameCancel = 5,
	EPheCamEnumSize = 6,		// {unit, index} -> TSize in slot 2; KErrNotFound past the last (VIDIOC_SUBDEV_ENUM_FRAME_SIZE)
	EPheCamSetCtrl = 7,		// {unit, V4L2 control id, value} (VIDIOC_S_CTRL)
	EPheCamGetCtrl = 8,		// {unit, id} -> TInt in slot 2 (VIDIOC_G_CTRL)
	EPheCamQueryCtrl = 9,		// {unit, id} -> TPheCamCtrlInfo in slot 2 (VIDIOC_QUERYCTRL); KErrNotSupported = no such control
	EPheCamMenuValue = 10		// {unit, id, index} -> TInt in slot 3: an integer menu's value (VIDIOC_QUERYMENU .value)
	};

// iRotation: the sensor's V4L2_CID_CAMERA_SENSOR_ROTATION (from the DT "rotation" property): the correction, in degrees
// counter-clockwise, a consumer applies to captured images to show them upright (PinePhone: rear 270, front 90)
struct TPheCamFormat { TInt iWidth, iHeight, iBytesPerLine, iSizeImage, iRotation; };

// struct v4l2_queryctrl, the fields a client needs; iMenuSkipMask: bit n set = menu item n not offered
struct TPheCamCtrlInfo { TUint32 iType, iFlags, iMenuSkipMask; TInt iMinimum, iMaximum, iStep, iDefault; };

// V4L2 control ids (v4l2-controls.h) the plug-in uses
const TUint32 KV4l2CidBase = 0x00980900;			// V4L2_CID_USER_BASE
const TUint32 KV4l2CidAutoWhiteBalance = KV4l2CidBase + 12;
const TUint32 KV4l2CidHflip = KV4l2CidBase + 20;
const TUint32 KV4l2CidCameraClassBase = 0x009a0900;		// V4L2_CID_CAMERA_CLASS_BASE
const TUint32 KV4l2CidExposureAuto = KV4l2CidCameraClassBase + 1;
const TUint32 KV4l2CidFocusAbsolute = KV4l2CidCameraClassBase + 10;
const TUint32 KV4l2CidFocusAuto = KV4l2CidCameraClassBase + 12;
const TUint32 KV4l2CidAutoExposureBias = KV4l2CidCameraClassBase + 19;
const TUint32 KV4l2CidAutoNPresetWhiteBalance = KV4l2CidCameraClassBase + 20;
const TUint32 KV4l2CidAutoFocusStart = KV4l2CidCameraClassBase + 28;
const TUint32 KV4l2CidAutoFocusStop = KV4l2CidCameraClassBase + 29;
const TUint32 KV4l2CidAutoFocusStatus = KV4l2CidCameraClassBase + 30;
const TUint32 KV4l2CidCameraSensorRotation = KV4l2CidCameraClassBase + 35;
const TUint32 KV4l2CidOv5640FocusZone = KV4l2CidBase + 0x1001;	// the ported ov5640.c's private "Focus Zone" (x << 16 | y)
const TInt KV4l2AutoFocusStatusBusy = 1, KV4l2AutoFocusStatusReached = 2, KV4l2AutoFocusStatusFailed = 4;
// enum v4l2_auto_n_preset_white_balance
enum TV4l2WbPreset { EV4l2WbManual, EV4l2WbAuto, EV4l2WbIncandescent, EV4l2WbFluorescent, EV4l2WbFluorescentH, EV4l2WbHorizon,
	EV4l2WbDaylight, EV4l2WbFlash, EV4l2WbCloudy, EV4l2WbShade };

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
	inline TInt Start(TInt aBuffers = 0) { return SendReceive(EPheCamStart, TIpcArgs(aBuffers)); }
	inline TInt Stop(TBool aKeepPower = EFalse) { return SendReceive(EPheCamStop, TIpcArgs(aKeepPower)); }	// aKeepPower: the sensor stays on until the next Start (a mode switch)
	inline void NextFrame(TDes8& aFrame, TRequestStatus& aStatus) { SendReceive(EPheCamNextFrame, TIpcArgs(&aFrame), aStatus); }
	inline void NextFrameCancel() { SendReceive(EPheCamNextFrameCancel); }
	inline TInt EnumSize(TInt aUnit, TInt aIndex, TSize& aSize)
		{
		TPckg<TSize> s(aSize);
		return SendReceive(EPheCamEnumSize, TIpcArgs(aUnit, aIndex, &s));
		}
	inline TInt SetCtrl(TInt aUnit, TUint32 aId, TInt aValue) { return SendReceive(EPheCamSetCtrl, TIpcArgs(aUnit, (TInt)aId, aValue)); }
	inline TInt GetCtrl(TInt aUnit, TUint32 aId, TInt& aValue)
		{
		TPckg<TInt> v(aValue);
		return SendReceive(EPheCamGetCtrl, TIpcArgs(aUnit, (TInt)aId, &v));
		}
	inline TInt MenuValue(TInt aUnit, TUint32 aId, TInt aIndex, TInt& aValue)
		{
		TPckg<TInt> v(aValue);
		return SendReceive(EPheCamMenuValue, TIpcArgs(aUnit, (TInt)aId, aIndex, &v));
		}
	inline TInt QueryCtrl(TInt aUnit, TUint32 aId, TPheCamCtrlInfo& aInfo)
		{
		TPckg<TPheCamCtrlInfo> i(aInfo);
		return SendReceive(EPheCamQueryCtrl, TIpcArgs(aUnit, (TInt)aId, &i));
		}
	};
#endif

#endif
