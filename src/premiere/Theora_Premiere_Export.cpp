///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2013, Brendan Bolles
// 
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// *	   Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// *	   Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------
//
// Theora plug-in for Premiere
//
// by Brendan Bolles <brendan@fnordware.com>
//
// ------------------------------------------------------------------------



#include "Theora_Premiere_Export.h"

#include "Theora_Premiere_Export_Params.h"


#ifdef PRMAC_ENV
	#include <mach/mach.h>
#else
	#include <assert.h>
	#include <math.h>
	#include <time.h>
	#include <sys/timeb.h>

	typedef unsigned char uint8;
	typedef unsigned short uint16;
#endif


extern "C" {

#include "theora/theoraenc.h"
#include "vorbis/codec.h"
#include "vorbis/vorbisenc.h"

}



static const csSDK_int32 Theora_ID = 'Theo';
static const csSDK_int32 Theora_Export_Class = 'Theo';

extern int g_num_cpus;



static void
utf16ncpy(prUTF16Char *dest, const char *src, int max_len)
{
	prUTF16Char *d = dest;
	const char *c = src;
	
	do{
		*d++ = *c;
	}while(*c++ != '\0' && --max_len);
}

static void
ncpyUTF16(char *dest, const prUTF16Char *src, int max_len)
{
	char *d = dest;
	const prUTF16Char *c = src;
	
	do{
		*d++ = *c;
	}while(*c++ != '\0' && --max_len);
}

static prMALError
exSDKStartup(
	exportStdParms		*stdParmsP, 
	exExporterInfoRec	*infoRecP)
{
	PrSDKAppInfoSuite *appInfoSuite = NULL;
	stdParmsP->getSPBasicSuite()->AcquireSuite(kPrSDKAppInfoSuite, kPrSDKAppInfoSuiteVersion, (const void**)&appInfoSuite);
	
	if(appInfoSuite)
	{
		int fourCC = 0;
	
		appInfoSuite->GetAppInfo(PrSDKAppInfoSuite::kAppInfo_AppFourCC, (void *)&fourCC);
	
		stdParmsP->getSPBasicSuite()->ReleaseSuite(kPrSDKAppInfoSuite, kPrSDKAppInfoSuiteVersion);
		
		// not a good idea to try to run a MediaCore exporter in AE
		if(fourCC == kAppAfterEffects)
			return exportReturn_IterateExporterDone;
	}
	

	infoRecP->fileType			= Theora_ID;
	
	utf16ncpy(infoRecP->fileTypeName, "Theora", 255);
	utf16ncpy(infoRecP->fileTypeDefaultExtension, "ogv", 255);
	
	infoRecP->classID = Theora_Export_Class;
	
	infoRecP->exportReqIndex	= 0;
	infoRecP->wantsNoProgressBar = kPrFalse;
	infoRecP->hideInUI			= kPrFalse;
	infoRecP->doesNotSupportAudioOnly = kPrFalse;
	infoRecP->canExportVideo	= kPrTrue;
	infoRecP->canExportAudio	= kPrTrue;
	infoRecP->singleFrameOnly	= kPrFalse;
	
	infoRecP->interfaceVersion	= EXPORTMOD_VERSION;
	
	infoRecP->isCacheable		= kPrFalse;
	

	return malNoError;
}


static prMALError
exSDKBeginInstance(
	exportStdParms			*stdParmsP, 
	exExporterInstanceRec	*instanceRecP)
{
	prMALError				result				= malNoError;
	SPErr					spError				= kSPNoError;
	ExportSettings			*mySettings;
	PrSDKMemoryManagerSuite	*memorySuite;
	csSDK_int32				exportSettingsSize	= sizeof(ExportSettings);
	SPBasicSuite			*spBasic			= stdParmsP->getSPBasicSuite();
	
	if(spBasic != NULL)
	{
		spError = spBasic->AcquireSuite(
			kPrSDKMemoryManagerSuite,
			kPrSDKMemoryManagerSuiteVersion,
			const_cast<const void**>(reinterpret_cast<void**>(&memorySuite)));
			
		mySettings = reinterpret_cast<ExportSettings *>(memorySuite->NewPtrClear(exportSettingsSize));

		if(mySettings)
		{
			mySettings->spBasic		= spBasic;
			mySettings->memorySuite	= memorySuite;
			
			spError = spBasic->AcquireSuite(
				kPrSDKExportParamSuite,
				kPrSDKExportParamSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->exportParamSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKExportFileSuite,
				kPrSDKExportFileSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->exportFileSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKExportInfoSuite,
				kPrSDKExportInfoSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->exportInfoSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKExportProgressSuite,
				kPrSDKExportProgressSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->exportProgressSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKPPixCreatorSuite,
				kPrSDKPPixCreatorSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->ppixCreatorSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKPPixSuite,
				kPrSDKPPixSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->ppixSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKPPix2Suite,
				kPrSDKPPix2SuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->ppix2Suite))));
			spError = spBasic->AcquireSuite(
				kPrSDKSequenceRenderSuite,
				kPrSDKSequenceRenderSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->sequenceRenderSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKSequenceAudioSuite,
				kPrSDKSequenceAudioSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->sequenceAudioSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKTimeSuite,
				kPrSDKTimeSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->timeSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKWindowSuite,
				kPrSDKWindowSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->windowSuite))));
		}


		instanceRecP->privateData = reinterpret_cast<void*>(mySettings);
	}
	else
	{
		result = exportReturn_ErrMemory;
	}
	
	return result;
}


static prMALError
exSDKEndInstance(
	exportStdParms			*stdParmsP, 
	exExporterInstanceRec	*instanceRecP)
{
	prMALError				result		= malNoError;
	ExportSettings			*lRec		= reinterpret_cast<ExportSettings *>(instanceRecP->privateData);
	SPBasicSuite			*spBasic	= stdParmsP->getSPBasicSuite();
	PrSDKMemoryManagerSuite	*memorySuite;
	if(spBasic != NULL && lRec != NULL)
	{
		if (lRec->exportParamSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKExportParamSuite, kPrSDKExportParamSuiteVersion);
		}
		if (lRec->exportFileSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKExportFileSuite, kPrSDKExportFileSuiteVersion);
		}
		if (lRec->exportInfoSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKExportInfoSuite, kPrSDKExportInfoSuiteVersion);
		}
		if (lRec->exportProgressSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKExportProgressSuite, kPrSDKExportProgressSuiteVersion);
		}
		if (lRec->ppixCreatorSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKPPixCreatorSuite, kPrSDKPPixCreatorSuiteVersion);
		}
		if (lRec->ppixSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKPPixSuite, kPrSDKPPixSuiteVersion);
		}
		if (lRec->ppix2Suite)
		{
			result = spBasic->ReleaseSuite(kPrSDKPPix2Suite, kPrSDKPPix2SuiteVersion);
		}
		if (lRec->sequenceRenderSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKSequenceRenderSuite, kPrSDKSequenceRenderSuiteVersion);
		}
		if (lRec->sequenceAudioSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKSequenceAudioSuite, kPrSDKSequenceAudioSuiteVersion);
		}
		if (lRec->timeSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKTimeSuite, kPrSDKTimeSuiteVersion);
		}
		if (lRec->windowSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKWindowSuite, kPrSDKWindowSuiteVersion);
		}
		if (lRec->memorySuite)
		{
			memorySuite = lRec->memorySuite;
			memorySuite->PrDisposePtr(reinterpret_cast<PrMemoryPtr>(lRec));
			result = spBasic->ReleaseSuite(kPrSDKMemoryManagerSuite, kPrSDKMemoryManagerSuiteVersion);
		}
	}

	return result;
}



