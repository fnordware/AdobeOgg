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


#include "Theora_Premiere_Import.h"


extern "C" {

#include "theora/theoradec.h"

//#include <vorbis/codec.h>

}


#include <assert.h>
#include <math.h>

#include <string>

#ifdef PRMAC_ENV
	#include <mach/mach.h>
#endif

int g_num_cpus = 1;

#undef assert
static void assert(bool b)
{
	bool c = b;
	
	if(!c)
	{
		b = c;
	}
}

#define OV_OK	0
#define OV_FALSE (-1)

static size_t ogg_read_func(void *ptr, size_t size, size_t nmemb, void *datasource)
{
	imFileRef fp = static_cast<imFileRef>(datasource);
	
#ifdef PRWIN_ENV	
	DWORD count = size * nmemb, out;
	
	BOOL result = ReadFile(fp, (LPVOID)ptr, count, &out, NULL);

	return (out / size);
#else
	ByteCount count = size * nmemb, out = 0;
	
	OSErr result = FSReadFork(CAST_REFNUM(fp), fsAtMark, 0, count, ptr, &out);

	return (out / size);
#endif
}


static int ogg_seek_func(void *datasource, ogg_int64_t offset, int whence)
{
	imFileRef fp = static_cast<imFileRef>(datasource);
	
#ifdef PRWIN_ENV
	LARGE_INTEGER lpos;

	lpos.QuadPart = offset;

	DWORD method = ( whence == SEEK_SET ? FILE_BEGIN :
						whence == SEEK_CUR ? FILE_CURRENT :
						whence == SEEK_END ? FILE_END :
						FILE_CURRENT );

#if _MSC_VER < 1300
	DWORD pos = SetFilePointer(fp, lpos.u.LowPart, &lpos.u.HighPart, method);

	BOOL result = (pos != 0xFFFFFFFF || NO_ERROR == GetLastError());
#else
	BOOL result = SetFilePointerEx(fp, lpos, NULL, method);
#endif

	return (result ? OV_OK : OV_FALSE);
#else
	UInt16 positionMode = ( whence == SEEK_SET ? fsFromStart :
							whence == SEEK_CUR ? fsFromMark :
							whence == SEEK_END ? fsFromLEOF :
							fsFromMark );
	
	OSErr result = FSSetForkPosition(CAST_REFNUM(fp), positionMode, offset);

	return (result == noErr ? OV_OK : OV_FALSE);
#endif
}


static long ogg_tell_func(void *datasource)
{
	imFileRef fp = static_cast<imFileRef>(datasource);
	
#ifdef PRWIN_ENV
	long pos;
	LARGE_INTEGER lpos, zero;

	zero.QuadPart = 0;

	BOOL result = SetFilePointerEx(fp, zero, &lpos, FILE_CURRENT);

	pos = lpos.QuadPart;
	
	return pos;
#else
	long pos;
	SInt64 lpos;

	OSErr result = FSGetForkPosition(CAST_REFNUM(fp), &lpos);
	
	pos = lpos;
	
	return pos;
#endif
}


#if IMPORTMOD_VERSION <= IMPORTMOD_VERSION_9
typedef PrSDKPPixCacheSuite2 PrCacheSuite;
#define PrCacheVersion	kPrSDKPPixCacheSuiteVersion2
#else
typedef PrSDKPPixCacheSuite PrCacheSuite;
#define PrCacheVersion	kPrSDKPPixCacheSuiteVersion
#endif


typedef struct TheoraCtx {
	ogg_sync_state    oy;
	ogg_page          og;
	ogg_packet        op;
	ogg_stream_state  to;
	th_info           ti;
	th_comment        tc;
	th_setup_info    *ts;
	th_dec_ctx       *td;
	
	bool have_video;
	
	TheoraCtx() : ts(NULL), td(NULL), have_video(false) {}
} TheoraCtx;


typedef struct
{	
	csSDK_int32				importerID;
	csSDK_int32				fileType;
	csSDK_int32				width;
	csSDK_int32				height;
	csSDK_int32				frameRateNum;
	csSDK_int32				frameRateDen;
	float					audioSampleRate;
	int						numChannels;
	
	//TheoraCtx				*th;
	
	PlugMemoryFuncsPtr		memFuncs;
	SPBasicSuite			*BasicSuite;
	PrSDKPPixCreatorSuite	*PPixCreatorSuite;
	PrCacheSuite			*PPixCacheSuite;
	PrSDKPPixSuite			*PPixSuite;
	PrSDKPPix2Suite			*PPix2Suite;
	PrSDKTimeSuite			*TimeSuite;
	PrSDKImporterFileManagerSuite *FileSuite;
} ImporterLocalRec8, *ImporterLocalRec8Ptr, **ImporterLocalRec8H;


