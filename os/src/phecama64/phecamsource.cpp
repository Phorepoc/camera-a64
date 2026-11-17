// Copyright The Phorepoc Authors
// SPDX-License-Identifier: Apache-2.0

// phecamsource.cpp -- see phecamsource.h.
#include <e32debug.h>
#include <e32math.h>
#include "phecamsource.h"

const TInt KVfW = 640, KVfH = 480;	// the viewfinder mode: a small 4:3 mode, converted in software for the screen
const TInt KStillBuffers = 2;		// vb2's minimum for the CSI (min_queued_buffers): 2 x 10 MB at 2592x1944 UYVY
const TInt KMaxBlankFrames = 20;	// Megapixels: up to 20 blank frames dropped after a mode switch

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
	iSensorSizes.Close();
	iUprightSizes.Close();
	}

void CPheCamSource::ConstructL(TInt aUnit)
	{
	iUnit = aUnit;
	User::LeaveIfError(iCam.Connect());
	iConnected = ETrue;
	iVfSize = TSize(KVfW, KVfH);
	// the rotation control is the sensor's (read-only, from the DT): known before any mode is set
	User::LeaveIfError(iCam.GetCtrl(aUnit, KV4l2CidCameraSensorRotation, iFmt.iRotation));
	// the sensor's modes, as media-ctl --enum lists them for the UYVY bus code
	const TBool quarter = iFmt.iRotation == 90 || iFmt.iRotation == 270;
	TSize s;
	for (TInt i = 0; iCam.EnumSize(aUnit, i, s) == KErrNone && i < 32; ++i)
		{
		iSensorSizes.AppendL(s);
		iUprightSizes.AppendL(quarter ? TSize(s.iHeight, s.iWidth) : s);
		RDebug::Printf("PH: camera-a64: plug-in unit %d mode %d: %dx%d", aUnit, i, s.iWidth, s.iHeight);
		}
	}

// configure the viewfinder mode, (re)allocate its buffer and bitmap, stream, ask for the first frame
TInt CPheCamSource::StartViewFinder()
	{
	// the OV5640 keeps the 15 fps a raw still set: ask for 30 again
	TInt r = iCam.Configure(iUnit, iVfSize.iWidth, iVfSize.iHeight, iFmt, EFalse, iUnit == 0 ? 30 : 0);
	RDebug::Printf("PH: camera-a64: plug-in configure unit %d -> %d (%dx%d, %d bytes/line)", iUnit, r, iFmt.iWidth, iFmt.iHeight,
		iFmt.iBytesPerLine);
	if (r != KErrNone)
		return r;
	if (!iFrame || iFrame->Des().MaxLength() < iFmt.iSizeImage)
		{
		delete iFrame;
		iFrame = HBufC8::New(iFmt.iSizeImage);
		if (!iFrame)
			return KErrNoMemory;
		}
	iBuf.Set(iFrame->Des());
	// the frame turned upright by the sensor's rotation control (as a V4L2 consumer -- libcamera, Megapixels -- does):
	// 90/270 swap the sides
	const TBool quarter = iFmt.iRotation == 90 || iFmt.iRotation == 270;
	if (!iBitmap)
		{
		RDebug::Printf("PH: camera-a64: plug-in sensor rotation %d", iFmt.iRotation);
		iBitmap = new CFbsBitmap;
		if (!iBitmap)
			return KErrNoMemory;
		r = iBitmap->Create(quarter ? TSize(iFmt.iHeight, iFmt.iWidth) : TSize(iFmt.iWidth, iFmt.iHeight), EColor16MU);
		if (r != KErrNone)
			return r;
		}
	if ((r = iCam.Start()) != KErrNone)
		return r;
	iStarted = ETrue;
	Next();
	return KErrNone;
	}

TInt CPheCamSource::Start()
	{
	return iStarted ? KErrNone : StartViewFinder();
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
		{
		Convert(iBuf.Ptr(), iFmt, *iBitmap, iMirror);
		if (++iFrames <= 3 || iFrames % 50 == 0)
			RDebug::Printf("PH: camera-a64: plug-in frame %d", iFrames);
		}
	else if (n < 0 && iLoggedErr++ < 3)
		RDebug::Printf("PH: camera-a64: plug-in NextFrame -> %d", n);
	if (n != KErrCancel && n != KErrServerTerminated)
		Next();
	}