static prMALError
exSDKFileExtension(
	exportStdParms					*stdParmsP, 
	exQueryExportFileExtensionRec	*exportFileExtensionRecP)
{
	utf16ncpy(exportFileExtensionRecP->outFileExtension, "ogv", 255);
		
	return malNoError;
}


static void get_framerate(PrTime ticksPerSecond, PrTime ticks_per_frame, exRatioValue *fps)
{
	PrTime frameRates[] = {	10, 15, 23,
							24, 25, 29,
							30, 50, 59,
							60};
													
	PrTime frameRateNumDens[][2] = {{10, 1}, {15, 1}, {24000, 1001},
									{24, 1}, {25, 1}, {30000, 1001},
									{30, 1}, {50, 1}, {60000, 1001},
									{60, 1}};
	
	int frameRateIndex = -1;
	
	for(csSDK_int32 i=0; i < sizeof(frameRates) / sizeof (PrTime); i++)
	{
		frameRates[i] = ticksPerSecond / frameRateNumDens[i][0] * frameRateNumDens[i][1];
		
		if(ticks_per_frame == frameRates[i])
			frameRateIndex = i;
	}
	
	if(frameRateIndex >= 0)
	{
		fps->numerator = frameRateNumDens[frameRateIndex][0];
		fps->denominator = frameRateNumDens[frameRateIndex][1];
	}
	else
	{
		fps->numerator = 1000 * ticksPerSecond / ticks_per_frame;
		fps->denominator = 1000;
	}
}


// converting from the Adobe 16-bit, i.e. max_val is 0x8000
static inline uint8
Convert16to8(const uint16 &v)
{
	return ( (((long)(v) * 255) + 16384) / 32768);
}


// I know, this is UGLY!  Very, very ugly!
// It's basically a straight copy from the encoder_example.c that comes with
// libtheora, but that uses all kinds of globals and statics that had to go.  At the same time I didn't want to
// change it too much until I knew my copy was working like the original.  Once
// we know it's good, then we'll make it pretty.

static
int fetch_and_process_audio(const PrSDKSequenceAudioSuite *audioSuite, const csSDK_uint32 audioRenderID, csSDK_int32 maxBlip,
 ogg_page *audiopage, ogg_stream_state *vo, vorbis_dsp_state *vd, vorbis_block *vb,
 int &audioflag, int &audio_hz, long &begin_sec, long &begin_usec, long &end_sec, long &end_usec, ogg_int64_t &samples_sofar)
 {
  ogg_packet op;

  prMALError result = malNoError;

  ogg_int64_t beginsample = audio_hz*(begin_sec+(begin_usec*.000001));
  ogg_int64_t endsample = audio_hz*(end_sec+(end_usec*.000001));
     
  
  
  while(result == malNoError && !audioflag){
    /* process any audio already buffered */
    if(ogg_stream_pageout(vo,audiopage)>0) return 1;
    if(ogg_stream_eos(vo))return 0;

	  if(samples_sofar>=(endsample - beginsample) && (endsample - beginsample)>0)
	  {
		vorbis_analysis_wrote(vd,0);
	  }
	  else
	  {
		  /* read and process more audio */
		  float **vorbis_buffer=vorbis_analysis_buffer(vd,maxBlip);
			  
		  result = audioSuite->GetAudio(audioRenderID, maxBlip, vorbis_buffer, false);
			  
		  vorbis_analysis_wrote(vd,maxBlip);
	  }

	  while(vorbis_analysis_blockout(vd,vb)==1){

		/* analysis, assume we want to use bitrate management */
		vorbis_analysis(vb,NULL);
		vorbis_bitrate_addblock(vb);

		/* weld packets into the bitstream */
		while(vorbis_bitrate_flushpacket(vd,&op))
		  ogg_stream_packetin(vo,&op);

	  }
		  
	  samples_sofar += maxBlip;
  }

  return audioflag;
}

static
size_t fread_buf(void *out_buf, size_t elemsz, size_t elem_num, PrMemoryPtr &buffer, size_t &buffer_pos)
{
	memcpy(out_buf, &buffer[buffer_pos], elemsz * elem_num);
	
	buffer_pos += (elemsz *  elem_num);
	
	return elem_num;
}

static int fseek_buf(size_t &buffer_pos, long pos, int whence)
{
	if(whence == SEEK_SET)
        buffer_pos = pos;
    else if(whence == SEEK_CUR)
        buffer_pos += pos;
    else
        assert(false);
        
	return 0;
}

static size_t fwrite_buf(const void *in_buf, size_t elemsz, size_t elem_num, PrMemoryPtr &buffer, size_t &buffer_pos, size_t &buffer_size, PrSDKMemoryManagerSuite *memorySuite)
{
    size_t write_size = (elemsz * elem_num);
	size_t end_of_write = buffer_pos + write_size;
    
    if(buffer_size < end_of_write)
    {
        if(buffer_size == 0)
            buffer = memorySuite->NewPtr(end_of_write);
        else
            memorySuite->SetPtrSize(&buffer, end_of_write);
        
        buffer_size = end_of_write;
    }
	
	memcpy(&buffer[buffer_pos], in_buf, write_size);
	
	buffer_pos += write_size;
	
	return elem_num;
}