static prMALError 
SDKInit(
	imStdParms		*stdParms, 
	imImportInfoRec	*importInfo)
{
	importInfo->canSave				= kPrFalse;		// Can 'save as' files to disk, real file only.
	importInfo->canDelete			= kPrFalse;		// File importers only, use if you only if you have child files
	importInfo->canCalcSizes		= kPrFalse;		// These are for importers that look at a whole tree of files so
													// Premiere doesn't know about all of them.
	importInfo->canTrim				= kPrFalse;
	
	importInfo->hasSetup			= kPrFalse;		// Set to kPrTrue if you have a setup dialog
	importInfo->setupOnDblClk		= kPrFalse;		// If user dbl-clicks file you imported, pop your setup dialog
	
	importInfo->dontCache			= kPrFalse;		// Don't let Premiere cache these files
	importInfo->keepLoaded			= kPrFalse;		// If you MUST stay loaded use, otherwise don't: play nice
	importInfo->priority			= 0;
	
	importInfo->avoidAudioConform	= kPrTrue;		// If I let Premiere conform the audio, I get silence when
													// I try to play it in the program.  Seems like a bug to me.

#ifdef PRMAC_ENV
	// get number of CPUs using Mach calls
	host_basic_info_data_t hostInfo;
	mach_msg_type_number_t infoCount;
	
	infoCount = HOST_BASIC_INFO_COUNT;
	host_info(mach_host_self(), HOST_BASIC_INFO, 
			  (host_info_t)&hostInfo, &infoCount);
	
	g_num_cpus = hostInfo.avail_cpus;
#else // WIN_ENV
	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);

	g_num_cpus = systemInfo.dwNumberOfProcessors;
#endif

	return malNoError;
}


static prMALError 
SDKGetIndFormat(
	imStdParms		*stdParms, 
	csSDK_size_t	index, 
	imIndFormatRec	*SDKIndFormatRec)
{
	prMALError	result		= malNoError;
	char formatname[255]	= "Theora";
	char shortname[32]		= "Theora";
	char platformXten[256]	= "ogv\0\0";

	switch(index)
	{
		//	Add a case for each filetype.
		
		case 0:		
			SDKIndFormatRec->filetype			= 'Theo';

			SDKIndFormatRec->canWriteTimecode	= kPrFalse;
			SDKIndFormatRec->canWriteMetaData	= kPrFalse;

			SDKIndFormatRec->flags = xfCanImport;

			#ifdef PRWIN_ENV
			strcpy_s(SDKIndFormatRec->FormatName, sizeof (SDKIndFormatRec->FormatName), formatname);				// The long name of the importer
			strcpy_s(SDKIndFormatRec->FormatShortName, sizeof (SDKIndFormatRec->FormatShortName), shortname);		// The short (menu name) of the importer
			strcpy_s(SDKIndFormatRec->PlatformExtension, sizeof (SDKIndFormatRec->PlatformExtension), platformXten);	// The 3 letter extension
			#else
			strcpy(SDKIndFormatRec->FormatName, formatname);			// The Long name of the importer
			strcpy(SDKIndFormatRec->FormatShortName, shortname);		// The short (menu name) of the importer
			strcpy(SDKIndFormatRec->PlatformExtension, platformXten);	// The 3 letter extension
			#endif

			break;

		default:
			result = imBadFormatIndex;
	}

	return result;
}