// Megapixels' blank-frame test (io_pipeline.c on_frame): the top-left 10x10 pixels all zero bytes
static TBool Blank(const TUint8* aFrame, const TPheCamFormat& aFmt)
	{
	// Megapixels checks the top-left 10x10; the first frame after a mode switch on the PinePhone passed that
	// check yet converted to a solid-green (all-zero) still (2026-11-17), so the last line's middle must hold data too
	const TInt bpp = aFmt.iBytesPerLine >= aFmt.iWidth * 2 ? 2 : 1;	// UYVY or 8-bit Bayer
	const TInt w = Min(10, aFmt.iWidth) * bpp, h = Min(10, aFmt.iHeight);
	TBool top = EFalse, bottom = EFalse;
	for (TInt y = 0; y < h && !top; ++y)
		for (TInt x = 0; x < w && !top; ++x)
			top = aFrame[y * aFmt.iBytesPerLine + x] != 0;
	const TUint8* last = aFrame + (aFmt.iHeight - 1) * aFmt.iBytesPerLine + (aFmt.iWidth / 2) * bpp;
	for (TInt x = 0; x < w && !bottom; ++x)
		bottom = last[x] != 0;
	return !(top && bottom);
	}

// the mean Y of 64 samples along the frame's middle line (UYVY: Y at odd bytes; Bayer: the raw samples)
static TInt MeanLuma(const TUint8* aFrame, const TPheCamFormat& aFmt)
	{
	const TInt bpp = aFmt.iBytesPerLine >= aFmt.iWidth * 2 ? 2 : 1;
	const TUint8* line = aFrame + (aFmt.iHeight / 2) * aFmt.iBytesPerLine;
	TInt sum = 0;
	for (TInt i = 0; i < 64; ++i)
		sum += line[(i * (aFmt.iWidth / 64)) * bpp + bpp - 1];
	return sum / 64;
	}

CFbsBitmap* CPheCamSource::CaptureStillL(TInt aIndex, TBool aMirror)
	{
	if (aIndex < 0 || aIndex >= iSensorSizes.Count())
		User::Leave(KErrArgument);
	const TSize s = iSensorSizes[aIndex];
	const TUint32 t0 = User::NTickCount();
	CFbsBitmap* out = new (ELeave) CFbsBitmap;
	CleanupStack::PushL(out);
	User::LeaveIfError(out->Create(iUprightSizes[aIndex], EColor16MU));

	User::LeaveIfError(Start());
	Cancel();
	TInt r = KErrNone, blanks = 0;
	if (s == iVfSize)
		{
		// the viewfinder mode already: the next frame, no switch
		for (;; ++blanks)
			{
			iBuf.SetLength(0);
			TRequestStatus st;
			iCam.NextFrame(iBuf, st);
			User::WaitForRequest(st);
			if ((r = st.Int()) < 0 || !Blank(iBuf.Ptr(), iFmt) || blanks == KMaxBlankFrames)
				break;
			}
		if (r >= 0)
			Convert(iBuf.Ptr(), iFmt, *out, aMirror);
		Next();
		}
	else
		{
		// the Megapixels mode switch: stop, the capture mode, start, the first non-blank frame, stop, the viewfinder again
		iCam.Stop(ETrue);	// the sensor stays powered across the switch (Linux: 1 s autosuspend)
		iStarted = EFalse;
		TPheCamFormat f;
		RChunk chunk;
		// the rear OV5640 carries no UYVY above 1280x720 over the parallel bus (no frames, 2026-11-17): Megapixels'
		// PinePhone config takes its rear stills as BGGR8 (capture-fmt=BGGR8, capture-rate 10 -> the driver's 15 fps)
		const TBool raw = RawStill(s);
		r = iCam.Configure(iUnit, s.iWidth, s.iHeight, f, raw, raw ? 15 : 0);
		if (r == KErrNone)
			r = chunk.CreateLocal(f.iSizeImage, f.iSizeImage);	// the client heap need not hold 10 MB
		if (r == KErrNone)
			{
			r = iCam.Start(KStillBuffers);
			TPtr8 p(chunk.Base(), 0, f.iSizeImage);
			// the new mode starts at the sensor's reset exposure (a near-black 1280x720 frame, centre 03030303, came first,
			// 2026-11-17): drop frames until the auto exposure settles -- the mean luma of a row of samples moves by
			// <= 3 between two frames -- or KMaxBlankFrames
			TInt prev = -100;
			RTimer timer;
			const TInt tr = timer.CreateLocal();
			for (; r == KErrNone && tr == KErrNone; ++blanks)
				{
				p.SetLength(0);
				TRequestStatus st;
				iCam.NextFrame(p, st);
				// a mode the parallel bus cannot carry sends no frame at all (UYVY above 1280x720 on the PinePhone's
				// OV5640, 2026-11-17; Megapixels takes those in raw BGGR8): give up after 3 s, not hang
				TRequestStatus ts;
				timer.After(ts, 3000000);
				User::WaitForRequest(st, ts);
				if (st == KRequestPending)
					{
					iCam.NextFrameCancel();
					User::WaitForRequest(st);
					st = KErrTimedOut;
					}
				else
					{
					timer.Cancel();
					User::WaitForRequest(ts);
					}
				if (st.Int() < 0)
					r = st.Int();
				else if (blanks == KMaxBlankFrames)
					break;
				else if (!Blank(p.Ptr(), f))
					{
					const TInt luma = MeanLuma(p.Ptr(), f);
					const TBool settled = luma > 8 && Abs(luma - prev) <= 3;
					prev = luma;
					if (settled)
						break;
					}
				}
			if (tr != KErrNone)
				r = tr;
			timer.Close();
			RDebug::Printf("PH: camera-a64: plug-in still exposure: mean luma %d", prev);
			if (r == KErrNone)
				{
				const TUint8* c = p.Ptr() + (f.iHeight / 2) * f.iBytesPerLine + (f.iWidth / 2) * 2;
				RDebug::Printf("PH: camera-a64: plug-in still frame %d bytes (of %d), centre %02x%02x%02x%02x", p.Length(),
					f.iSizeImage, c[0], c[1], c[2], c[3]);
				}
			if (r == KErrNone)
				{
				if (raw)
					ConvertBggr(p.Ptr(), f, *out, aMirror);
				else
					Convert(p.Ptr(), f, *out, aMirror);
				}
			iCam.Stop(ETrue);
			chunk.Close();
			}
		const TInt v = StartViewFinder();
		if (v != KErrNone)
			RDebug::Printf("PH: camera-a64: plug-in viewfinder restart -> %d", v);
		}
	RDebug::Printf("PH: camera-a64: plug-in still %dx%d -> %d (%d blank frames dropped, %u ms)", s.iWidth, s.iHeight, r, blanks,
		(TUint)(User::NTickCount() - t0));
	User::LeaveIfError(r < 0 ? r : KErrNone);
	CleanupStack::Pop(out);
	return out;
	}