static
int fetch_and_process_video_packet(PrSDKSequenceRenderSuite	*renderSuite, csSDK_uint32 videoRenderID, SequenceRender_ParamsRec &renderParms, PrTime ticksPerSecond,
 PrSDKPPixSuite *pixSuite, PrSDKPPix2Suite *pix2Suite, PrMemoryPtr &vbr_buffer, size_t &vbr_buffer_pos, size_t &vbr_buffer_size, PrSDKMemoryManagerSuite *memorySuite,int passno,
 th_enc_ctx *td,ogg_packet *op, int &video_fps_n, int &video_fps_d, long &begin_sec, long &begin_usec, long &end_sec, long &end_usec,
 size_t &y4m_dst_buf_sz, size_t &y4m_aux_buf_sz, int &pic_w, int &pic_h, int &dst_c_dec_h, int &dst_c_dec_v,
 int &frame_state, ogg_int64_t &frames, unsigned char ** &yuvframe, th_ycbcr_buffer &ycbcr)
 {
  int                        ret;
  int                        pic_sz;
  int                        c_w;
  int                        c_h;
  int                        c_sz;
  ogg_int64_t                beginframe;
  ogg_int64_t                endframe;
  beginframe=video_fps_n*(begin_sec+begin_usec*.000001)/video_fps_d;
  endframe=video_fps_n*(end_sec+end_usec*.000001)/video_fps_d;
  if(frame_state==-1){
    /* initialize the double frame buffer */
    yuvframe[0]=(unsigned char *)malloc(y4m_dst_buf_sz);
    yuvframe[1]=(unsigned char *)malloc(y4m_dst_buf_sz);
    yuvframe[2]=(unsigned char *)malloc(y4m_aux_buf_sz);
    frame_state=0;
  }
  pic_sz=pic_w*pic_h;
  c_w=(pic_w+dst_c_dec_h-1)/dst_c_dec_h;
  c_h=(pic_h+dst_c_dec_v-1)/dst_c_dec_v;
  c_sz=c_w*c_h;
  /* read and process more video */
  /* video strategy reads one frame ahead so we know when we're
     at end of stream and can mark last video frame as such
     (vorbis audio has to flush one frame past last video frame
     due to overlap and thus doesn't need this extra work */

  /* have two frame buffers full (if possible) before
     proceeding.  after first pass and until eos, one will
     always be full when we get here */
  
  for(;frame_state<2 && (frames<endframe || endframe<0);){
  
    SequenceRender_GetFrameReturnRec renderResult;
  
    PrTime render_time = frames * video_fps_d * ticksPerSecond / video_fps_n;
    prMALError result = renderSuite->RenderVideoFrame(videoRenderID, render_time, &renderParms, kRenderCacheType_AllFrames, &renderResult);
    if(result != malNoError)break;
	
	PrPixelFormat pixFormat;
	prRect bounds;
	csSDK_uint32 parN, parD;
	
	pixSuite->GetPixelFormat(renderResult.outFrame, &pixFormat);
	pixSuite->GetBounds(renderResult.outFrame, &bounds);
	pixSuite->GetPixelAspectRatio(renderResult.outFrame, &parN, &parD);
	
	const int width = bounds.right - bounds.left;
	const int height = bounds.bottom - bounds.top;
	
	if(pixFormat == PrPixelFormat_YUV_420_MPEG2_FRAME_PICTURE_PLANAR_8u_709)
	{
        assert(dst_c_dec_h == 2 && dst_c_dec_v == 2);
    
		char *Y_PixelAddress, *U_PixelAddress, *V_PixelAddress;
		csSDK_uint32 Y_RowBytes, U_RowBytes, V_RowBytes;
		
		pix2Suite->GetYUV420PlanarBuffers(renderResult.outFrame, PrPPixBufferAccess_ReadOnly,
											&Y_PixelAddress, &Y_RowBytes,
											&U_PixelAddress, &U_RowBytes,
											&V_PixelAddress, &V_RowBytes);
											
		const int w = width;
		const int h = height;
		unsigned char *y = (unsigned char *)yuvframe[frame_state];
		unsigned char *u = y + (w * h);
		unsigned char *v = u + (((w + 1) / 2) * ((h + 1) / 2));
		
		for(int i = 0; i < h; i++)
		{
			const unsigned char *prY = (unsigned char *)Y_PixelAddress + (Y_RowBytes * i);
			
			memcpy(y, prY, w * sizeof(unsigned char));
			
			y += w;
		}
		
		for(int i = 0; i < ((h + 1) / 2); i++)
		{
			const unsigned char *prU = (unsigned char *)U_PixelAddress + (U_RowBytes * i);
			const unsigned char *prV = (unsigned char *)V_PixelAddress + (V_RowBytes * i);
			
			memcpy(u, prU, ((w + 1) / 2) * sizeof(unsigned char));
			memcpy(v, prV, ((w + 1) / 2) * sizeof(unsigned char));
			
			u += ((w + 1) / 2);
			v += ((w + 1) / 2);
		}
	}
    else if(pixFormat == PrPixelFormat_YUYV_422_8u_709)
    {
        assert(dst_c_dec_h == 2 && dst_c_dec_v == 1);
        
		char *frameBufferP = NULL;
		csSDK_int32 rowbytes = 0;
		
		pixSuite->GetPixels(renderResult.outFrame, PrPPixBufferAccess_ReadOnly, &frameBufferP);
		pixSuite->GetRowBytes(renderResult.outFrame, &rowbytes);
        
        assert(false); // not ready for this yet
    }
	else if(pixFormat == PrPixelFormat_BGRA_4444_8u || pixFormat == PrPixelFormat_ARGB_4444_8u)
	{
		char *frameBufferP = NULL;
		csSDK_int32 rowbytes = 0;
		
		pixSuite->GetPixels(renderResult.outFrame, PrPPixBufferAccess_ReadOnly, &frameBufferP);
		pixSuite->GetRowBytes(renderResult.outFrame, &rowbytes);
		
		
        int ch_rnd_h = dst_c_dec_h - 1;
        int ch_rnd_v = dst_c_dec_v - 1;
        
		unsigned char *yP = (unsigned char *)yuvframe[frame_state];
		unsigned char *uP = yP + (width * height);
		unsigned char *vP = uP + (((width + ch_rnd_h) / dst_c_dec_h) * ((height + ch_rnd_v) / dst_c_dec_v));
		
		size_t y_stride = width * sizeof(unsigned char);
		size_t uv_stride = ((width + ch_rnd_h) / dst_c_dec_h) * sizeof(unsigned char);
		
		for(int y = 0; y < height; y++)
		{
			// using the conversion found here: http://www.fourcc.org/fccyvrgb.php
			
			unsigned char *imgY = yP + (y_stride * y);
			unsigned char *imgU = uP + (uv_stride * (y / dst_c_dec_v));
			unsigned char *imgV = vP + (uv_stride * (y / dst_c_dec_v));
			
			// the rows in this kind of Premiere buffer are flipped, FYI (or is it flopped?)
			unsigned char *prBGRA = (unsigned char *)frameBufferP + (rowbytes * (height - 1 - y));
			
			unsigned char *prB = prBGRA + 0;
			unsigned char *prG = prBGRA + 1;
			unsigned char *prR = prBGRA + 2;
			unsigned char *prA = prBGRA + 3;
			
			if(pixFormat == PrPixelFormat_ARGB_4444_8u)
			{
				// Media Encoder CS5 insists on handing us this format in some cases,
				// even though we didn't list it as an option
				prA = prBGRA + 0;
				prR = prBGRA + 1;
				prG = prBGRA + 2;
				prB = prBGRA + 3;
			}
			
			for(int x=0; x < width; x++)
			{
				// like the clever integer (fixed point) math?
				*imgY++ = ((257 * (int)*prR) + (504 * (int)*prG) + ( 98 * (int)*prB) + 16500) / 1000;
				
				if( (y % dst_c_dec_v == 0) && (x % dst_c_dec_h == 0) )
				{
					*imgV++ = ((439 * (int)*prR) - (368 * (int)*prG) - ( 71 * (int)*prB) + 128500) / 1000;
					*imgU++ = (-(148 * (int)*prR) - (291 * (int)*prG) + (439 * (int)*prB) + 128500) / 1000;
				}
				
				prR += 4;
				prG += 4;
				prB += 4;
				prA += 4;
			}
		}
	}
	
	pixSuite->Dispose(renderResult.outFrame);
	
	//assert(y4m_aux_buf_read_sz == 0);

    /*Now convert the just read frame.*/
    //(*y4m_convert)(yuvframe[frame_state],yuvframe[2]);
    frames++;
    if(frames>=beginframe)
    frame_state++;
  }
  /* check to see if there are dupes to flush */
  if(th_encode_packetout(td,frame_state<1,op)>0)return 1;
  assert(frame_state >= 1);
  /* Theora is a one-frame-in,one-frame-out system; submit a frame
     for compression and pull out the packet */
  /* in two-pass mode's second pass, we need to submit first-pass data */
  if(passno==2){
    for(;;){
      unsigned char buffer[80];
      /*Ask the encoder how many bytes it would like.*/
      int bytes=th_encode_ctl(td,TH_ENCCTL_2PASS_IN,NULL,0);
	  assert(bytes >= 0 && bytes <= 80);
      /*If it's got enough, stop.*/
      if(bytes==0)break;
      /*Read in some more bytes, if necessary.*/
      if(bytes>0&&fread_buf(buffer,1,bytes,vbr_buffer,vbr_buffer_pos)<bytes){
        assert(false);
      }
      /*And pass them off.*/
      ret=th_encode_ctl(td,TH_ENCCTL_2PASS_IN,buffer,bytes);
      if(ret<0){
        assert(false);
      }
        
      if(ret < bytes)
          fseek_buf(vbr_buffer_pos, -(bytes - ret), SEEK_CUR);
    }
  }
  /*We submit the buffer using the size of the picture region.
    libtheora will pad the picture region out to the full frame size for us,
     whether we pass in a full frame or not.*/
  ycbcr[0].width=pic_w;
  ycbcr[0].height=pic_h;
  ycbcr[0].stride=pic_w;
  ycbcr[0].data=yuvframe[0];
  ycbcr[1].width=c_w;
  ycbcr[1].height=c_h;
  ycbcr[1].stride=c_w;
  ycbcr[1].data=yuvframe[0]+pic_sz;
  ycbcr[2].width=c_w;
  ycbcr[2].height=c_h;
  ycbcr[2].stride=c_w;
  ycbcr[2].data=yuvframe[0]+pic_sz+c_sz;
  th_encode_ycbcr_in(td,ycbcr);
  {
    unsigned char *temp=yuvframe[0];
    yuvframe[0]=yuvframe[1];
    yuvframe[1]=temp;
    frame_state--;
  }
  /* in two-pass mode's first pass we need to extract and save the pass data */
  if(passno==1){
    unsigned char *buffer;
    int bytes = th_encode_ctl(td, TH_ENCCTL_2PASS_OUT, &buffer, sizeof(buffer));
    if(bytes<0){
      assert(false);
    }
    if(fwrite_buf(buffer,1,bytes,vbr_buffer, vbr_buffer_pos, vbr_buffer_size, memorySuite)<bytes){
      assert(false);
    }
    //fflush(twopass_file);
  }
  /* if there was only one frame, it's the last in the stream */
  ret = th_encode_packetout(td,frame_state<1,op);
  if(passno==1 && frame_state<1){
    /* need to read the final (summary) packet */
    unsigned char *buffer;
    int bytes = th_encode_ctl(td, TH_ENCCTL_2PASS_OUT, &buffer, sizeof(buffer));
    if(bytes<0){
      assert(false);
    }
    if(fseek_buf(vbr_buffer_pos,0,SEEK_SET)<0){
      assert(false);
    }
    if(fwrite_buf(buffer,1,bytes,vbr_buffer, vbr_buffer_pos, vbr_buffer_size, memorySuite)<bytes){
      assert(false);
    }
    //fflush(twopass_file);
  }
  return ret;
}