static prMALError SetupTheora(TheoraCtx &ctx, imFileRef &fileRef)
{
	prMALError result = malNoError;

	
	ogg_seek_func(fileRef, 0, SEEK_SET);
	

	// all of this ripped right out of dump_video.c in libtheora

	// we keep all this stuff in a stuct, but use references to maintain the 
	// clean ogg style
	
	ogg_sync_state    &oy = ctx.oy;
	ogg_page          &og = ctx.og;
	ogg_packet        &op = ctx.op;
	ogg_stream_state  &to = ctx.to;
	th_info           &ti = ctx.ti;
	th_comment        &tc = ctx.tc;
	th_setup_info     *&ts = ctx.ts;
	th_dec_ctx        *&td = ctx.td;
	
	
	ogg_sync_init(&oy);
	
	th_comment_init(&tc);
	th_info_init(&ti);
	
	
	int stateflag = 0;
	int theora_p = 0;
	int theora_processing_headers = 0;
			
	while(!stateflag)
	{
		char *buffer = ogg_sync_buffer(&oy, 4096);
		
		int bytes = ogg_read_func(buffer, 1, 4096, fileRef);
		
		ogg_sync_wrote(&oy, bytes);
		
		if(bytes == 0)
			break;
		
		while(ogg_sync_pageout(&oy, &og) > 0)
		{
			ogg_stream_state test;
			
			// is this a mandated initial header? If not, stop parsing
			if( !ogg_page_bos(&og) )
			{
				// don't leak the page; get it into the appropriate stream
				if(theora_p)
					ogg_stream_pagein(&to, &og);
					
				stateflag = 1;
				
				break;
			}
			
			ogg_stream_init(&test, ogg_page_serialno(&og));
			
			ogg_stream_pagein(&test, &og);
			
			int got_packet = ogg_stream_packetpeek(&test, &op);
			
			if(got_packet == 1 && !theora_p &&
				(theora_processing_headers = th_decode_headerin(&ti, &tc, &ts, &op)) >= 0)
			{
				// it is theora -- save this stream state */
				memcpy(&to, &test, sizeof(test));
				
				theora_p = 1;
				
				// Advance past the successfully processed header.
				if(theora_processing_headers)
				{
					ogg_stream_packetout(&to, NULL);
				}
				else
				{
					// whatever it is, we don't care about it
					ogg_stream_clear(&test);
				}
			}
		}
	}
		
	// we're expecting more header packets.
	while(theora_p && theora_processing_headers && result == malNoError)
	{
		int ret;

		// look for further theora headers
		while(theora_processing_headers && (ret = ogg_stream_packetpeek(&to, &op)) && result == malNoError)
		{
			if(ret < 0)
				continue;
				
			theora_processing_headers = th_decode_headerin(&ti, &tc, &ts, &op);
			
			if(theora_processing_headers < 0)
			{
				result = imBadHeader;
			}
			else if(theora_processing_headers > 0)
			{
				// Advance past the successfully processed header.
				ogg_stream_packetout(&to, NULL);
			}
			
			theora_p++;
		}

		//Stop now so we don't fail if there aren't enough pages in a short stream.
		if(!(theora_p && theora_processing_headers))
			break;

		if(result == malNoError)
		{
			// The header pages/packets will arrive before anything else we
			//   care about, or the stream is not obeying spec

			if(ogg_sync_pageout(&oy, &og) > 0)
			{
				if(theora_p)
					ogg_stream_pagein(&to, &og); // demux into the appropriate stream
			}
			else
			{
				// someone needs more data
				char *buffer2 = ogg_sync_buffer(&oy, 4096);
				
				int bytes2 = ogg_read_func(buffer2, 1, 4096, fileRef);
				
				ogg_sync_wrote(&oy, bytes2);
				
				if(bytes2 == 0)
				{
					result = imBadFile;
				}
			}
		}
	}
	
	ctx.have_video = (theora_p > 0);
	
	return result;
}


static void TheoraSetdown(TheoraCtx &ctx)
{
	ogg_sync_state    &oy = ctx.oy;
	ogg_page          &og = ctx.og;
	ogg_packet        &op = ctx.op;
	ogg_stream_state  &to = ctx.to;
	th_info           &ti = ctx.ti;
	th_comment        &tc = ctx.tc;
	th_setup_info     *&ts = ctx.ts;
	th_dec_ctx        *&td = ctx.td;

	ogg_stream_clear(&to);
	
	if(ts)
	{
		th_setup_free(ts);
		ts = NULL;
	}
	
	if(td)
	{
		th_decode_free(td);
		td = NULL;
	}
		
	th_comment_clear(&tc);
	th_info_clear(&ti);
	ogg_sync_clear(&oy);
}



