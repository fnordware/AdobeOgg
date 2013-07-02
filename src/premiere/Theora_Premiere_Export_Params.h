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


#ifndef THEORA_PREMIERE_EXPORT_PARAMS_H
#define THEORA_PREMIERE_EXPORT_PARAMS_H


#include "Theora_Premiere_Export.h"


typedef enum {
	THEORA_CODEC_VP8 = 0,
	THEORA_CODEC_VP9
} Theora_Video_Codec;

typedef enum {
	THEORA_METHOD_QUALITY = 0,
	THEORA_METHOD_BITRATE,
	THEORA_METHOD_VBR
} Theora_Video_Method;

typedef enum {
	THEORA_ENCODING_REALTIME = 0,
	THEORA_ENCODING_GOOD,
	THEORA_ENCODING_BEST
} Theora_Video_Encoding;



#define TheoraPluginVersion		"TheoraPluginVersion"

#define ADBEVideoMatchSource	"ADBEVideoMatchSource"

#define TheoraVideoMethod		"TheoraVideoMethod"
#define TheoraVideoQuality		"TheoraVideoQuality"
#define TheoraVideoBitrate		"TheoraVideoBitrate"
#define TheoraVideoEncoding		"TheoraVideoEncoding"

//#define TheoraCustomGroup		"TheoraCustomGroup"
//#define TheoraCustomArgs		"TheoraCustomArgs"


typedef enum {
	OGG_QUALITY = 0,
	OGG_BITRATE
} Ogg_Method;

#define TheoraAudioMethod	"TheoraAudioMethod"
#define TheoraAudioQuality	"TheoraAudioQuality"
#define TheoraAudioBitrate	"TheoraAudioBitrate"




prMALError
exSDKQueryOutputSettings(
	exportStdParms				*stdParmsP,
	exQueryOutputSettingsRec	*outputSettingsP);

prMALError
exSDKGenerateDefaultParams(
	exportStdParms				*stdParms, 
	exGenerateDefaultParamRec	*generateDefaultParamRec);

prMALError
exSDKPostProcessParams(
	exportStdParms			*stdParmsP, 
	exPostProcessParamsRec	*postProcessParamsRecP);

prMALError
exSDKGetParamSummary(
	exportStdParms			*stdParmsP, 
	exParamSummaryRec		*summaryRecP);

prMALError
exSDKValidateParamChanged (
	exportStdParms		*stdParmsP, 
	exParamChangedRec	*validateParamChangedRecP);
	

//bool ConfigureEncoderPre(vpx_codec_enc_cfg_t &config, const char *txt);

//bool ConfigureEncoderPost(vpx_codec_ctx_t *encoder, const char *txt);


#endif // THEORA_PREMIERE_EXPORT_PARAMS_H