static
int fetch_and_process_video(PrSDKSequenceRenderSuite	*renderSuite, csSDK_uint32 videoRenderID, SequenceRender_ParamsRec &renderParms, PrTime ticksPerSecond,
 PrSDKPPixSuite *pixSuite, PrSDKPPix2Suite *pix2Suite, ogg_page *videopage,
 ogg_stream_state *to,th_enc_ctx *td,PrMemoryPtr &vbr_buffer, size_t &vbr_buffer_pos, size_t &vbr_buffer_size, PrSDKMemoryManagerSuite *memorySuite,int passno,
 int videoflag, int &video_fps_n, int &video_fps_d, long &begin_sec, long &begin_usec, long &end_sec, long &end_usec,
 size_t &y4m_dst_buf_sz, size_t &y4m_aux_buf_sz, int &pic_w, int &pic_h, int &dst_c_dec_h, int &dst_c_dec_v,
 int &frame_state, ogg_int64_t &frames, unsigned char ** &yuvframe, th_ycbcr_buffer &ycbcr)
 {
  ogg_packet op;
  int ret;
  // is there a video page flushed?  If not, work until there is. 
  while(!videoflag){
    if(ogg_stream_pageout(to,videopage)>0) return 1;
    if(ogg_stream_eos(to)) return 0;
	
	
    ret=fetch_and_process_video_packet(renderSuite, videoRenderID, renderParms, ticksPerSecond,
		 pixSuite, pix2Suite, vbr_buffer, vbr_buffer_pos, vbr_buffer_size, memorySuite,passno,
		 td,&op, video_fps_n, video_fps_d, begin_sec, begin_usec, end_sec, end_usec,
		 y4m_dst_buf_sz, y4m_aux_buf_sz, pic_w, pic_h, dst_c_dec_h, dst_c_dec_v,
		 frame_state, frames, yuvframe, ycbcr);
	
	
	
    if(ret<=0)return 0;
    ogg_stream_packetin(to,&op);
  }
  return videoflag;
}

static
int ilog(unsigned _v){
  int ret;
  for(ret=0;_v;ret++)_v>>=1;
  return ret;
}

static
int parse_time(long *_sec,long *_usec,const char *_optarg){
  double      secf;
  long        secl;
  const char *pos;
  char       *end;
  int         err;
  err=0;
  secl=0;
  pos=strchr(_optarg,':');
  if(pos!=NULL){
    char *pos2;
    secl=strtol(_optarg,&end,10)*60;
    err|=pos!=end;
    pos2=(char *)strchr(++pos,':');
    if(pos2!=NULL){
      secl=(secl+strtol(pos,&end,10))*60;
      err|=pos2!=end;
      pos=pos2+1;
    }
  }
  else pos=_optarg;
  secf=strtod(pos,&end);
  if(err||*end!='\0')return -1;
  *_sec=secl+(long)floor(secf);
  *_usec=(long)((secf-floor(secf))*1E6+0.5);
  return 0;
}

static size_t fwrite_pr(const void *in_buf, size_t elemsz, size_t elem_num, csSDK_uint32 fileObject, PrSDKExportFileSuite *fileSuite)
{
	prSuiteError err = fileSuite->Write(fileObject, (void *)in_buf, (elemsz * elem_num));
	
	return (err == malNoError ? elem_num : 0);
}

#ifdef PRWIN_ENV
static inline double
rint(double in)
{
	return (in >= 0 ? floor(in + 0.5) : ceil(in - 0.5));
}
#endif