prMALError 
SDKOpenFile8(
	imStdParms		*stdParms, 
	imFileRef		*SDKfileRef, 
	imFileOpenRec8	*SDKfileOpenRec8)
{
	prMALError			result = malNoError;

	ImporterLocalRec8H	localRecH = NULL;
	ImporterLocalRec8Ptr localRecP = NULL;

	if(SDKfileOpenRec8->privatedata)
	{
		localRecH = (ImporterLocalRec8H)SDKfileOpenRec8->privatedata;

		stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(localRecH));

		localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *localRecH );
	}
	else
	{
		localRecH = (ImporterLocalRec8H)stdParms->piSuites->memFuncs->newHandle(sizeof(ImporterLocalRec8));
		SDKfileOpenRec8->privatedata = (PrivateDataPtr)localRecH;

		stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(localRecH));

		localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *localRecH );
		
		//localRecP->th = NULL;
		
		// Acquire needed suites
		localRecP->memFuncs = stdParms->piSuites->memFuncs;
		localRecP->BasicSuite = stdParms->piSuites->utilFuncs->getSPBasicSuite();
		if(localRecP->BasicSuite)
		{
			localRecP->BasicSuite->AcquireSuite(kPrSDKPPixCreatorSuite, kPrSDKPPixCreatorSuiteVersion, (const void**)&localRecP->PPixCreatorSuite);
			localRecP->BasicSuite->AcquireSuite(kPrSDKPPixCacheSuite, PrCacheVersion, (const void**)&localRecP->PPixCacheSuite);
			localRecP->BasicSuite->AcquireSuite(kPrSDKPPixSuite, kPrSDKPPixSuiteVersion, (const void**)&localRecP->PPixSuite);
			localRecP->BasicSuite->AcquireSuite(kPrSDKPPix2Suite, kPrSDKPPix2SuiteVersion, (const void**)&localRecP->PPix2Suite);
			localRecP->BasicSuite->AcquireSuite(kPrSDKTimeSuite, kPrSDKTimeSuiteVersion, (const void**)&localRecP->TimeSuite);
			localRecP->BasicSuite->AcquireSuite(kPrSDKImporterFileManagerSuite, kPrSDKImporterFileManagerSuiteVersion, (const void**)&localRecP->FileSuite);
		}

		localRecP->importerID = SDKfileOpenRec8->inImporterID;
		localRecP->fileType = SDKfileOpenRec8->fileinfo.filetype;
	}


	SDKfileOpenRec8->fileinfo.fileref = *SDKfileRef = reinterpret_cast<imFileRef>(imInvalidHandleValue);


	if(localRecP)
	{
		const prUTF16Char *path = SDKfileOpenRec8->fileinfo.filepath;
	
	#ifdef PRWIN_ENV
		HANDLE fileH = CreateFileW(path,
									GENERIC_READ,
									FILE_SHARE_READ,
									NULL,
									OPEN_EXISTING,
									FILE_ATTRIBUTE_NORMAL,
									NULL);
									
		if(fileH != imInvalidHandleValue)
		{
			SDKfileOpenRec8->fileinfo.fileref = *SDKfileRef = fileH;
		}
		else
			result = imFileOpenFailed;
	#else
		FSIORefNum refNum = CAST_REFNUM(imInvalidHandleValue);
				
		CFStringRef filePathCFSR = CFStringCreateWithCharacters(NULL, path, prUTF16CharLength(path));
													
		CFURLRef filePathURL = CFURLCreateWithFileSystemPath(NULL, filePathCFSR, kCFURLPOSIXPathStyle, false);
		
		if(filePathURL != NULL)
		{
			FSRef fileRef;
			Boolean success = CFURLGetFSRef(filePathURL, &fileRef);
			
			if(success)
			{
				HFSUniStr255 dataForkName;
				FSGetDataForkName(&dataForkName);
			
				OSErr err = FSOpenFork(	&fileRef,
										dataForkName.length,
										dataForkName.unicode,
										fsRdWrPerm,
										&refNum);
			}
										
			CFRelease(filePathURL);
		}
									
		CFRelease(filePathCFSR);

		if(CAST_FILEREF(refNum) != imInvalidHandleValue)
		{
			SDKfileOpenRec8->fileinfo.fileref = *SDKfileRef = CAST_FILEREF(refNum);
		}
		else
			result = imFileOpenFailed;
	#endif

	}

	if(result == malNoError)
	{
		assert(0 == ogg_tell_func(*SDKfileRef));
		
		
		TheoraCtx ctx;
				
		result = SetupTheora(ctx, *SDKfileRef);
		
		TheoraSetdown(ctx);
	}
	
	// close file and delete private data if we got a bad file
	if(result != malNoError)
	{
		if(SDKfileOpenRec8->privatedata)
		{
			stdParms->piSuites->memFuncs->disposeHandle(reinterpret_cast<PrMemoryHandle>(SDKfileOpenRec8->privatedata));
			SDKfileOpenRec8->privatedata = NULL;
		}
	}
	else
	{
		stdParms->piSuites->memFuncs->unlockHandle(reinterpret_cast<char**>(SDKfileOpenRec8->privatedata));
	}

	return result;
}



static prMALError 
SDKQuietFile(
	imStdParms			*stdParms, 
	imFileRef			*SDKfileRef, 
	void				*privateData)
{
	// "Quiet File" really means close the file handle, but we're still
	// using it and might open it again, so hold on to any stored data
	// structures you don't want to re-create.

	// If file has not yet been closed
	if(SDKfileRef && *SDKfileRef != imInvalidHandleValue)
	{
		ImporterLocalRec8H ldataH	= reinterpret_cast<ImporterLocalRec8H>(privateData);

		stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(ldataH));

		ImporterLocalRec8Ptr localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *ldataH );


		stdParms->piSuites->memFuncs->unlockHandle(reinterpret_cast<char**>(ldataH));

	#ifdef PRWIN_ENV
		CloseHandle(*SDKfileRef);
	#else
		FSCloseFork( CAST_REFNUM(*SDKfileRef) );
	#endif
	
		*SDKfileRef = imInvalidHandleValue;
	}

	return malNoError; 
}