// packed UYVY (U0 Y0 V0 Y1), BT.601 studio range -> X8R8G8B8, rotated iRotation degrees counter-clockwise, then
// mirrored left-right if asked
void CPheCamSource::Convert(const TUint8* aUyvy, const TPheCamFormat& aFmt, CFbsBitmap& aOut, TBool aMirror)
	{
	const TInt w = aFmt.iWidth, h = aFmt.iHeight;
	const TInt outW = aOut.SizeInPixels().iWidth;
	aOut.LockHeap();
	TUint32* out = (TUint32*)aOut.DataAddress();
	const TInt wordsPerLine = aOut.DataStride() / 4;
	for (TInt y = 0; y < h; ++y)
		{
		const TUint8* in = aUyvy + y * aFmt.iBytesPerLine;
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
				const TInt sx = x + k;
				TInt dx, dy;		// destination of source pixel (sx, y)
				switch (aFmt.iRotation)
					{
					case 90: dx = y; dy = w - 1 - sx; break;		// 90 CCW
					case 180: dx = w - 1 - sx; dy = h - 1 - y; break;
					case 270: dx = h - 1 - y; dy = sx; break;		// 270 CCW = 90 CW
					default: dx = sx; dy = y; break;
					}
				if (aMirror)
					dx = outW - 1 - dx;
				out[dy * wordsPerLine + dx] = 0xFF000000u | (r << 16) | (g << 8) | b;
				}
			}
		}
	aOut.UnlockHeap();
	}

// a still taken as 8-bit Bayer: the rear OV5640 (unit 0) above 1280x720, where UYVY gives no frames
TBool CPheCamSource::RawStill(const TSize& aSensorSize) const
	{
	return iUnit == 0 && aSensorSize.iWidth * aSensorSize.iHeight > 1280 * 720;
	}