static
prMALError compress_main(PrSDKSequenceRenderSuite	*renderSuite, csSDK_uint32 videoRenderID, SequenceRender_ParamsRec &renderParms, PrTime ticksPerSecond,
 PrSDKPPixSuite *pixSuite, PrSDKPPix2Suite *pix2Suite, PrMemoryPtr &vbr_buffer, size_t &vbr_buffer_pos, size_t &vbr_buffer_size, PrSDKMemoryManagerSuite *memorySuite,
 const PrSDKSequenceAudioSuite *audioSuite, const csSDK_uint32 audioRenderID, csSDK_int32 maxBlip,
 PrSDKExportProgressSuite *exportProgressSuite, csSDK_uint32 inExportID,
 csSDK_uint32 fileObject, PrSDKExportFileSuite *fileSuite, bool do_video, bool do_audio,
 int audio_ch, int audio_hz, int pic_w, int pic_h, int video_fps_n, int video_fps_d, int video_par_n, int video_par_d,
 float audio_q, int audio_r, int video_q, int video_r, int twopass, Theora_Video_Encoding encoding, Theora_Chroma_Sampling chroma_samp,
 long begin_sec, long begin_usec, long end_sec, long end_usec){

	prMALError result = malNoError;
  // former globals
//int audio_ch=0;
//int audio_hz=0;

//float audio_q=.1f;
//int audio_r=-1;
int vp3_compatible=0;

int quiet=1;

int frame_w=0;
int frame_h=0;
//int pic_w=0;
//int pic_h=0;
int pic_x=0;
int pic_y=0;
//int video_fps_n=-1;
//int video_fps_d=-1;
//int video_par_n=-1;
//int video_par_d=-1;
//char interlace;
//int src_c_dec_h=2;
//int src_c_dec_v=2;
int dst_c_dec_h=2;
int dst_c_dec_v=2;
//char chroma_type[16];

if(chroma_samp == THEORA_CHROMA_422)
    dst_c_dec_v = 1;
else if(chroma_samp == THEORA_CHROMA_444)
    dst_c_dec_v = dst_c_dec_h = 1;

/*The size of each converted frame buffer.*/
size_t y4m_dst_buf_sz = pic_w*pic_h+2*((pic_w+dst_c_dec_h-1)/dst_c_dec_h)*((pic_h+dst_c_dec_v-1)/dst_c_dec_v);
/*The amount to read directly into the converted frame buffer.*/
//size_t y4m_dst_buf_read_sz;
/*The size of the auxilliary buffer.*/
size_t y4m_aux_buf_sz = 0;
/*The amount to read into the auxilliary buffer.*/
//size_t y4m_aux_buf_read_sz;

/*The function used to perform chroma conversion.*/
//typedef void (*y4m_convert_func)(unsigned char *_dst,unsigned char *_aux);

//y4m_convert_func y4m_convert=NULL;

//int video_r=-1;
//int video_q=-1;
ogg_uint32_t keyframe_frequency=0;
int buf_delay=-1;

//long begin_sec=-1;
//long begin_usec=0;
//long end_sec=-1;
//long end_usec=0;

 ogg_int64_t samples_sofar = 0;

int                 frame_state=-1;
ogg_int64_t         frames=0;
    unsigned char      *yuvframe_d[3] = { NULL, NULL, NULL };
unsigned char      **yuvframe = yuvframe_d;
th_ycbcr_buffer     ycbcr;

 
  int c,long_option_index,ret;

  ogg_stream_state to; /* take physical pages, weld into a logical
                           stream of packets */
  ogg_stream_state vo; /* take physical pages, weld into a logical
                           stream of packets */
  ogg_page         og; /* one Ogg bitstream page.  Vorbis packets are inside */
  ogg_packet       op; /* one raw packet of data for decode */

  th_enc_ctx      *td;
  th_info          ti;
  th_comment       tc;

  vorbis_info      vi; /* struct that stores all the static vorbis bitstream
                          settings */
  vorbis_comment   vc; /* struct that stores all the user comments */

  vorbis_dsp_state vd; /* central working state for the packet->PCM decoder */
  vorbis_block     vb; /* local working space for packet->PCM decode */

  int speed= (encoding == THEORA_ENCODING_BEST ? 0 :
                encoding == THEORA_ENCODING_REALTIME ? 10000000 :
                -1);
  speed = -1; // not really dealing with this right now
  int audioflag=0;
  int videoflag=0;
  int akbps=0;
  int vkbps=0;
  int soft_target=0;

  ogg_int64_t audio_bytesout=0;
  ogg_int64_t video_bytesout=0;
  double timebase;

  //fpos_t video_rewind_pos;
  //int twopass=0;
  int passno;

  //clock_t clock_start=clock();
  //clock_t clock_end;
  double elapsed;


  if(soft_target){
    if(video_r<=0){
      assert(false);
    }
    if(video_q==-1)
      video_q=0;
  }else{
    if(video_q==-1){
      if(video_r>0)
        video_q=0;
      else
        video_q=48;
    }
  }

  if(keyframe_frequency<=0){
    /*Use a default keyframe frequency of 64 for 1-pass (streaming) mode, and
       256 for two-pass mode.*/
    keyframe_frequency=twopass?256:64;
  }



  /* Set up Ogg output stream */
  srand(time(NULL));
  ogg_stream_init(&to,rand()); /* oops, add one to the above */

  /* initialize Vorbis assuming we have audio to compress. */
  if(do_audio && twopass!=1){
    ogg_stream_init(&vo,rand());
    vorbis_info_init(&vi);
    if(audio_q>-99)
      ret = vorbis_encode_init_vbr(&vi,audio_ch,audio_hz,audio_q);
    else
      ret = vorbis_encode_init(&vi,audio_ch,audio_hz,-1,
                               (int)(64870*(ogg_int64_t)audio_r>>16),-1);
    if(ret){
      assert(false);
    }

    vorbis_comment_init(&vc);
    vorbis_analysis_init(&vd,&vi);
    vorbis_block_init(&vd,&vb);
  }

  for(passno=(twopass==3?1:twopass);passno<=(twopass==3?2:twopass) && result == malNoError;passno++){
    /* Set up Theora encoder */
    if(!do_video){
      assert(false);
    }
    /* Theora has a divisible-by-sixteen restriction for the encoded frame size */
    /* scale the picture size up to the nearest /16 and calculate offsets */
    frame_w=pic_w+15&~0xF;
    frame_h=pic_h+15&~0xF;
    /*Force the offsets to be even so that chroma samples line up like we
       expect.*/
    pic_x=frame_w-pic_w>>1&~1;
    pic_y=frame_h-pic_h>>1&~1;
    th_info_init(&ti);
    ti.frame_width=frame_w;
    ti.frame_height=frame_h;
    ti.pic_width=pic_w;
    ti.pic_height=pic_h;
    ti.pic_x=pic_x;
    ti.pic_y=pic_y;
    ti.fps_numerator=video_fps_n;
    ti.fps_denominator=video_fps_d;
    ti.aspect_numerator=video_par_n;
    ti.aspect_denominator=video_par_d;
    ti.colorspace=TH_CS_UNSPECIFIED;
    /*Account for the Ogg page overhead.
      This is 1 byte per 255 for lacing values, plus 26 bytes per 4096 bytes for
       the page header, plus approximately 1/2 byte per packet (not accounted for
       here).*/
    ti.target_bitrate=(int)(64870*(ogg_int64_t)video_r>>16);
    ti.quality=video_q;
    ti.keyframe_granule_shift=ilog(keyframe_frequency-1);
    if(dst_c_dec_h==2){
      if(dst_c_dec_v==2)ti.pixel_fmt=TH_PF_420;
      else ti.pixel_fmt=TH_PF_422;
    }
    else ti.pixel_fmt=TH_PF_444;
    td=th_encode_alloc(&ti);
    th_info_clear(&ti);
    if(td==NULL){
      assert(false);
    }
    /* setting just the granule shift only allows power-of-two keyframe
       spacing.  Set the actual requested spacing. */
    ret=th_encode_ctl(td,TH_ENCCTL_SET_KEYFRAME_FREQUENCY_FORCE,
     &keyframe_frequency,sizeof(keyframe_frequency-1));
    if(ret<0){
      //fprintf(stderr,"Could not set keyframe interval to %d.\n",(int)keyframe_frequency);
    }
    if(vp3_compatible){
      ret=th_encode_ctl(td,TH_ENCCTL_SET_VP3_COMPATIBLE,&vp3_compatible,
       sizeof(vp3_compatible));
      if(ret<0||!vp3_compatible){
        //fprintf(stderr,"Could not enable strict VP3 compatibility.\n");
        if(ret>=0){
          //fprintf(stderr,"Ensure your source format is supported by VP3.\n");
          //fprintf(stderr,
          // "(4:2:0 pixel format, width and height multiples of 16).\n");
        }
      }
    }
    if(soft_target){
      /* reverse the rate control flags to favor a 'long time' strategy */
      int arg = TH_RATECTL_CAP_UNDERFLOW;
      ret=th_encode_ctl(td,TH_ENCCTL_SET_RATE_FLAGS,&arg,sizeof(arg));
      //if(ret<0)
        //fprintf(stderr,"Could not set encoder flags for --soft-target\n");
      /* Default buffer control is overridden on two-pass */
      if(!twopass&&buf_delay<0){
        if((keyframe_frequency*7>>1) > 5*video_fps_n/video_fps_d)
          arg=keyframe_frequency*7>>1;
        else
          arg=5*video_fps_n/video_fps_d;
        ret=th_encode_ctl(td,TH_ENCCTL_SET_RATE_BUFFER,&arg,sizeof(arg));
        //if(ret<0)
        //  fprintf(stderr,"Could not set rate control buffer for --soft-target\n");
      }
    }
    /* set up two-pass if needed */
    if(passno==1){
      unsigned char *buffer;
      int bytes;
      bytes=th_encode_ctl(td,TH_ENCCTL_2PASS_OUT,&buffer,sizeof(buffer));
      if(bytes<0){
        assert(false);
      }
      /*Perform a seek test to ensure we can overwrite this placeholder data at
         the end; this is better than letting the user sit through a whole
         encode only to find out their pass 1 file is useless at the end.*/
      if(fseek_buf(vbr_buffer_pos,0,SEEK_SET)<0){
        assert(false);
      }
      if(fwrite_buf(buffer,1,bytes,vbr_buffer, vbr_buffer_pos, vbr_buffer_size, memorySuite)<bytes){
        assert(false);
      }
      //fflush(twopass_file);
    }
    if(passno==2){
      /*Enable the second pass here.
        We make this call just to set the encoder into 2-pass mode, because
         by default enabling two-pass sets the buffer delay to the whole file
         (because there's no way to explicitly request that behavior).
        If we waited until we were actually encoding, it would overwite our
         settings.*/
      if(th_encode_ctl(td,TH_ENCCTL_2PASS_IN,NULL,0)<0){
        assert(false);
      }
      if(twopass==3){
        /* 'automatic' second pass */
        //if(fsetpos(video,&video_rewind_pos)<0){
        //  fprintf(stderr,"Could not rewind video input file for second pass!\n");
        //  exit(1);
        //}
         if(fseek_buf(vbr_buffer_pos,0,SEEK_SET)<0){
          assert(false);
        }
        frame_state=0;
        frames=0;
      }
    }
    /*Now we can set the buffer delay if the user requested a non-default one
       (this has to be done after two-pass is enabled).*/
    if(passno!=1&&buf_delay>=0){
      ret=th_encode_ctl(td,TH_ENCCTL_SET_RATE_BUFFER,
       &buf_delay,sizeof(buf_delay));
      if(ret<0){
        //fprintf(stderr,"Warning: could not set desired buffer delay.\n");
      }
    }
    /*Speed should also be set after the current encoder mode is established,
       since the available speed levels may change depending.*/
    if(speed>=0){
      int speed_max;
      int ret;
      ret=th_encode_ctl(td,TH_ENCCTL_GET_SPLEVEL_MAX,
       &speed_max,sizeof(speed_max));
      if(ret<0){
        //fprintf(stderr,"Warning: could not determine maximum speed level.\n");
        speed_max=0;
      }
      ret=th_encode_ctl(td,TH_ENCCTL_SET_SPLEVEL,&speed,sizeof(speed));
      if(ret<0){
        //fprintf(stderr,"Warning: could not set speed level to %i of %i\n",
        // speed,speed_max);
        if(speed>speed_max){
          //fprintf(stderr,"Setting it to %i instead\n",speed_max);
        }
        ret=th_encode_ctl(td,TH_ENCCTL_SET_SPLEVEL,
         &speed_max,sizeof(speed_max));
        if(ret<0){
          //fprintf(stderr,"Warning: could not set speed level to %i of %i\n",
          // speed_max,speed_max);
        }
      }
    }
    /* write the bitstream header packets with proper page interleave */
    th_comment_init(&tc);
    /* first packet will get its own page automatically */
    if(th_encode_flushheader(td,&tc,&op)<=0){
      assert(false);
    }
    if(passno!=1){
      ogg_stream_packetin(&to,&op);
      if(ogg_stream_pageout(&to,&og)!=1){
        assert(false);
      }
      fwrite_pr(og.header,1,og.header_len,fileObject, fileSuite);
      fwrite_pr(og.body,1,og.body_len,fileObject, fileSuite);
    }
    /* create the remaining theora headers */
    for(;;){
      ret=th_encode_flushheader(td,&tc,&op);
      if(ret<0){
        assert(false);
      }
      else if(!ret)break;
      if(passno!=1)ogg_stream_packetin(&to,&op);
    }
    if(do_audio && passno!=1){
      ogg_packet header;
      ogg_packet header_comm;
      ogg_packet header_code;
      vorbis_analysis_headerout(&vd,&vc,&header,&header_comm,&header_code);
      ogg_stream_packetin(&vo,&header); /* automatically placed in its own
                                           page */
      if(ogg_stream_pageout(&vo,&og)!=1){
        assert(false);
      }
      fwrite_pr(og.header,1,og.header_len,fileObject, fileSuite);
      fwrite_pr(og.body,1,og.body_len,fileObject, fileSuite);
      /* remaining vorbis header packets */
      ogg_stream_packetin(&vo,&header_comm);
      ogg_stream_packetin(&vo,&header_code);
    }
    /* Flush the rest of our headers. This ensures
       the actual data in each stream will start
       on a new page, as per spec. */
    if(passno!=1){
      for(;;){
        int result = ogg_stream_flush(&to,&og);
        if(result<0){
          /* can't get here */
          assert(false);
        }
        if(result==0)break;
        fwrite_pr(og.header,1,og.header_len,fileObject, fileSuite);
        fwrite_pr(og.body,1,og.body_len,fileObject, fileSuite);
      }
    }
    if(do_audio && passno!=1){
      for(;;){
        int result=ogg_stream_flush(&vo,&og);
        if(result<0){
          /* can't get here */
          assert(false);
        }
        if(result==0)break;
        fwrite_pr(og.header,1,og.header_len,fileObject, fileSuite);
        fwrite_pr(og.body,1,og.body_len,fileObject, fileSuite);
      }
    }
    /* setup complete.  Raw processing loop */
    for(;;){
      int audio_or_video=-1;
      if(passno==1){
        ogg_packet op;
		
		
		
        int ret=fetch_and_process_video_packet(renderSuite, videoRenderID, renderParms, ticksPerSecond,
 pixSuite, pix2Suite, vbr_buffer, vbr_buffer_pos, vbr_buffer_size, memorySuite,passno,
 td,&op, video_fps_n, video_fps_d, begin_sec, begin_usec, end_sec, end_usec,
 y4m_dst_buf_sz, y4m_aux_buf_sz, pic_w, pic_h, dst_c_dec_h, dst_c_dec_v,
 frame_state, frames, yuvframe, ycbcr);
		
		
		
		
        if(ret<0)break;
        if(op.e_o_s)break; /* end of stream */
        timebase=th_granule_time(td,op.granulepos);
        audio_or_video=1;
      }else{
        double audiotime;
        double videotime;
        ogg_page audiopage;
        ogg_page videopage;
		
		
		
        /* is there an audio page flushed?  If not, fetch one if possible */
        audioflag=fetch_and_process_audio(audioSuite, audioRenderID, maxBlip,
 &audiopage, &vo, &vd, &vb,
 audioflag, audio_hz, begin_sec, begin_usec, end_sec, end_usec, samples_sofar);
		
		
		
		
		
        /* is there a video page flushed?  If not, fetch one if possible */
        videoflag=fetch_and_process_video(renderSuite, videoRenderID, renderParms, ticksPerSecond,
					pixSuite, pix2Suite, &videopage,
					&to,td,vbr_buffer, vbr_buffer_pos, vbr_buffer_size, memorySuite,passno,
					videoflag, video_fps_n, video_fps_d, begin_sec, begin_usec, end_sec, end_usec,
					y4m_dst_buf_sz, y4m_aux_buf_sz, pic_w, pic_h, dst_c_dec_h, dst_c_dec_v,
					frame_state, frames, yuvframe, ycbcr);
	
		

        /* no pages of either?  Must be end of stream. */
        if(!audioflag && !videoflag)break;
        /* which is earlier; the end of the audio page or the end of the
           video page? Flush the earlier to stream */
        audiotime=
        audioflag?vorbis_granule_time(&vd,ogg_page_granulepos(&audiopage)):-1;
        videotime=
        videoflag?th_granule_time(td,ogg_page_granulepos(&videopage)):-1;
        if(!audioflag){
          audio_or_video=1;
        } else if(!videoflag) {
          audio_or_video=0;
        } else {
          if(audiotime<videotime)
            audio_or_video=0;
          else
            audio_or_video=1;
        }
        if(audio_or_video==1){
          /* flush a video page */
          video_bytesout+=fwrite_pr(videopage.header,1,videopage.header_len,fileObject, fileSuite);
          video_bytesout+=fwrite_pr(videopage.body,1,videopage.body_len,fileObject, fileSuite);
          videoflag=0;
          timebase=videotime;
        }else{
          /* flush an audio page */
          audio_bytesout+=fwrite_pr(audiopage.header,1,audiopage.header_len,fileObject, fileSuite);
          audio_bytesout+=fwrite_pr(audiopage.body,1,audiopage.body_len,fileObject, fileSuite);
          audioflag=0;
          timebase=audiotime;
        }
      }
      if(!quiet&&timebase>0){
        int hundredths=(int)(timebase*100-(long)timebase*100);
        int seconds=(long)timebase%60;
        int minutes=((long)timebase/60)%60;
        int hours=(long)timebase/3600;
        if(audio_or_video)vkbps=(int)rint(video_bytesout*8./timebase*.001);
        else akbps=(int)rint(audio_bytesout*8./timebase*.001);
        //fprintf(stderr,
        //        "\r      %d:%02d:%02d.%02d audio: %dkbps video: %dkbps                 ",
        //        hours,minutes,seconds,hundredths,akbps,vkbps);
      }
    
      float progress = (double)frames / (double)((((end_sec + (end_usec * 0.000001)) - (begin_sec + (begin_usec * 0.000001))) * video_fps_n / video_fps_d) - 1);
    
      if(twopass > 1)
          progress = (progress / 2.f) + (passno == 2 ? 0.5f : 0.f);
    
      result = exportProgressSuite->UpdateProgressPercent(inExportID, progress);
    
      if(result == suiteError_ExporterSuspended)
      {
        result = exportProgressSuite->WaitForResume(inExportID);
      }
    
      if(result != malNoError)
        break;
    }
    if(do_video)th_encode_free(td);
  }
    
    for(int i=0; i < 3; i++)
        if(yuvframe_d[i] != NULL)
            free(yuvframe_d[i]);

  /* clear out state */
  if(do_audio && twopass!=1){
    ogg_stream_clear(&vo);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    //if(audio!=stdin)fclose(audio);
  }
  if(do_video){
    ogg_stream_clear(&to);
    th_comment_clear(&tc);
    //if(video!=stdin)fclose(video);
  }

  //if(outfile && outfile!=stdout)fclose(outfile);
  //if(twopass_file)fclose(twopass_file);

  //clock_end=clock();
  //elapsed=(clock_end-clock_start)/(double)CLOCKS_PER_SEC;

  return result;

}