static prMALError 
SDKCloseFile(
	imStdParms			*stdParms, 
	imFileRef			*SDKfileRef,
	void				*privateData) 
{
	ImporterLocalRec8H ldataH	= reinterpret_cast<ImporterLocalRec8H>(privateData);
	
	// If file has not yet been closed
	if(SDKfileRef && *SDKfileRef != imInvalidHandleValue)
	{
		SDKQuietFile(stdParms, SDKfileRef, privateData);
	}

	// Remove the privateData handle.
	// CLEANUP - Destroy the handle we created to avoid memory leaks
	if(ldataH && *ldataH && (*ldataH)->BasicSuite)
	{
		stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(ldataH));

		ImporterLocalRec8Ptr localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *ldataH );;

		localRecP->BasicSuite->ReleaseSuite(kPrSDKPPixCreatorSuite, kPrSDKPPixCreatorSuiteVersion);
		localRecP->BasicSuite->ReleaseSuite(kPrSDKPPixCacheSuite, PrCacheVersion);
		localRecP->BasicSuite->ReleaseSuite(kPrSDKPPixSuite, kPrSDKPPixSuiteVersion);
		localRecP->BasicSuite->ReleaseSuite(kPrSDKPPix2Suite, kPrSDKPPix2SuiteVersion);
		localRecP->BasicSuite->ReleaseSuite(kPrSDKTimeSuite, kPrSDKTimeSuiteVersion);
		localRecP->BasicSuite->ReleaseSuite(kPrSDKImporterFileManagerSuite, kPrSDKImporterFileManagerSuiteVersion);

		stdParms->piSuites->memFuncs->disposeHandle(reinterpret_cast<PrMemoryHandle>(ldataH));
	}

	return malNoError;
}


static prMALError 
SDKGetIndPixelFormat(
	imStdParms			*stdParms,
	csSDK_size_t		idx,
	imIndPixelFormatRec	*SDKIndPixelFormatRec) 
{
	prMALError	result	= malNoError;
	//ImporterLocalRec8H	ldataH	= reinterpret_cast<ImporterLocalRec8H>(SDKIndPixelFormatRec->privatedata);

	switch(idx)
	{
		case 0:
			SDKIndPixelFormatRec->outPixelFormat = PrPixelFormat_YUV_420_MPEG2_FRAME_PICTURE_PLANAR_8u_709;
			break;
	
		default:
			result = imBadFormatIndex;
			break;
	}

	return result;	
}


// TODO: Support imDataRateAnalysis and we'll get a pretty graph in the Properties panel!
// Sounds like a good task for someone who wants to contribute to this open source project.


static prMALError 
SDKAnalysis(
	imStdParms		*stdParms,
	imFileRef		SDKfileRef,
	imAnalysisRec	*SDKAnalysisRec)
{
	// Is this all I'm supposed to do here?
	// The string shows up in the properties dialog.
	ImporterLocalRec8H ldataH = reinterpret_cast<ImporterLocalRec8H>(SDKAnalysisRec->privatedata);
	ImporterLocalRec8Ptr localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *ldataH );

	const char *properties_messsage = "Hi there";

	if(SDKAnalysisRec->buffersize > strlen(properties_messsage))
		strcpy(SDKAnalysisRec->buffer, properties_messsage);

	return malNoError;
}



prMALError 
SDKGetInfo8(
	imStdParms			*stdParms, 
	imFileAccessRec8	*fileAccessInfo8, 
	imFileInfoRec8		*SDKFileInfo8)
{
	prMALError					result				= malNoError;


	SDKFileInfo8->vidInfo.supportsAsyncIO			= kPrFalse;
	SDKFileInfo8->vidInfo.supportsGetSourceVideo	= kPrTrue;
	SDKFileInfo8->vidInfo.hasPulldown				= kPrFalse;
	SDKFileInfo8->hasDataRate						= kPrFalse;


	// private data
	assert(SDKFileInfo8->privatedata);
	ImporterLocalRec8H ldataH = reinterpret_cast<ImporterLocalRec8H>(SDKFileInfo8->privatedata);
	stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(ldataH));
	ImporterLocalRec8Ptr localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *ldataH );


	SDKFileInfo8->hasVideo = kPrFalse;
	SDKFileInfo8->hasAudio = kPrFalse;
	
	
	if(localRecP)
	{
		TheoraCtx ctx;
		
		th_info &ti = ctx.ti;
		
		result = SetupTheora(ctx, fileAccessInfo8->fileref);
		
		
		if(result == malNoError && ctx.have_video)
		{
			PrPixelFormat pix_format = (ti.pixel_fmt == TH_PF_420 ? PrPixelFormat_YUV_420_MPEG2_FRAME_PICTURE_PLANAR_8u_601 :
										ti.pixel_fmt == TH_PF_422 ? PrPixelFormat_YUYV_422_8u_709 :
										ti.pixel_fmt == TH_PF_444 ? PrPixelFormat_VUYA_4444_8u_709 :
										PrPixelFormat_BGRA_4444_8u);
		
			// Video information
			SDKFileInfo8->hasVideo				= kPrTrue;
			SDKFileInfo8->vidInfo.subType		= pix_format;
			SDKFileInfo8->vidInfo.imageWidth	= ti.pic_width;
			SDKFileInfo8->vidInfo.imageHeight	= ti.pic_height;
			SDKFileInfo8->vidInfo.depth			= 24;	// for RGB, no A
			SDKFileInfo8->vidInfo.fieldType		= prFieldsUnknown; // Matroska talk about DefaultDecodedFieldDuration but...
			SDKFileInfo8->vidInfo.isStill		= kPrFalse;
			SDKFileInfo8->vidInfo.noDuration	= imNoDurationFalse;
			SDKFileInfo8->vidDuration			= 10 * ti.fps_denominator;
			SDKFileInfo8->vidScale				= ti.fps_numerator;
			SDKFileInfo8->vidSampleSize			= ti.fps_denominator;

			SDKFileInfo8->vidInfo.alphaType		= alphaNone;

			SDKFileInfo8->vidInfo.pixelAspectNum = ti.aspect_numerator;
			SDKFileInfo8->vidInfo.pixelAspectDen = ti.aspect_denominator;
			
			// store some values we want to get without going to the file
			localRecP->width = SDKFileInfo8->vidInfo.imageWidth;
			localRecP->height = SDKFileInfo8->vidInfo.imageHeight;

			localRecP->frameRateNum = SDKFileInfo8->vidScale;
			localRecP->frameRateDen = SDKFileInfo8->vidSampleSize;
		
		}
		
		if(false) // audio
		{
		/*				// Audio information
						SDKFileInfo8->hasAudio				= kPrTrue;
						SDKFileInfo8->audInfo.numChannels	= pAudioTrack->GetChannels();
						SDKFileInfo8->audInfo.sampleRate	= pAudioTrack->GetSamplingRate();
						SDKFileInfo8->audInfo.sampleType	= bitDepth == 8 ? kPrAudioSampleType_8BitInt :
																bitDepth == 16 ? kPrAudioSampleType_16BitInt :
																bitDepth == 24 ? kPrAudioSampleType_24BitInt :
																bitDepth == 32 ? kPrAudioSampleType_32BitFloat :
																bitDepth == 64 ? kPrAudioSampleType_64BitFloat :
																kPrAudioSampleType_Compressed;
																
						SDKFileInfo8->audDuration			= (uint64_t)SDKFileInfo8->audInfo.sampleRate * duration / 1000000000UL;
						
						
						localRecP->audioSampleRate			= SDKFileInfo8->audInfo.sampleRate;
						localRecP->numChannels				= SDKFileInfo8->audInfo.numChannels;
*/		}

		TheoraSetdown(ctx);
	}
		
	stdParms->piSuites->memFuncs->unlockHandle(reinterpret_cast<char**>(ldataH));

	return result;
}


