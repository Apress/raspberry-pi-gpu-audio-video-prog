#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <OMX_Core.h>
#include <OMX_Component.h>

#include <bcm_host.h>
#include <ilclient.h>

#define AUDIO  "enigma.s16"

#define OUT "out"
FILE *outfp;

void printState(OMX_HANDLETYPE handle) {
    // elided
}

char *err2str(int err) {
    return "elided";
}

void eos_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data) {
    fprintf(stderr, "Got eos event\n");
}

void error_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data) {
    //fprintf(stderr, "OMX error %s\n", err2str(data));
}

int get_file_size(char *fname) {
    struct stat st;

    if (stat(fname, &st) == -1) {
	perror("Stat'ing img file");
	return -1;
    }
    return(st.st_size);
}

static void set_audio_decoder_input_format(COMPONENT_T *component, 
					   int port, int format) {
    // set input audio format
    //printf("Setting audio decoder format\n");
    OMX_AUDIO_PARAM_PORTFORMATTYPE audioPortFormat;
    //setHeader(&audioPortFormat,  sizeof(OMX_AUDIO_PARAM_PORTFORMATTYPE));
    memset(&audioPortFormat, 0, sizeof(OMX_AUDIO_PARAM_PORTFORMATTYPE));
    audioPortFormat.nSize = sizeof(OMX_AUDIO_PARAM_PORTFORMATTYPE);
    audioPortFormat.nVersion.nVersion = OMX_VERSION;

    audioPortFormat.nPortIndex = port;
    //audioPortFormat.eEncoding = OMX_AUDIO_CodingPCM;
    audioPortFormat.eEncoding = format;
    OMX_SetParameter(ilclient_get_handle(component),
                     OMX_IndexParamAudioPortFormat, &audioPortFormat);
    //printf("Format set ok to %d\n", format);
}

char *format2str(OMX_AUDIO_CODINGTYPE format) {
    switch(format) {
    case OMX_AUDIO_CodingUnused: return "OMX_AUDIO_CodingUnused";
    case OMX_AUDIO_CodingAutoDetect: return "OMX_AUDIO_CodingAutoDetect";
    case OMX_AUDIO_CodingPCM: return "OMX_AUDIO_CodingPCM";
    case OMX_AUDIO_CodingADPCM: return "OMX_AUDIO_CodingADPCM";
    case OMX_AUDIO_CodingAMR: return "OMX_AUDIO_CodingAMR";
    case OMX_AUDIO_CodingGSMFR: return "OMX_AUDIO_CodingGSMFR";
    case OMX_AUDIO_CodingGSMEFR: return "OMX_AUDIO_CodingGSMEFR";
    case OMX_AUDIO_CodingGSMHR: return "OMX_AUDIO_CodingGSMHR";
    case OMX_AUDIO_CodingPDCFR: return "OMX_AUDIO_CodingPDCFR";
    case OMX_AUDIO_CodingPDCEFR: return "OMX_AUDIO_CodingPDCEFR";
    case OMX_AUDIO_CodingPDCHR: return "OMX_AUDIO_CodingPDCHR";
    case OMX_AUDIO_CodingTDMAFR: return "OMX_AUDIO_CodingTDMAFR";
    case OMX_AUDIO_CodingTDMAEFR: return "OMX_AUDIO_CodingTDMAEFR";
    case OMX_AUDIO_CodingQCELP8: return "OMX_AUDIO_CodingQCELP8";
    case OMX_AUDIO_CodingQCELP13: return "OMX_AUDIO_CodingQCELP13";
    case OMX_AUDIO_CodingEVRC: return "OMX_AUDIO_CodingEVRC";
    case OMX_AUDIO_CodingSMV: return "OMX_AUDIO_CodingSMV";
    case OMX_AUDIO_CodingG711: return "OMX_AUDIO_CodingG711";
    case OMX_AUDIO_CodingG723: return "OMX_AUDIO_CodingG723";
    case OMX_AUDIO_CodingG726: return "OMX_AUDIO_CodingG726";
    case OMX_AUDIO_CodingG729: return "OMX_AUDIO_CodingG729";
    case OMX_AUDIO_CodingAAC: return "OMX_AUDIO_CodingAAC";
    case OMX_AUDIO_CodingMP3: return "OMX_AUDIO_CodingMP3";
    case OMX_AUDIO_CodingSBC: return "OMX_AUDIO_CodingSBC";
    case OMX_AUDIO_CodingVORBIS: return "OMX_AUDIO_CodingVORBIS";
    case OMX_AUDIO_CodingWMA: return "OMX_AUDIO_CodingWMA";
    case OMX_AUDIO_CodingRA: return "OMX_AUDIO_CodingRA";
    case OMX_AUDIO_CodingMIDI: return "OMX_AUDIO_CodingMIDI";
    case OMX_AUDIO_CodingFLAC: return "OMX_AUDIO_CodingFLAC";
    case OMX_AUDIO_CodingDDP: return "OMX_AUDIO_CodingDDP";
    case OMX_AUDIO_CodingDTS: return "OMX_AUDIO_CodingDTS";
    case OMX_AUDIO_CodingWMAPRO: return "OMX_AUDIO_CodingWMAPRO";
    case OMX_AUDIO_CodingATRAC3: return "OMX_AUDIO_CodingATRAC3";
    case OMX_AUDIO_CodingATRACX: return "OMX_AUDIO_CodingATRACX";
    case OMX_AUDIO_CodingATRACAAL: return "OMX_AUDIO_CodingATRACAAL";
    default: return "Unknown format";
    }
}