static prMALError
exSDKExport(
	exportStdParms	*stdParmsP,
	exDoExportRec	*exportInfoP)
{
	prMALError					result					= malNoError;
	ExportSettings				*mySettings				= reinterpret_cast<ExportSettings*>(exportInfoP->privateData);
	PrSDKExportParamSuite		*paramSuite				= mySettings->exportParamSuite;
	PrSDKExportInfoSuite		*exportInfoSuite		= mySettings->exportInfoSuite;
	PrSDKSequenceRenderSuite	*renderSuite			= mySettings->sequenceRenderSuite;
	PrSDKSequenceAudioSuite		*audioSuite				= mySettings->sequenceAudioSuite;
	PrSDKMemoryManagerSuite		*memorySuite			= mySettings->memorySuite;
	PrSDKPPixCreatorSuite		*pixCreatorSuite		= mySettings->ppixCreatorSuite;
	PrSDKPPixSuite				*pixSuite				= mySettings->ppixSuite;
	PrSDKPPix2Suite				*pix2Suite				= mySettings->ppix2Suite;


	PrTime ticksPerSecond = 0;
	mySettings->timeSuite->GetTicksPerSecond(&ticksPerSecond);
	
	
	csSDK_uint32 exID = exportInfoP->exporterPluginID;
	csSDK_int32 gIdx = 0;
	
	exParamValues matchSourceP, widthP, heightP, pixelAspectRatioP, fieldTypeP, frameRateP;
	
	paramSuite->GetParamValue(exID, gIdx, ADBEVideoMatchSource, &matchSourceP);
	
	if(matchSourceP.value.intValue)
	{
		// get current settings
		PrParam curr_widthP, curr_heightP, curr_parN, curr_parD, curr_fieldTypeP, curr_frameRateP;
		
		exportInfoSuite->GetExportSourceInfo(exID, kExportInfo_VideoWidth, &curr_widthP);
		exportInfoSuite->GetExportSourceInfo(exID, kExportInfo_VideoHeight, &curr_heightP);
		exportInfoSuite->GetExportSourceInfo(exID, kExportInfo_PixelAspectNumerator, &curr_parN);
		exportInfoSuite->GetExportSourceInfo(exID, kExportInfo_PixelAspectDenominator, &curr_parD);
		exportInfoSuite->GetExportSourceInfo(exID, kExportInfo_VideoFieldType, &curr_fieldTypeP);
		exportInfoSuite->GetExportSourceInfo(exID, kExportInfo_VideoFrameRate, &curr_frameRateP);
		
		widthP.value.intValue = curr_widthP.mInt32;
		heightP.value.intValue = curr_heightP.mInt32;
		
		pixelAspectRatioP.value.ratioValue.numerator = curr_parN.mInt32;
		pixelAspectRatioP.value.ratioValue.denominator = curr_parD.mInt32;
		
		fieldTypeP.value.intValue = curr_fieldTypeP.mInt32;
		frameRateP.value.timeValue = curr_frameRateP.mInt64;
	}
	else
	{
		paramSuite->GetParamValue(exID, gIdx, ADBEVideoWidth, &widthP);
		paramSuite->GetParamValue(exID, gIdx, ADBEVideoHeight, &heightP);
		paramSuite->GetParamValue(exID, gIdx, ADBEVideoAspect, &pixelAspectRatioP);
		paramSuite->GetParamValue(exID, gIdx, ADBEVideoFieldType, &fieldTypeP);
		paramSuite->GetParamValue(exID, gIdx, ADBEVideoFPS, &frameRateP);
	}
	
	
	exParamValues sampleRateP, channelTypeP;
	paramSuite->GetParamValue(exID, gIdx, ADBEAudioRatePerSecond, &sampleRateP);
	paramSuite->GetParamValue(exID, gIdx, ADBEAudioNumChannels, &channelTypeP);
	
	const PrAudioChannelType audioFormat = (PrAudioChannelType)channelTypeP.value.intValue;
	const int audioChannels = (audioFormat == kPrAudioChannelType_51 ? 6 :
								audioFormat == kPrAudioChannelType_Mono ? 1 :
								2);
	
	exParamValues methodP, videoQualityP, bitrateP, encodingP; //customArgsP;
	paramSuite->GetParamValue(exID, gIdx, TheoraVideoMethod, &methodP);
	paramSuite->GetParamValue(exID, gIdx, TheoraVideoQuality, &videoQualityP);
	paramSuite->GetParamValue(exID, gIdx, TheoraVideoBitrate, &bitrateP);
    paramSuite->GetParamValue(exID, gIdx, TheoraVideoBitrate, &bitrateP);
    paramSuite->GetParamValue(exID, gIdx, TheoraVideoEncoding, &encodingP);
	//paramSuite->GetParamValue(exID, gIdx, TheoraCustomArgs, &customArgsP);
	
	Theora_Video_Method method = (Theora_Video_Method)methodP.value.intValue;
	
	//char customArgs[256];
	//ncpyUTF16(customArgs, customArgsP.paramString, 255);
	//customArgs[255] = '\0';
	

	exParamValues audioMethodP, audioQualityP, audioBitrateP;
	paramSuite->GetParamValue(exID, gIdx, TheoraAudioMethod, &audioMethodP);
	paramSuite->GetParamValue(exID, gIdx, TheoraAudioQuality, &audioQualityP);
	paramSuite->GetParamValue(exID, gIdx, TheoraAudioBitrate, &audioBitrateP);
	
    
    Theora_Chroma_Sampling chroma_samp = THEORA_CHROMA_420; // FYI, nobody can handle 444 right now, it seems
    
    PrPixelFormat preferred_pixel_format = chroma_samp == THEORA_CHROMA_420 ? PrPixelFormat_YUV_420_MPEG2_FRAME_PICTURE_PLANAR_8u_709 :
                                            chroma_samp == THEORA_CHROMA_422 ? PrPixelFormat_YUYV_422_8u_709 :
                                            PrPixelFormat_BGRA_4444_8u;
	
	SequenceRender_ParamsRec renderParms;
	PrPixelFormat pixelFormats[] = { preferred_pixel_format,
									PrPixelFormat_BGRA_4444_8u };// must support BGRA, even if I don't want to
	
	renderParms.inRequestedPixelFormatArray = pixelFormats;
	renderParms.inRequestedPixelFormatArrayCount = 3;
	renderParms.inWidth = widthP.value.intValue;
	renderParms.inHeight = heightP.value.intValue;
	renderParms.inPixelAspectRatioNumerator = pixelAspectRatioP.value.ratioValue.numerator;
	renderParms.inPixelAspectRatioDenominator = pixelAspectRatioP.value.ratioValue.denominator;
	renderParms.inRenderQuality = kPrRenderQuality_High;
	renderParms.inFieldType = fieldTypeP.value.intValue;
	renderParms.inDeinterlace = kPrFalse;
	renderParms.inDeinterlaceQuality = kPrRenderQuality_High;
	renderParms.inCompositeOnBlack = kPrTrue;
	
	
	csSDK_uint32 videoRenderID = 0;
	
	if(exportInfoP->exportVideo)
	{
		result = renderSuite->MakeVideoRenderer(exID, &videoRenderID, frameRateP.value.timeValue);
	}
	
	csSDK_uint32 audioRenderID = 0;
	
	if(exportInfoP->exportAudio)
	{
		result = audioSuite->MakeAudioRenderer(exID,
												exportInfoP->startTime,
												audioFormat,
												kPrAudioSampleType_32BitFloat,
												sampleRateP.value.floatValue, 
												&audioRenderID);
	}

	
	csSDK_int32 maxBlip = 0;
	mySettings->sequenceAudioSuite->GetMaxBlip(audioRenderID, frameRateP.value.timeValue, &maxBlip);

	exRatioValue fps;
	get_framerate(ticksPerSecond, frameRateP.value.timeValue, &fps);


	PrMemoryPtr vbr_buffer = NULL;
	size_t vbr_buffer_pos = 0;
	size_t vbr_buffer_size = 0;

	
	long begin_sec = (exportInfoP->startTime / ticksPerSecond);
	long begin_usec = ((exportInfoP->startTime - (begin_sec * ticksPerSecond)) * 1000000) / ticksPerSecond;
	long end_sec = (exportInfoP->endTime / ticksPerSecond);
	long end_usec = ((exportInfoP->endTime - (end_sec * ticksPerSecond)) * 1000000) / ticksPerSecond;
	
	
	
	int twopass = 0;
	
	float audio_q=.1f;
	int audio_r=-1;
	int video_r=-1;
	int video_q=-1;
	
	if(audioMethodP.value.intValue == OGG_QUALITY)
		audio_q = audioQualityP.value.floatValue;
	else
		audio_r = audioBitrateP.value.intValue;
		
	
	if(methodP.value.intValue == THEORA_METHOD_QUALITY)
		video_q = videoQualityP.value.intValue;
	else
	{
		video_r = bitrateP.value.intValue * 1024;
		
		if(methodP.value.intValue == THEORA_METHOD_VBR)
			twopass = 3;
	}
	
	
	mySettings->exportFileSuite->Open(exportInfoP->fileObject);
	
	
	result = compress_main(renderSuite, videoRenderID, renderParms, ticksPerSecond,
							pixSuite, pix2Suite, vbr_buffer, vbr_buffer_pos, vbr_buffer_size, memorySuite,
							audioSuite, audioRenderID, maxBlip,
							mySettings->exportProgressSuite, exID,
							exportInfoP->fileObject, mySettings->exportFileSuite,
							exportInfoP->exportVideo, exportInfoP->exportAudio,
							audioChannels, sampleRateP.value.floatValue,
							widthP.value.intValue, heightP.value.intValue, fps.numerator, fps.denominator,
							pixelAspectRatioP.value.ratioValue.numerator, pixelAspectRatioP.value.ratioValue.denominator,
							audio_q, audio_r, video_q, video_r, twopass, (Theora_Video_Encoding)encodingP.value.intValue, chroma_samp,
							begin_sec, begin_usec, end_sec, end_usec);
	
	
	mySettings->exportFileSuite->Close(exportInfoP->fileObject);
	
	
	if(vbr_buffer != NULL)
		memorySuite->PrDisposePtr(vbr_buffer);
	
	
	if(exportInfoP->exportVideo)
		renderSuite->ReleaseVideoRenderer(exID, videoRenderID);

	if(exportInfoP->exportAudio)
		audioSuite->ReleaseAudioRenderer(exID, audioRenderID);
	

	return result;
}