static prMALError 
SDKPreferredFrameSize(
	imStdParms					*stdparms, 
	imPreferredFrameSizeRec		*preferredFrameSizeRec)
{
	prMALError			result	= imIterateFrameSizes;
	ImporterLocalRec8H	ldataH	= reinterpret_cast<ImporterLocalRec8H>(preferredFrameSizeRec->inPrivateData);

	stdparms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(ldataH));

	ImporterLocalRec8Ptr localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *ldataH );


	// TODO: Make sure it really isn't possible to decode a smaller frame
	bool can_shrink = false;

	if(preferredFrameSizeRec->inIndex == 0)
	{
		preferredFrameSizeRec->outWidth = localRecP->width;
		preferredFrameSizeRec->outHeight = localRecP->height;
	}
	else
	{
		// we store width and height in private data so we can produce it here
		const int divisor = pow(2.0, preferredFrameSizeRec->inIndex);
		
		if(can_shrink &&
			preferredFrameSizeRec->inIndex < 4 &&
			localRecP->width % divisor == 0 &&
			localRecP->height % divisor == 0 )
		{
			preferredFrameSizeRec->outWidth = localRecP->width / divisor;
			preferredFrameSizeRec->outHeight = localRecP->height / divisor;
		}
		else
			result = malNoError;
	}


	stdparms->piSuites->memFuncs->unlockHandle(reinterpret_cast<char**>(ldataH));

	return result;
}


static void stripe_decoded_422(th_ycbcr_buffer _dst, th_ycbcr_buffer _src, int _fragy0, int _fragy_end)
{
	for(int pli=0; pli < 3; pli++)
	{
		int yshift = (pli != 0 && !(true));
		int y_end = (_fragy_end << 3 - yshift);

		// An implemention intending to display this data would need to check the
		// crop rectangle before proceeding.
		for(int y = (_fragy0 << 3 - yshift); y < y_end; y++)
		{
			memcpy(_dst[pli].data + y * _dst[pli].stride, _src[pli].data + y * _src[pli].stride, _src[pli].width);
		}
	}
}

static void stripe_decoded_420(th_ycbcr_buffer _dst, th_ycbcr_buffer _src, int _fragy0, int _fragy_end)
{
	for(int pli=0; pli < 3; pli++)
	{
		int yshift = (pli != 0 && !(false));
		int y_end = (_fragy_end << 3 - yshift);

		// An implemention intending to display this data would need to check the
		// crop rectangle before proceeding.
		for(int y = (_fragy0 << 3 - yshift); y < y_end; y++)
		{
			memcpy(_dst[pli].data + y * _dst[pli].stride, _src[pli].data + y * _src[pli].stride, _src[pli].width);
		}
	}
}


