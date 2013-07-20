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

#include "theoraplay.h"

}


#include <assert.h>
#include <math.h>

#include <string>

#ifdef PRMAC_ENV
	#include <mach/mach.h>
#endif


static long theora_read(THEORAPLAY_Io *io, void *buf, long buflen)
{
	imFileRef fp = static_cast<imFileRef>(io->userdata);
	
#ifdef PRWIN_ENV	
	DWORD count = buflen, out;
	
	BOOL result = ReadFile(fp, (LPVOID)buf, count, &out, NULL);

	return out;
#else
	ByteCount count = buflen, out = 0;
	
	OSErr result = FSReadFork(CAST_REFNUM(fp), fsAtMark, 0, count, buf, &out);

	return out;
#endif
}

static void theora_close(THEORAPLAY_Io *io)
{
	// Not gonna close, just move to the file beginning
	imFileRef fp = static_cast<imFileRef>(io->userdata);
	
#ifdef PRWIN_ENV
	LARGE_INTEGER lpos;

	lpos.QuadPart = 0;

#if _MSC_VER < 1300
	DWORD pos = SetFilePointer(fp, lpos.u.LowPart, &lpos.u.HighPart, FILE_BEGIN);

	BOOL result = (pos != 0xFFFFFFFF || NO_ERROR == GetLastError());
#else
	BOOL result = SetFilePointerEx(fp, lpos, NULL, FILE_BEGIN);
#endif
#else
	OSErr result = FSSetForkPosition(CAST_REFNUM(fp), fsFromStart, 0);
#endif
}




#if IMPORTMOD_VERSION <= IMPORTMOD_VERSION_9
typedef PrSDKPPixCacheSuite2 PrCacheSuite;
#define PrCacheVersion	kPrSDKPPixCacheSuiteVersion2
#else
typedef PrSDKPPixCacheSuite PrCacheSuite;
#define PrCacheVersion	kPrSDKPPixCacheSuiteVersion
#endif


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


static void
float_to_rational_fps(float fps, unsigned int *fps_num, unsigned int *fps_den)
{
	// known frame rates
	static const int frameRateNumDens[10][2] = {{10, 1}, {15, 1}, {24000, 1001},
												{24, 1}, {25, 1}, {30000, 1001},
												{30, 1}, {50, 1}, {60000, 1001},
												{60, 1}};

	int match_index = -1;
	double match_episilon = 999;

	for(int i=0; i < 10; i++)
	{
		double rate = (double)frameRateNumDens[i][0] / (double)frameRateNumDens[i][1];
		double episilon = fabs(fps - rate);

		if(episilon < match_episilon)
		{
			match_index = i;
			match_episilon = episilon;
		}
	}

	if(match_index >=0 && match_episilon < 0.01)
	{
		*fps_num = frameRateNumDens[match_index][0];
		*fps_den = frameRateNumDens[match_index][1];
	}
	else
	{
		*fps_num = (fps * 1000.0) + 0.5;
		*fps_den = 1000;
	}
}

