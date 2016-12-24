/**
   Based on code
   Copyright (C) 2007-2009 STMicroelectronics
   Copyright (C) 2007-2009 Nokia Corporation and/or its subsidiary(-ies).
   under the LGPL
*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>

#include <OMX_Core.h>
#include <OMX_Component.h>
#include <OMX_Types.h>
#include <OMX_Audio.h>

#ifdef RASPBERRY_PI
#include <bcm_host.h>
#endif

OMX_ERRORTYPE err;
OMX_HANDLETYPE handle;
OMX_VERSIONTYPE specVersion, compVersion;

OMX_CALLBACKTYPE callbacks;

#define indent {int n = 0; while (n++ < indentLevel*2) putchar(' ');}

static void setHeader(OMX_PTR header, OMX_U32 size) {
    /* header->nVersion */
    OMX_VERSIONTYPE* ver = (OMX_VERSIONTYPE*)(header + sizeof(OMX_U32));
    /* header->nSize */
    *((OMX_U32*)header) = size;

    /* for 1.2
       ver->s.nVersionMajor = OMX_VERSION_MAJOR;
       ver->s.nVersionMinor = OMX_VERSION_MINOR;
       ver->s.nRevision = OMX_VERSION_REVISION;
       ver->s.nStep = OMX_VERSION_STEP;
    */
    ver->s.nVersionMajor = specVersion.s.nVersionMajor;
    ver->s.nVersionMinor = specVersion.s.nVersionMinor;
    ver->s.nRevision = specVersion.s.nRevision;
    ver->s.nStep = specVersion.s.nStep;
}

void printState() {
    OMX_STATETYPE state;
    err = OMX_GetState(handle, &state);
    if (err != OMX_ErrorNone) {
	fprintf(stderr, "Error on getting state\n");
	exit(1);
    }
    switch (state) {
    case OMX_StateLoaded: fprintf(stderr, "StateLoaded\n"); break;
    case OMX_StateIdle: fprintf(stderr, "StateIdle\n"); break;
    case OMX_StateExecuting: fprintf(stderr, "StateExecuting\n"); break;
    case OMX_StatePause: fprintf(stderr, "StatePause\n"); break;
    case OMX_StateWaitForResources: fprintf(stderr, "StateWiat\n"); break;
    default:  fprintf(stderr, "State unknown\n"); break;
    }
}