static prMALError 
SDKGetSourceVideo(
	imStdParms			*stdParms, 
	imFileRef			fileRef, 
	imSourceVideoRec	*sourceVideoRec)
{
	prMALError		result		= malNoError;
	csSDK_int32		theFrame	= 0;
	imFrameFormat	*frameFormat;

	// privateData
	ImporterLocalRec8H ldataH = reinterpret_cast<ImporterLocalRec8H>(sourceVideoRec->inPrivateData);
	stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(ldataH));
	ImporterLocalRec8Ptr localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *ldataH );


	PrTime ticksPerSecond = 0;
	localRecP->TimeSuite->GetTicksPerSecond(&ticksPerSecond);
	

	if(localRecP->frameRateDen == 0) // i.e. still frame
	{
		theFrame = 0;
	}
	else
	{
		PrTime ticksPerFrame = (ticksPerSecond * (PrTime)localRecP->frameRateDen) / (PrTime)localRecP->frameRateNum;
		theFrame = sourceVideoRec->inFrameTime / ticksPerFrame;
	}

	// Check to see if frame is already in cache
	result = localRecP->PPixCacheSuite->GetFrameFromCache(	localRecP->importerID,
															0,
															theFrame,
															1,
															sourceVideoRec->inFrameFormats,
															sourceVideoRec->outFrame,
															NULL,
															NULL);

	// If frame is not in the cache, read the frame and put it in the cache; otherwise, we're done
	if(result != suiteError_NoError)
	{
		// ok, we'll read the file - clear error
		result = malNoError;
		
		// get the Premiere buffer
		frameFormat = &sourceVideoRec->inFrameFormats[0];
		prRect theRect;
		if(frameFormat->inFrameWidth == 0 && frameFormat->inFrameHeight == 0)
		{
			frameFormat->inFrameWidth = localRecP->width;
			frameFormat->inFrameHeight = localRecP->height;
		}
		
		// Windows and MacOS have different definitions of Rects, so use the cross-platform prSetRect
		prSetRect(&theRect, 0, 0, frameFormat->inFrameWidth, frameFormat->inFrameHeight);
		

		
		if(true)
		{
			TheoraCtx ctx;
			
			ogg_sync_state    &oy = ctx.oy;
			ogg_page          &og = ctx.og;
			ogg_packet        &op = ctx.op;
			ogg_stream_state  &to = ctx.to;
			th_info           &ti = ctx.ti;
			th_comment        &tc = ctx.tc;
			th_setup_info     *&ts = ctx.ts;
			th_dec_ctx        *&td = ctx.td;
			
			result = SetupTheora(ctx, fileRef);
			
			if(result == malNoError)
			{
				assert(ctx.have_video);
				
				td = th_decode_alloc(&ti, ts);
				
				
				// this is their open_video() function
				th_ycbcr_buffer ycbcr;
				
				const bool chroma_each_row = (ti.pixel_fmt == TH_PF_444); // (ti.pixel_fmt & 1)
				const bool chroma_each_line = (ti.pixel_fmt == TH_PF_422 || ti.pixel_fmt == TH_PF_444); // (ti.pixel_fmt & 2)
				
				for(int pli=0; pli < 3; pli++)
				{
					int xshift = (pli !=0 && !chroma_each_row);
					int yshift = (pli !=0 && !chroma_each_line);
					
					ycbcr[pli].data = (unsigned char *)malloc((ti.frame_width >> xshift) * (ti.frame_height >> yshift) * sizeof(char));
					
					ycbcr[pli].stride = ti.frame_width >> xshift;
					ycbcr[pli].width = ti.frame_width >> xshift;
					ycbcr[pli].height= ti.frame_height >> yshift;
				}
				
				
				th_stripe_callback cb;
				
				cb.ctx = ycbcr;
				
				if(chroma_each_line)
					cb.stripe_decoded = (th_stripe_decoded_func)stripe_decoded_422;
				else
					cb.stripe_decoded = (th_stripe_decoded_func)stripe_decoded_420;
					
				
				th_decode_ctl(td, TH_DECCTL_SET_STRIPE_CB, &cb, sizeof(cb));
				
				// end of open_video()
				

				while(ogg_sync_pageout(&oy, &og) > 0)
					ogg_stream_pagein(&to, &og);
				
				
				while(1)
				{
					int frames = 0;
					int videobuf_ready = 0;
					ogg_int64_t videobuf_granulepos = -1;
					double videobuf_time = 0;
					
					while(!videobuf_ready && ogg_stream_packetout(&to,&op) > 0)
					{
						// theora is one in, one out...
						if( th_decode_packetin(td, &op, &videobuf_granulepos) >= 0 )
						{
							videobuf_time = th_granule_time(td, videobuf_granulepos);
							videobuf_ready = 1;
							frames++;
						}
					}
					
					if(!videobuf_ready)
					{
						// no data yet for somebody.  Grab another page
						char *buffer = ogg_sync_buffer(&oy, 4096);
						
						int bytes = ogg_read_func(buffer, 1, 4096, fileRef);
						
						if(bytes == 0)
							break;
						
						ogg_sync_wrote(&oy, bytes);
						
						while( ogg_sync_pageout(&oy,&og) > 0 )
							ogg_stream_pagein(&to, &og);
					}
					else
					{
						// their video_write()
						int x0 = 0;
						int y0 = 0;
						
						int xend = ti.frame_width;
						int yend = ti.frame_height;
					
						int hdec = 0;
						int vdec = 0;
						
						for(int pli=0; pli < 3; pli++)
						{
							for(int i = (y0 >> vdec); i < (yend + vdec >> vdec); i++)
							{
								// you've got uncompressee video here
								
								//fwrite(ycbcr[pli].data  +ycbcr[pli].stride * i + (x0>>hdec), 1,
								//			(xend + hdec >> hdec) - (x0 >> hdec), outfile);
							}

							hdec = !(ti.pixel_fmt&1);
							vdec = !(ti.pixel_fmt&2);
						}
					}
				}
				
				for(int pli=0; pli < 3; pli++)
				{
					free(ycbcr[pli].data);
				}
			}
			
			TheoraSetdown(ctx);
		}
	}


	stdParms->piSuites->memFuncs->unlockHandle(reinterpret_cast<char**>(ldataH));

	return result;
}