#define MAX_FRAMES 9999

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
		THEORAPLAY_Io io = { theora_read, theora_close, fileAccessInfo8->fileref };
		
		THEORAPLAY_Decoder *decoder = THEORAPLAY_startDecode(&io, MAX_FRAMES, THEORAPLAY_VIDFMT_IYUV);
		
		if(decoder)
		{
			int frames = 0;
            PrTime samples = 0;
			
            // This is really bad.  I have to decode the entire movie in order to find
            // out how many frames there are.
            while(THEORAPLAY_isDecoding(decoder))
            {
                const THEORAPLAY_VideoFrame *video = THEORAPLAY_getVideo(decoder);
                
                if(video)
                {
                    if(frames == 0)
                    {
                        // Get video information
                        SDKFileInfo8->hasVideo				= kPrTrue;
                        SDKFileInfo8->vidInfo.subType		= PrPixelFormat_YUV_420_MPEG2_FRAME_PICTURE_PLANAR_8u_601;
                        SDKFileInfo8->vidInfo.imageWidth	= video->width;
                        SDKFileInfo8->vidInfo.imageHeight	= video->height;
                        SDKFileInfo8->vidInfo.depth			= 24;	// for RGB, no A
                        SDKFileInfo8->vidInfo.fieldType		= prFieldsUnknown; // Matroska talk about DefaultDecodedFieldDuration but...
                        SDKFileInfo8->vidInfo.isStill		= kPrFalse;
                        SDKFileInfo8->vidInfo.noDuration	= imNoDurationFalse;
                        
                        unsigned int fps_num, fps_den;
                        float_to_rational_fps(video->fps, &fps_num, &fps_den);
                        
                        SDKFileInfo8->vidScale				= fps_num;
                        SDKFileInfo8->vidSampleSize			= fps_den;

                        SDKFileInfo8->vidInfo.alphaType		= alphaNone;
                        
                        // pixel aspect ratio should be available, but not exposed in TheoraPlay
                    }
                    
                    THEORAPLAY_freeVideo(video);
                    
                    frames++;
                }
                
                
                const THEORAPLAY_AudioPacket *audio = THEORAPLAY_getAudio(decoder);
                
                if(audio)
                {
                    if(samples == 0)
                    {
                        SDKFileInfo8->hasAudio				= kPrTrue;
                        SDKFileInfo8->audInfo.numChannels	= audio->channels;
                        SDKFileInfo8->audInfo.sampleRate	= audio->freq;
                        SDKFileInfo8->audInfo.sampleType	= kPrAudioSampleType_Compressed;
                    }
                    
                    samples += audio->frames;
                    
                    THEORAPLAY_freeAudio(audio);
                }
                
                if (!video && !audio)
				{
				#ifdef PRWIN_ENV
					Sleep(10);
				#else
                    usleep(10000);
				#endif
				}
            }
			
			
			if( THEORAPLAY_decodingError(decoder) )
			{
				result = imBadFile;
			}
			else if(frames > 0)
			{
				SDKFileInfo8->vidDuration = frames * SDKFileInfo8->vidSampleSize;
                SDKFileInfo8->audDuration = samples;
							

				// store some values we want to get without going to the file
				localRecP->width = SDKFileInfo8->vidInfo.imageWidth;
				localRecP->height = SDKFileInfo8->vidInfo.imageHeight;

				localRecP->frameRateNum = SDKFileInfo8->vidScale;
				localRecP->frameRateDen = SDKFileInfo8->vidSampleSize;
                
                localRecP->audioSampleRate			= SDKFileInfo8->audInfo.sampleRate;
                localRecP->numChannels				= SDKFileInfo8->audInfo.numChannels;
			}
			else
				result = imBadFile;
			
			THEORAPLAY_stopDecode(decoder);
		}
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