void test_audio_port_formats(COMPONENT_T *component, int port) {
    int n = 2;
    while (n <= OMX_AUDIO_CodingMIDI) {
	set_audio_decoder_input_format(component, port, n);
	
	
	// input port
	if (ilclient_enable_port_buffers(component, port, 
					 NULL, NULL, NULL) < 0) {
	    printf("    Unsupported encoding is %s\n", 
		   format2str(n));
	} else {
	    printf("    Supported encoding is %s\n",
		  format2str(n));
	    ilclient_disable_port_buffers(component, port, 
					  NULL, NULL, NULL);
	}
	n++;
    }
    n = OMX_AUDIO_CodingFLAC;
    while (n <= OMX_AUDIO_CodingATRACAAL) {
	set_audio_decoder_input_format(component, port, n);
	
	
	// input port
	if (ilclient_enable_port_buffers(component, port, 
					 NULL, NULL, NULL) < 0) {
	    printf("    Unsupported encoding is %s\n", 
		   format2str(n));
	} else {
	    printf("    Supported encoding is %s\n", 
		   format2str(n));
	    ilclient_disable_port_buffers(component, port, 
					  NULL, NULL, NULL);
	}
	n++;
    }
}

void test_all_audio_ports(COMPONENT_T *component) {
    OMX_PORT_PARAM_TYPE param;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
    OMX_ERRORTYPE err;
    OMX_HANDLETYPE handle = ilclient_get_handle(component);

    int startPortNumber;
    int nPorts;
    int n;

    //setHeader(&param, sizeof(OMX_PORT_PARAM_TYPE));
    memset(&param, 0, sizeof(OMX_PORT_PARAM_TYPE));
    param.nSize = sizeof(OMX_PORT_PARAM_TYPE);
    param.nVersion.nVersion = OMX_VERSION;

    err = OMX_GetParameter(handle, OMX_IndexParamAudioInit, &param);
    if(err != OMX_ErrorNone){
	fprintf(stderr, "Error in getting audio OMX_PORT_PARAM_TYPE parameter\n");
	return;
    }
    printf("Audio ports:\n");

    startPortNumber = param.nStartPortNumber;
    nPorts = param.nPorts;
    if (nPorts == 0) {
	printf("No ports of this type\n");
	return;
    }

    printf("Ports start on %d\n", startPortNumber);
    printf("There are %d open ports\n", nPorts);


    for (n = 0; n < nPorts; n++) {
	memset(&sPortDef, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
	sPortDef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	sPortDef.nVersion.nVersion = OMX_VERSION;
	

	sPortDef.nPortIndex = startPortNumber + n;
	err = OMX_GetParameter(handle, OMX_IndexParamPortDefinition, &sPortDef);
	if(err != OMX_ErrorNone){
	    fprintf(stderr, "Error in getting OMX_PORT_DEFINITION_TYPE parameter\n");
	    exit(1);
	}
	printf("Port %d has %d buffers of size %d\n",
	       sPortDef.nPortIndex,
	       sPortDef.nBufferCountActual,
	       sPortDef.nBufferSize);
	printf("Direction is %s\n", 
	       (sPortDef.eDir == OMX_DirInput ? "input" : "output"));
	test_audio_port_formats(component, sPortDef.nPortIndex);
    }
}

int main(int argc, char** argv) {
    char *componentName;
    int err;
    ILCLIENT_T  *handle;
    COMPONENT_T *component;

    componentName = "audio_decode";
    if (argc == 2) {
	componentName = argv[1];
    }

    bcm_host_init();

    handle = ilclient_init();
    if (handle == NULL) {
	fprintf(stderr, "IL client init failed\n");
	exit(1);
    }

    if (OMX_Init() != OMX_ErrorNone) {
	ilclient_destroy(handle);
	fprintf(stderr, "OMX init failed\n");
	exit(1);
    }

    ilclient_set_error_callback(handle,
				error_callback,
				NULL);
    ilclient_set_eos_callback(handle,
			      eos_callback,
			      NULL);


    err = ilclient_create_component(handle,
				    &component,
				    componentName,
				    ILCLIENT_DISABLE_ALL_PORTS
				    |
				    ILCLIENT_ENABLE_INPUT_BUFFERS
				    |
				    ILCLIENT_ENABLE_OUTPUT_BUFFERS
				    );
    if (err == -1) {
	fprintf(stderr, "Component create failed\n");
	exit(1);
    }
    printState(ilclient_get_handle(component));

    err = ilclient_change_component_state(component,
					  OMX_StateIdle);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Idle\n");
	exit(1);
    }
    printState(ilclient_get_handle(component));

    test_all_audio_ports(component);

    exit(0);
}
