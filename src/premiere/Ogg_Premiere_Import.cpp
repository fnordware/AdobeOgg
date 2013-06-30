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
// Ogg plug-in for Premiere
//
// by Brendan Bolles <brendan@fnordware.com>
//
// ------------------------------------------------------------------------


#include "Ogg_Premiere_Import.h"


extern "C" {

#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

}


#include <assert.h>
#include <math.h>

#include <sstream>



#define OV_OK	0

static size_t ogg_read_func(void *ptr, size_t size, size_t nmemb, void *datasource)
{
	imFileRef fp = static_cast<imFileRef>(datasource);
	

#ifdef PRWIN_ENV	
	DWORD count = size, out;
	
	BOOL result = ReadFile(fp, (LPVOID)ptr, count, &out, NULL);

	return out;
#else
	ByteCount count = size, out = 0;
	
	OSErr result = FSReadFork(CAST_REFNUM(fp), fsAtMark, 0, count, ptr, &out);

	return out;
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

static ov_callbacks g_ov_callbacks = { ogg_read_func, ogg_seek_func, NULL, ogg_tell_func };


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
	int						numChannels;
	float					audioSampleRate;
	
	OggVorbis_File			*vf;
	
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
	char formatname[255]	= "Ogg Vorbis";
	char shortname[32]		= "Ogg Vorbis";
	char platformXten[256]	= "ogg\0\0";

	switch(index)
	{
		//	Add a case for each filetype.
		
		case 0:		
			SDKIndFormatRec->filetype			= 'OggV';

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
		
		localRecP->vf = NULL;
		
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
		localRecP->vf = new OggVorbis_File;
	
		OggVorbis_File &vf = *localRecP->vf;
		
		int ogg_err = ov_open_callbacks(static_cast<void *>(*SDKfileRef), &vf, NULL, 0, g_ov_callbacks);
		
		if(ogg_err == OV_OK)
		{
			if( ov_streams(&vf) == 0 )
			{
				result = imFileHasNoImportableStreams;
				
				ov_clear(&vf);
			}
			else if( !ov_seekable(&vf) )
			{
				result = imBadFile;
			}
		}
		else
			result = imBadHeader;
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
	// "Quet File" really means close the file handle, but we're still
	// using it and might open it again, so hold on to any stored data
	// structures you don't want to re-create.

	// If file has not yet been closed
	if(SDKfileRef && *SDKfileRef != imInvalidHandleValue)
	{
		ImporterLocalRec8H ldataH	= reinterpret_cast<ImporterLocalRec8H>(privateData);

		stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(ldataH));

		ImporterLocalRec8Ptr localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *ldataH );


		int clear_err = ov_clear(localRecP->vf);
		
		assert(clear_err == OV_OK);
		
		delete localRecP->vf;
		
		localRecP->vf = NULL;
		

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
	if(ldataH && *ldataH)
	{
		stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(ldataH));

		ImporterLocalRec8Ptr localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *ldataH );;

		stdParms->piSuites->memFuncs->disposeHandle(reinterpret_cast<PrMemoryHandle>(ldataH));
	}

	return malNoError;
}



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

	std::stringstream ss;
	
	// actually, this is already reported, what do I have to add?
	ss << localRecP->numChannels << " channels, " << localRecP->audioSampleRate << " Hz";
	
	if(SDKAnalysisRec->buffersize > ss.str().size())
		strcpy(SDKAnalysisRec->buffer, ss.str().c_str());

	return malNoError;
}




prMALError 
SDKGetInfo8(
	imStdParms			*stdParms, 
	imFileAccessRec8	*fileAccessInfo8, 
	imFileInfoRec8		*SDKFileInfo8)
{
	prMALError					result				= malNoError;


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
		OggVorbis_File &vf = *localRecP->vf;
	
		vorbis_info *info = ov_info(&vf, 0);
	
		// Audio information
		SDKFileInfo8->hasAudio				= kPrTrue;
		SDKFileInfo8->audInfo.numChannels	= info->channels;
		SDKFileInfo8->audInfo.sampleRate	= info->rate;
		SDKFileInfo8->audInfo.sampleType	= kPrAudioSampleType_Compressed;
												
		SDKFileInfo8->audDuration			= ov_pcm_total(&vf, 0);
		
		
		localRecP->audioSampleRate			= SDKFileInfo8->audInfo.sampleRate;
		localRecP->numChannels				= SDKFileInfo8->audInfo.numChannels;
	}
		
	stdParms->piSuites->memFuncs->unlockHandle(reinterpret_cast<char**>(ldataH));

	return result;
}




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


	if(localRecP)
	{
		assert(audioRec7->position >= 0); // Do they really want contiguous samples?
		
		
		OggVorbis_File &vf = *localRecP->vf;
		
		int seek_err = OV_OK;
		
		if(audioRec7->position >= 0) // otherwise contiguous, but we should be good at the current position
			seek_err = ov_pcm_seek(&vf, audioRec7->position);
			
		
		if(seek_err == OV_OK)
		{
			int num = 0;
			float **pcm_channels;
			
			long samples_needed = audioRec7->size;
			long pos = 0;
			
			while(samples_needed > 0 && result == malNoError)
			{
				int samples = samples_needed;
				
				if(samples > 1024)
					samples = 1024; // maximum size this call can read at once
			
				long samples_read = ov_read_float(&vf, &pcm_channels, samples, &num);
				
				if(samples_read >= 0)
				{
					if(samples_read == 0)
					{
						// EOF
						// Premiere will keep asking me for more and more samples,
						// even beyond what I told it I had in SDKFileInfo8->audDuration.
						// Just stop and everything will be fine.
						break;
					}
					
					for(int i=0; i < localRecP->numChannels; i++)
					{
						memcpy(&audioRec7->buffer[i][pos], pcm_channels[i], samples_read * sizeof(float));
					}
					
					samples_needed -= samples_read;
					pos += samples_read;
				}
				else
					result = imDecompressionError;
			}
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

		// Importers that support the Premiere Pro 2.0 API must return malSupports8 for this selector
		case imGetSupports8:
			result = malSupports8;
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