DllExport PREMPLUGENTRY xSDKExport (
	csSDK_int32		selector, 
	exportStdParms	*stdParmsP, 
	void			*param1, 
	void			*param2)
{
	prMALError result = exportReturn_Unsupported;
	
	switch (selector)
	{
		case exSelStartup:
			result = exSDKStartup(	stdParmsP, 
									reinterpret_cast<exExporterInfoRec*>(param1));
			break;

		case exSelBeginInstance:
			result = exSDKBeginInstance(stdParmsP,
										reinterpret_cast<exExporterInstanceRec*>(param1));
			break;

		case exSelEndInstance:
			result = exSDKEndInstance(	stdParmsP,
										reinterpret_cast<exExporterInstanceRec*>(param1));
			break;

		case exSelGenerateDefaultParams:
			result = exSDKGenerateDefaultParams(stdParmsP,
												reinterpret_cast<exGenerateDefaultParamRec*>(param1));
			break;

		case exSelPostProcessParams:
			result = exSDKPostProcessParams(stdParmsP,
											reinterpret_cast<exPostProcessParamsRec*>(param1));
			break;

		case exSelGetParamSummary:
			result = exSDKGetParamSummary(	stdParmsP,
											reinterpret_cast<exParamSummaryRec*>(param1));
			break;

		case exSelQueryOutputSettings:
			result = exSDKQueryOutputSettings(	stdParmsP,
												reinterpret_cast<exQueryOutputSettingsRec*>(param1));
			break;

		case exSelQueryExportFileExtension:
			result = exSDKFileExtension(stdParmsP,
										reinterpret_cast<exQueryExportFileExtensionRec*>(param1));
			break;

		case exSelValidateParamChanged:
			result = exSDKValidateParamChanged(	stdParmsP,
												reinterpret_cast<exParamChangedRec*>(param1));
			break;

		case exSelValidateOutputSettings:
			result = malNoError;
			break;

		case exSelExport:
			result = exSDKExport(	stdParmsP,
									reinterpret_cast<exDoExportRec*>(param1));
			break;
	}
	
	return result;
}