OMX_ERRORTYPE setEncoding(int portNumber, OMX_AUDIO_CODINGTYPE encoding) {
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;

    setHeader(&sPortDef, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
    sPortDef.nPortIndex = portNumber;
    sPortDef.nPortIndex = portNumber;
    err = OMX_GetParameter(handle, OMX_IndexParamPortDefinition, &sPortDef);
    if(err != OMX_ErrorNone){
        fprintf(stderr, "Error in getting OMX_PORT_DEFINITION_TYPE parameter\n",
 0);
        exit(1);
    }

    sPortDef.format.audio.eEncoding = encoding;
    sPortDef.nBufferCountActual = sPortDef.nBufferCountMin;

    err = OMX_SetParameter(handle, OMX_IndexParamPortDefinition, &sPortDef);
    return err;
}

void getPCMInformation(int indentLevel, int portNumber) {
    /* assert: PCM is a supported mode */
    OMX_AUDIO_PARAM_PCMMODETYPE sPCMMode;

    /* set it into PCM format before asking for PCM info */
    if (setEncoding(portNumber, OMX_AUDIO_CodingPCM) != OMX_ErrorNone) {
	fprintf(stderr, "Error in setting coding to PCM\n");
	return;
    }
       
    setHeader(&sPCMMode, sizeof(OMX_AUDIO_PARAM_PCMMODETYPE));
    sPCMMode.nPortIndex = portNumber;
    err = OMX_GetParameter(handle, OMX_IndexParamAudioPcm, &sPCMMode);
    if(err != OMX_ErrorNone){
	indent printf("PCM mode unsupported\n");
    } else {
	indent printf("  PCM default sampling rate %d\n", sPCMMode.nSamplingRate);
	indent printf("  PCM default bits per sample %d\n", sPCMMode.nBitPerSample);
	indent printf("  PCM default number of channels %d\n", sPCMMode.nChannels);
    }      

    /*
    setHeader(&sAudioPortFormat, sizeof(OMX_AUDIO_PARAM_PORTFORMATTYPE));
    sAudioPortFormat.nIndex = 0;
    sAudioPortFormat.nPortIndex = portNumber;
    */

    
}
void getMP3Information(int indentLevel, int portNumber) {
    /* assert: MP3 is a supported mode */
    OMX_AUDIO_PARAM_MP3TYPE sMP3Mode;

    /* set it into MP3 format before asking for MP3 info */
    if (setEncoding(portNumber, OMX_AUDIO_CodingMP3) != OMX_ErrorNone) {
	fprintf(stderr, "Error in setting coding to MP3\n");
	return;
    }
    
    setHeader(&sMP3Mode, sizeof(OMX_AUDIO_PARAM_MP3TYPE));
    sMP3Mode.nPortIndex = portNumber;
    err = OMX_GetParameter(handle, OMX_IndexParamAudioMp3, &sMP3Mode);
    if(err != OMX_ErrorNone){
	indent printf("MP3 mode unsupported\n");
    } else {
	indent printf("  MP3 default sampling rate %d\n", sMP3Mode.nSampleRate);
	indent printf("  MP3 default bits per sample %d\n", sMP3Mode.nBitRate);
	indent printf("  MP3 default number of channels %d\n", sMP3Mode.nChannels);
    }   
}

void getSupportedImageFormats(int indentLevel, int portNumber) {
    OMX_IMAGE_PARAM_PORTFORMATTYPE sImagePortFormat;

    setHeader(&sImagePortFormat, sizeof(OMX_IMAGE_PARAM_PORTFORMATTYPE));
    sImagePortFormat.nIndex = 0;
    sImagePortFormat.nPortIndex = portNumber;

#ifdef LIM
    printf("LIM doesn't set image formats properly\n");
    return;
#endif

    indent printf("Supported image formats are:\n");
    indentLevel++;
    for(;;) {
        err = OMX_GetParameter(handle, OMX_IndexParamImagePortFormat, &sImagePortFormat);
        if (err == OMX_ErrorNoMore) {
	    indent printf("No more formats supported\n");
	    return;
        }

	/* This shouldn't occur, but does with Broadcom library */
	if (sImagePortFormat.eColorFormat == OMX_IMAGE_CodingUnused) {
	     indent printf("No coding format returned\n");
	     return;
	}

	indent printf("Image format compression format 0x%X\n", 
		      sImagePortFormat.eCompressionFormat);
	indent printf("Image format color encoding 0x%X\n", 
		      sImagePortFormat.eColorFormat);
	sImagePortFormat.nIndex++;
    }
}

void getSupportedVideoFormats(int indentLevel, int portNumber) {
    OMX_VIDEO_PARAM_PORTFORMATTYPE sVideoPortFormat;

    setHeader(&sVideoPortFormat, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
    sVideoPortFormat.nIndex = 0;
    sVideoPortFormat.nPortIndex = portNumber;

#ifdef LIM
    printf("LIM doesn't set video formats properly\n");
    return;
#endif

    indent printf("Supported video formats are:\n");
    for(;;) {
        err = OMX_GetParameter(handle, OMX_IndexParamVideoPortFormat, &sVideoPortFormat);
        if (err == OMX_ErrorNoMore) {
	    indent printf("No more formats supported\n");
	    return;
        }

	/* This shouldn't occur, but does with Broadcom library */
	if (sVideoPortFormat.eColorFormat == OMX_VIDEO_CodingUnused) {
	     indent printf("No coding format returned\n");
	     return;
	}

	indent printf("Video format encoding 0x%X\n", 
		      sVideoPortFormat.eColorFormat);
	sVideoPortFormat.nIndex++;
    }
}

void getSupportedAudioFormats(int indentLevel, int portNumber) {
    OMX_AUDIO_PARAM_PORTFORMATTYPE sAudioPortFormat;

    setHeader(&sAudioPortFormat, sizeof(OMX_AUDIO_PARAM_PORTFORMATTYPE));
    sAudioPortFormat.nIndex = 0;
    sAudioPortFormat.nPortIndex = portNumber;

#ifdef LIM
    printf("LIM doesn't set audio formats properly\n");
    return;
#endif

    indent printf("Supported audio formats are:\n");
    for(;;) {
        err = OMX_GetParameter(handle, OMX_IndexParamAudioPortFormat, &sAudioPortFormat);
        if (err == OMX_ErrorNoMore) {
	    indent printf("No more formats supported\n");
	    return;
        }

	/* This shouldn't occur, but does with Broadcom library */
	if (sAudioPortFormat.eEncoding == OMX_AUDIO_CodingUnused) {
	     indent printf("No coding format returned\n");
	     return;
	}

	switch (sAudioPortFormat.eEncoding) {
	case OMX_AUDIO_CodingPCM:
	    indent printf("Supported encoding is PCM\n");
	    getPCMInformation(indentLevel+1, portNumber);
	    break; 
	case OMX_AUDIO_CodingVORBIS:
	    indent printf("Supported encoding is Ogg Vorbis\n");
	    break; 
	case OMX_AUDIO_CodingMP3:
	    indent printf("Supported encoding is MP3\n");
	    getMP3Information(indentLevel+1, portNumber);
	    break;
#ifdef RASPBERRY_PI
	case OMX_AUDIO_CodingFLAC:
	    indent printf("Supported encoding is FLAC\n");
	    break; 
	case OMX_AUDIO_CodingDDP:
	    indent printf("Supported encoding is DDP\n");
	    break; 
	case OMX_AUDIO_CodingDTS:
	    indent printf("Supported encoding is DTS\n");
	    break; 
	case OMX_AUDIO_CodingWMAPRO:
	    indent printf("Supported encoding is WMAPRO\n");
	    break; 
	case OMX_AUDIO_CodingATRAC3:
	    indent printf("Supported encoding is ATRAC3\n");
	    break;
	case OMX_AUDIO_CodingATRACX:
	    indent printf("Supported encoding is ATRACX\n");
	    break;
	case OMX_AUDIO_CodingATRACAAL:
	    indent printf("Supported encoding is ATRACAAL\n");
	    break;
#endif
	case OMX_AUDIO_CodingAAC:
	    indent printf("Supported encoding is AAC\n");
	    break; 
	case OMX_AUDIO_CodingWMA:
	    indent printf("Supported encoding is WMA\n");
	    break;
	case OMX_AUDIO_CodingRA:
	    indent printf("Supported encoding is RA\n");
	    break; 
	case OMX_AUDIO_CodingAMR:
	    indent printf("Supported encoding is AMR\n");
	    break; 
	case OMX_AUDIO_CodingEVRC:
	    indent printf("Supported encoding is EVRC\n");
	    break;
	case OMX_AUDIO_CodingG726:
	    indent printf("Supported encoding is G726\n");
	    break;
	case OMX_AUDIO_CodingMIDI:
	    indent printf("Supported encoding is MIDI\n");
	    break;

	    /*
	case OMX_AUDIO_Coding:
	    indent printf("Supported encoding is \n");
	    break;
	    */
	default:
	    indent printf("Supported encoding is not PCM or MP3 or Vorbis, is 0x%X\n",
		  sAudioPortFormat.eEncoding);
	}
        sAudioPortFormat.nIndex++;
    }
}



void getAudioPortInformation(int indentLevel, int nPort, OMX_PARAM_PORTDEFINITIONTYPE sPortDef) {
    indent printf("Port %d requires %d buffers\n", nPort, sPortDef.nBufferCountMin); 
    indent printf("Port %d has min buffer size %d bytes\n", nPort, sPortDef.nBufferSize); 
    
    if (sPortDef.eDir == OMX_DirInput) {
	indent printf("Port %d is an input port\n", nPort);
    } else {
	indent printf("Port %d is an output port\n",  nPort);
    }
    switch (sPortDef.eDomain) {
    case OMX_PortDomainAudio:
	indent printf("Port %d is an audio port\n", nPort);
	indent printf("Port mimetype %s\n",
	       sPortDef.format.audio.cMIMEType);

	switch (sPortDef.format.audio.eEncoding) {
	case OMX_AUDIO_CodingPCM:
	    indent printf("Port encoding is PCM\n");
	    break; 
	case OMX_AUDIO_CodingVORBIS:
	    indent printf("Port encoding is Ogg Vorbis\n");
	    break; 
	case OMX_AUDIO_CodingMP3:
	    indent printf("Port encoding is MP3\n");
	    break; 
	default:
	    indent printf("Port encoding is not PCM or MP3 or Vorbis, is %d\n",
		   sPortDef.format.audio.eEncoding);
	}
	getSupportedAudioFormats(indentLevel+1, nPort);

	break;
	/* could put other port types here */
    default:
	indent printf("Port %d is not an audio port\n",  nPort);
    }    
}

void getAllAudioPortsInformation(int indentLevel) {
    OMX_PORT_PARAM_TYPE param;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;

    int startPortNumber;
    int nPorts;
    int n;

    setHeader(&param, sizeof(OMX_PORT_PARAM_TYPE));

    err = OMX_GetParameter(handle, OMX_IndexParamAudioInit, &param);
    if(err != OMX_ErrorNone){
	fprintf(stderr, "Error in getting audio OMX_PORT_PARAM_TYPE parameter\n", 0);
	return;
    }
    indent printf("Audio ports:\n");
    indentLevel++;

    startPortNumber = param.nStartPortNumber;
    nPorts = param.nPorts;
    if (nPorts == 0) {
	indent printf("No ports of this type\n");
	return;
    }

    indent printf("Ports start on %d\n", startPortNumber);
    indent printf("There are %d open ports\n", nPorts);


    for (n = 0; n < nPorts; n++) {
	setHeader(&sPortDef, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
	sPortDef.nPortIndex = startPortNumber + n;
	err = OMX_GetParameter(handle, OMX_IndexParamPortDefinition, &sPortDef);
	if(err != OMX_ErrorNone){
	    fprintf(stderr, "Error in getting OMX_PORT_DEFINITION_TYPE parameter\n", 0);
	    exit(1);
	}
	indent printf("Port %d has %d buffers of size %d\n",
		      sPortDef.nPortIndex,
		      sPortDef.nBufferCountActual,
		      sPortDef.nBufferSize);
	indent printf("Direction is %s\n", 
		      (sPortDef.eDir == OMX_DirInput ? "input" : "output"));
	getAudioPortInformation(indentLevel+1, startPortNumber + n, sPortDef);
    }
}

void getAllVideoPortsInformation(int indentLevel) {
    OMX_PORT_PARAM_TYPE param;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
    int startPortNumber;
    int nPorts;
    int n;

    setHeader(&param, sizeof(OMX_PORT_PARAM_TYPE));

    err = OMX_GetParameter(handle, OMX_IndexParamVideoInit, &param);
    if(err != OMX_ErrorNone){
	fprintf(stderr, "Error in getting video OMX_PORT_PARAM_TYPE parameter\n", 0);
	return;
    }

    printf("Video ports:\n");
    indentLevel++;

    startPortNumber = param.nStartPortNumber;
    nPorts = param.nPorts;
    if (nPorts == 0) {
	indent printf("No ports of this type\n");
	return;
    }

    indent printf("Ports start on %d\n", startPortNumber);
    indent printf("There are %d open ports\n", nPorts);


    for (n = 0; n < nPorts; n++) {
	setHeader(&sPortDef, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
	sPortDef.nPortIndex = startPortNumber + n;
	err = OMX_GetParameter(handle, OMX_IndexParamPortDefinition, &sPortDef);
	if(err != OMX_ErrorNone){
	    fprintf(stderr, "Error in getting OMX_PORT_DEFINITION_TYPE parameter\n", 0);
	    exit(1);
	}
	//getVideoPortInformation(indentLevel+1, startPortNumber + n, sPortDef);
	indent printf("Port %d has %d buffers (minimum %d) of size %d\n",
		      sPortDef.nPortIndex,
		      sPortDef.nBufferCountActual,
		      sPortDef.nBufferCountMin,
		      sPortDef.nBufferSize);
	indent printf("Direction is %s\n", 
		      (sPortDef.eDir == OMX_DirInput ? "input" : "output"));
	
	getSupportedVideoFormats(indentLevel+1,  startPortNumber + n);
    }
}

void getAllImagePortsInformation(int indentLevel) {
    OMX_PORT_PARAM_TYPE param;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
    int startPortNumber;
    int nPorts;
    int n;

    setHeader(&param, sizeof(OMX_PORT_PARAM_TYPE));

    err = OMX_GetParameter(handle, OMX_IndexParamImageInit, &param);
    if(err != OMX_ErrorNone){
	fprintf(stderr, "Error in getting image OMX_PORT_PARAM_TYPE parameter\n", 0);
	return;
    }
    printf("Image ports:\n");
    indentLevel++;

    startPortNumber = param.nStartPortNumber;
    nPorts = param.nPorts;
    if (nPorts == 0) {
	indent printf("No ports of this type\n");
	return;
    }

    indent printf("Ports start on %d\n", startPortNumber);
    indent printf("There are %d open ports\n", nPorts);



    for (n = 0; n < nPorts; n++) {
	setHeader(&sPortDef, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
	sPortDef.nPortIndex = startPortNumber + n;
	err = OMX_GetParameter(handle, OMX_IndexParamPortDefinition, &sPortDef);
	if(err != OMX_ErrorNone){
	    fprintf(stderr, "Error in getting OMX_PORT_DEFINITION_TYPE parameter\n", 0);
	    exit(1);
	}

	indent printf("Port %d has %d buffers (minimum %d) of size %d\n",
		      sPortDef.nPortIndex,
		      sPortDef.nBufferCountActual,
		      sPortDef.nBufferCountMin,
		      sPortDef.nBufferSize);
	indent printf("Direction is %s\n", 
		      (sPortDef.eDir == OMX_DirInput ? "input" : "output"));

	//getImagePortInformation(indentLevel+1, startPortNumber + n, sPortDef);
	getSupportedImageFormats(indentLevel+1,  startPortNumber + n);
    }
}

void getAllOtherPortsInformation(int indentLevel) {
    OMX_PORT_PARAM_TYPE param;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
    int startPortNumber;
    int nPorts;
    int n;

    setHeader(&param, sizeof(OMX_PORT_PARAM_TYPE));

    err = OMX_GetParameter(handle, OMX_IndexParamOtherInit, &param);
    if(err != OMX_ErrorNone){
	fprintf(stderr, "Error in getting other OMX_PORT_PARAM_TYPE parameter\n", 0);
	exit(1);
    }
    printf("Other ports:\n");
    indentLevel++;

    startPortNumber = param.nStartPortNumber;
    nPorts = param.nPorts;
    if (nPorts == 0) {
	indent printf("No ports of this type\n");
	return;
    }

    indent printf("Ports start on %d\n", startPortNumber);
    indent printf("There are %d open ports\n", nPorts);

    indent printf("Port %d has %d buffers of size %d\n",
		  sPortDef.nPortIndex,
		  sPortDef.nBufferCountActual,
		  sPortDef.nBufferSize);
    indent printf("Direction is %s\n", 
		  (sPortDef.eDir == OMX_DirInput ? "input" : "output"));
}

void getAllPortsInformation(int indentLevel) {
    OMX_PORT_PARAM_TYPE param;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
    int startPortNumber;
    int nPorts;
    int n;

    setHeader(&param, sizeof(OMX_PORT_PARAM_TYPE));

    err = OMX_GetParameter(handle, OMX_IndexParamVideoInit, &param);
    if(err != OMX_ErrorNone){
	fprintf(stderr, "Error in getting video OMX_PORT_PARAM_TYPE parameter\n", 0);
	return;
    }

    printf("Video ports:\n");
    indentLevel++;

    startPortNumber = param.nStartPortNumber;
    nPorts = param.nPorts;
    if (nPorts == 0) {
	indent printf("No ports of this type\n");
	return;
    }

    indent printf("Ports start on %d\n", startPortNumber);
    indent printf("There are %d open ports\n", nPorts);

    for (n = 0; n < nPorts; n++) {
	setHeader(&sPortDef, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
	sPortDef.nPortIndex = startPortNumber + n;
	err = OMX_GetParameter(handle, OMX_IndexParamPortDefinition, &sPortDef);
	if(err != OMX_ErrorNone){
	    fprintf(stderr, "Error in getting OMX_PORT_DEFINITION_TYPE parameter\n", 0);
	    exit(1);
	}
	indent printf("Port %d has %d buffers of size %d\n",
		      sPortDef.nPortIndex,
		      sPortDef.nBufferCountActual,
		      sPortDef.nBufferSize);
	indent printf("Direction is %s\n", 
		      (sPortDef.eDir == OMX_DirInput ? "input" : "output"));
	switch (sPortDef.eDomain) {
	case  OMX_PortDomainVideo:
	    indent printf("Domain is video\n");
	    getSupportedVideoFormats(indentLevel+1,  startPortNumber + n);
	    break;
	case  OMX_PortDomainImage:
	    indent printf("Domain is image\n");
	    getSupportedImageFormats(indentLevel+1,  startPortNumber + n);
	    break;
	case  OMX_PortDomainAudio:
	    indent printf("Domain is audio\n");
	    getSupportedAudioFormats(indentLevel+1,  startPortNumber + n);
	    break;
	case  OMX_PortDomainOther:
	    indent printf("Domain is other\n");
	    // getSupportedOtherFormats(indentLevel+1,  startPortNumber + n);
	    break;
	}
	//getVideoPortInformation(indentLevel+1, startPortNumber + n, sPortDef);
	/*
	if (sPortDef.eDomain == OMX_PortDomainVideo)
	    getSupportedVideoFormats(indentLevel+1,  startPortNumber + n);
	else
	    indent printf("Not a video port\n");
	*/
    }
}

int main(int argc, char** argv) {

    OMX_PORT_PARAM_TYPE param;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
    OMX_AUDIO_PORTDEFINITIONTYPE sAudioPortDef;
    OMX_AUDIO_PARAM_PORTFORMATTYPE sAudioPortFormat;
    OMX_AUDIO_PARAM_PCMMODETYPE sPCMMode;

#ifdef RASPBERRY_PI
    //char *componentName = "OMX.broadcom.audio_mixer";
    //char *componentName = "OMX.broadcom.audio_mixer";
    char *componentName = "OMX.broadcom.video_render";
#else
#ifdef LIM
    char *componentName = "OMX.limoi.alsa_sink";
#else
    char *componentName = "OMX.st.volume.component";
#endif
#endif
    unsigned char name[128]; /* spec says 128 is max name length */
    OMX_UUIDTYPE uid;
    int startPortNumber;
    int nPorts;
    int n;

    /* ovveride component name by command line argument */
    if (argc == 2) {
	componentName = argv[1];
    }

# ifdef RASPBERRY_PI
    bcm_host_init();
# endif

    err = OMX_Init();
    if(err != OMX_ErrorNone) {
	fprintf(stderr, "OMX_Init() failed\n", 0);
	exit(1);
    }
    /** Ask the core for a handle to the volume control component
     */
    err = OMX_GetHandle(&handle, componentName, NULL /*app private data */, &callbacks);
    if (err != OMX_ErrorNone) {
	fprintf(stderr, "OMX_GetHandle failed\n", 0);
	exit(1);
    }
    err = OMX_GetComponentVersion(handle, name, &compVersion, &specVersion, &uid);
    if (err != OMX_ErrorNone) {
	fprintf(stderr, "OMX_GetComponentVersion failed\n", 0);
	exit(1);
    }
    printf("Component name: %s version %d.%d, Spec version %d.%d\n",
	   name, compVersion.s.nVersionMajor,
	   compVersion.s.nVersionMinor,
	   specVersion.s.nVersionMajor,
	   specVersion.s.nVersionMinor);

    /** Get  ports information */
    //getAllPortsInformation(0);

    
    getAllAudioPortsInformation(0);
    getAllVideoPortsInformation(0);
    getAllImagePortsInformation(0);
    getAllOtherPortsInformation(0);
   

    exit(0);
}