// Set to the half the size of the number of frames you think Premiere
// will actually keep in its cache.
#define FRAME_REACH 60

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
		

		THEORAPLAY_Io io = { theora_read, theora_close, fileRef };
		
		THEORAPLAY_Decoder *decoder = THEORAPLAY_startDecode(&io, MAX_FRAMES, THEORAPLAY_VIDFMT_IYUV);
		
		if(decoder)
		{
			const int start_frame = theFrame - FRAME_REACH;
			const int end_frame = theFrame + FRAME_REACH;
		
			int frame = 0;
			
			
			while(THEORAPLAY_isDecoding(decoder) && frame <= end_frame)
			{
				const THEORAPLAY_VideoFrame *video = THEORAPLAY_getVideo(decoder);
				
				if(video)
				{
					// This is really bad.  I have to decode everything up to the requested frame.
					// In order to make it less wasteful, I cache frames with Premiere.  But I
					// don't think Premiere will keep all my frames in cache, so I'm guessing
					// at the size with FRAME_REACH.  And at least I don't read all the way to the
					// end every time to cache frames that Premiere will dump.  Really, my advice
					// at this point is NOT to load long Theora movies into Premiere.  Firefox
					// seems to be able to scan Theora pretty well, so I'm confident it can be
					// done.  But true to its name, TheoraPlay only goes from the beginning,
					// and so does the Theora sample.
					if(frame >= start_frame)
					{
						PPixHand ppix;
						localRecP->PPixCreatorSuite->CreatePPix(&ppix, PrPPixBufferAccess_ReadWrite, frameFormat->inPixelFormat, &theRect);
						
						if(frameFormat->inPixelFormat == PrPixelFormat_YUV_420_MPEG2_FRAME_PICTURE_PLANAR_8u_709)
						{
							char *Y_PixelAddress, *U_PixelAddress, *V_PixelAddress;
							csSDK_uint32 Y_RowBytes, U_RowBytes, V_RowBytes;
							
							localRecP->PPix2Suite->GetYUV420PlanarBuffers(ppix, PrPPixBufferAccess_ReadWrite,
																			&Y_PixelAddress, &Y_RowBytes,
																			&U_PixelAddress, &U_RowBytes,
																			&V_PixelAddress, &V_RowBytes);
																		
							assert(frameFormat->inFrameHeight == video->height);
							assert(frameFormat->inFrameWidth == video->width);

							const int w = video->width;
							const int h = video->height;
							const unsigned char *y = (const unsigned char *)video->pixels;
							const unsigned char *u = y + (w * h);
							const unsigned char *v = u + (((w + 1) / 2) * ((h + 1) / 2));

							for(int i = 0; i < h; i++)
							{
								unsigned char *prY = (unsigned char *)Y_PixelAddress + (Y_RowBytes * i);
								
								memcpy(prY, y, w * sizeof(unsigned char));
								
								y += w;
							}
							
							for(int i = 0; i < ((h + 1) / 2); i++)
							{
								unsigned char *prU = (unsigned char *)U_PixelAddress + (U_RowBytes * i);
								unsigned char *prV = (unsigned char *)V_PixelAddress + (V_RowBytes * i);
								
								memcpy(prU, u, ((w + 1) / 2) * sizeof(unsigned char));
								memcpy(prV, v, ((w + 1) / 2) * sizeof(unsigned char));
								
								u += ((w + 1) / 2);
								v += ((w + 1) / 2);
							}
							
							localRecP->PPixCacheSuite->AddFrameToCache(	localRecP->importerID,
																		0,
																		ppix,
																		frame,
																		NULL,
																		NULL);
							
							if(frame == theFrame)
							{
								*sourceVideoRec->outFrame = ppix;
							}
							else
							{
								// Premiere copied the frame to its cache, so we dispose ours.
								// Very obvious memory leak if we don't.
								localRecP->PPixSuite->Dispose(ppix);
							}
						}
						else
							assert(false); // looks like Premiere is happy to always give me this kind of buffer
					}
					
					THEORAPLAY_freeVideo(video);
					
					frame++;
				}
				
				
				const THEORAPLAY_AudioPacket *audio = THEORAPLAY_getAudio(decoder);
				
				if(audio)
				{
					THEORAPLAY_freeAudio(audio);
				}
				
				if(!video && !audio)
				{
				#ifdef PRWIN_ENV
					Sleep(10);
				#else
                    usleep(10000);
				#endif
				}
			}
			
			
			if( THEORAPLAY_decodingError(decoder) )
			{
				result = imBadFile;
			}
			
			THEORAPLAY_stopDecode(decoder);
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
		
		THEORAPLAY_Io io = { theora_read, theora_close, SDKfileRef };
		
		THEORAPLAY_Decoder *decoder = THEORAPLAY_startDecode(&io, MAX_FRAMES, THEORAPLAY_VIDFMT_IYUV);
		
		if(decoder)
		{
            csSDK_uint32 samples_read = 0;
            
            PrTime audio_pos = 0; // this is the position of this sound packet in the overall movie
            
			while(THEORAPLAY_isDecoding(decoder) && (samples_read < audioRec7->size))
			{
				const THEORAPLAY_VideoFrame *video = THEORAPLAY_getVideo(decoder);
				
				if(video)
				{
					THEORAPLAY_freeVideo(video);
				}
				
				
				const THEORAPLAY_AudioPacket *audio = THEORAPLAY_getAudio(decoder);
				
				if(audio)
				{
					// This is really bad.  I have to decode everything up to the requested frame.
                    // Fortunately Premiere doesn't ask for audio just one frame at a time, but it
                    // also doesn't let me tell it how much to store, because then I'd give it the
                    // whole movie's worth if I could.
                    
                    // these are the positions to copy relative to this this packet, which are likely out of range
                    int start_sample = audioRec7->position - audio_pos;
                    int end_sample = audioRec7->position + audioRec7->size - audio_pos;
                    
                    if(start_sample < audio->frames && end_sample >= 0)
                    {
                        if(start_sample < 0)
                            start_sample = 0;
                        
                        if(end_sample > audio->frames - 1)
                            end_sample = audio->frames - 1;
                    
                        for(int i=start_sample; i <= end_sample; i++)
                        {
                            for(int c=0; c < audio->channels; c++)
                            {
                                audioRec7->buffer[c][i + audio_pos - audioRec7->position] = audio->samples[(i * audio->channels) + c];
                            }
                            
                            samples_read++;
                        }
                    }
                       
                    audio_pos += audio->frames;
                       
                    
					THEORAPLAY_freeAudio(audio);
				}
				
				if(!video && !audio)
				{
				#ifdef PRWIN_ENV
					Sleep(10);
				#else
                    usleep(10000);
				#endif
				}
			}
			
			
			if( THEORAPLAY_decodingError(decoder) )
			{
				result = imBadFile;
			}
			
			THEORAPLAY_stopDecode(decoder);
		}
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