/*												
template <typename T>
static inline T minimum(T one, T two)
{
	return (one < two ? one : two);
}
*/

static prMALError 
SDKImportAudio7(
	imStdParms			*stdParms, 
	imFileRef			SDKfileRef, 
	imImportAudioRec7	*audioRec7)
{
	prMALError		result		= malNoError;

	// privateData
	ImporterLocalRec8H ldataH = reinterpret_cast<ImporterLocalRec8H>(audioRec7->privateData);
	stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(ldataH));
	ImporterLocalRec8Ptr localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *ldataH );


	
	if(true)
	{
		assert(audioRec7->position >= 0); // Do they really want contiguous samples?
	}
	
					
	stdParms->piSuites->memFuncs->unlockHandle(reinterpret_cast<char**>(ldataH));
	
	assert(result == malNoError);
	
	return result;
}


PREMPLUGENTRY DllExport xImportEntry (
	csSDK_int32		selector, 
	imStdParms		*stdParms, 
	void			*param1, 
	void			*param2)
{
	prMALError	result				= imUnsupported;

	try{

	switch (selector)
	{
		case imInit:
			result =	SDKInit(stdParms, 
								reinterpret_cast<imImportInfoRec*>(param1));
			break;

		case imGetInfo8:
			result =	SDKGetInfo8(stdParms, 
									reinterpret_cast<imFileAccessRec8*>(param1), 
									reinterpret_cast<imFileInfoRec8*>(param2));
			break;

		case imOpenFile8:
			result =	SDKOpenFile8(	stdParms, 
										reinterpret_cast<imFileRef*>(param1), 
										reinterpret_cast<imFileOpenRec8*>(param2));
			break;
		
		case imQuietFile:
			result =	SDKQuietFile(	stdParms, 
										reinterpret_cast<imFileRef*>(param1), 
										param2); 
			break;

		case imCloseFile:
			result =	SDKCloseFile(	stdParms, 
										reinterpret_cast<imFileRef*>(param1), 
										param2);
			break;

		case imAnalysis:
			result =	SDKAnalysis(	stdParms,
										reinterpret_cast<imFileRef>(param1),
										reinterpret_cast<imAnalysisRec*>(param2));
			break;

		case imGetIndFormat:
			result =	SDKGetIndFormat(stdParms, 
										reinterpret_cast<csSDK_size_t>(param1),
										reinterpret_cast<imIndFormatRec*>(param2));
			break;

		case imGetIndPixelFormat:
			result = SDKGetIndPixelFormat(	stdParms,
											reinterpret_cast<csSDK_size_t>(param1),
											reinterpret_cast<imIndPixelFormatRec*>(param2));
			break;

		// Importers that support the Premiere Pro 2.0 API must return malSupports8 for this selector
		case imGetSupports8:
			result = malSupports8;
			break;

		case imGetPreferredFrameSize:
			result =	SDKPreferredFrameSize(	stdParms,
												reinterpret_cast<imPreferredFrameSizeRec*>(param1));
			break;

		case imGetSourceVideo:
			result =	SDKGetSourceVideo(	stdParms,
											reinterpret_cast<imFileRef>(param1),
											reinterpret_cast<imSourceVideoRec*>(param2));
			break;
			
		case imImportAudio7:
			result =	SDKImportAudio7(	stdParms,
											reinterpret_cast<imFileRef>(param1),
											reinterpret_cast<imImportAudioRec7*>(param2));
			break;

		case imCreateAsyncImporter:
			result =	imUnsupported;
			break;
	}
	
	}catch(...) { result = imOtherErr; }

	return result;
}