// 8-bit BGGR Bayer -> X8R8G8B8, rotated and mirrored as Convert(). Our own code (Apache-2.0, like this file); the
// approach is the one Megapixels' preview takes (GPL-3.0, read for the method only, no code taken): each pixel
// takes the 2x2 quad it opens (one red, two greens averaged, one blue) with the colour order by the pixel's position,
// the black level (3, as Megapixels' PinePhone config) is subtracted, a grey-world white balance is applied, and
// the fast sRGB estimate (1.138 / sqrt(c) - 0.138) * c from https://mimosa-pudica.net/fast-gamma/ shapes the curve.
void CPheCamSource::ConvertBggr(const TUint8* aRaw, const TPheCamFormat& aFmt, CFbsBitmap& aOut, TBool aMirror)
	{
	const TInt w = aFmt.iWidth, h = aFmt.iHeight, bpl = aFmt.iBytesPerLine;
	const TInt KBlack = 3;		// Megapixels' PinePhone rear blacklevel
	// grey world: the channel means over every 8th quad
	TUint sum[3] = { 0, 0, 0 };
	for (TInt y = 0; y + 1 < h; y += 16)
		for (TInt x = 0; x + 1 < w; x += 16)
			{
			const TUint8* q = aRaw + y * bpl + x;
			sum[0] += q[bpl + 1];
			sum[1] += (q[1] + q[bpl]) / 2;
			sum[2] += q[0];
			}
	TInt gain[3];		// x256
	for (TInt c = 0; c < 3; ++c)
		{
		gain[c] = sum[c] ? (TInt)(((TInt64)sum[1] << 8) / sum[c]) : 256;
		if (gain[c] > 4 * 256)
			gain[c] = 4 * 256;
		}
	// the transfer curve, 0..255 linear -> 0..255 sRGB (fast estimate), integer table
	TUint8 lut[256];
	for (TInt v = 0; v < 256; ++v)
		{
		if (v == 0)
			{
			lut[0] = 0;
			continue;
			}
		// (1.138 / sqrt(c) - 0.138) * c = 1.138 sqrt(c) - 0.138 c, c = v / 255
		TReal c = v / 255.0, s;
		Math::Sqrt(s, c);
		TInt o = (TInt)((1.138 * s - 0.138 * c) * 255.0 + 0.5);
		lut[v] = (TUint8)(o > 255 ? 255 : o);
		}
	const TInt outW = aOut.SizeInPixels().iWidth;
	aOut.LockHeap();
	TUint32* out = (TUint32*)aOut.DataAddress();
	const TInt wordsPerLine = aOut.DataStride() / 4;
	for (TInt y = 0; y < h; ++y)
		{
		const TInt y0 = y + 1 < h ? y : y - 1;
		for (TInt x = 0; x < w; ++x)
			{
			const TInt x0 = x + 1 < w ? x : x - 1;
			const TUint8* q = aRaw + y0 * bpl + x0;
			TInt rgb[3] = { 0, 0, 0 };	// r, g, b
			for (TInt k = 0; k < 4; ++k)
				{
				const TInt dx = k & 1, dy = k >> 1;
				const TInt v = q[dy * bpl + dx];
				const TInt px = (x0 + dx) & 1, py = (y0 + dy) & 1;
				if (!px && !py)
					rgb[2] = v;				// blue at (even, even)
				else if (px && py)
					rgb[0] = v;				// red at (odd, odd)
				else
					rgb[1] += v;			// green twice
				}
			rgb[1] /= 2;
			TUint32 pix = 0xFF000000u;
			for (TInt c = 0; c < 3; ++c)
				{
				TInt v = ((rgb[c] - KBlack) * gain[c]) >> 8;
				v = v < 0 ? 0 : v > 255 ? 255 : v;
				pix |= (TUint32)lut[v] << (16 - 8 * c);
				}
			TInt dx, dy;
			switch (aFmt.iRotation)
				{
				case 90: dx = y; dy = w - 1 - x; break;
				case 180: dx = w - 1 - x; dy = h - 1 - y; break;
				case 270: dx = h - 1 - y; dy = x; break;
				default: dx = x; dy = y; break;
				}
			if (aMirror)
				dx = outW - 1 - dx;
			out[dy * wordsPerLine + dx] = pix;
			}
		}
	aOut.UnlockHeap();
	RDebug::Printf("PH: camera-a64: plug-in debayer %dx%d, grey-world gains r %d g %d b %d (/256)", w, h, gain[0], gain[1], gain[2]);
	}
