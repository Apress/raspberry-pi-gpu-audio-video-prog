#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <OMX_Core.h>
#include <OMX_Component.h>

#include <bcm_host.h>
#include <ilclient.h>

#define IMG  "/opt/vc/src/hello_pi/hello_video/test.h264"

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
    fprintf(stderr, "OMX error %s\n", err2str(data));
}

int get_file_size(char *fname) {
    struct stat st;

    if (stat(fname, &st) == -1) {
	    perror("Stat'ing img file");
	    return -1;
	}
    return(st.st_size);
}

static void set_video_decoder_input_format(COMPONENT_T *component) {
   // set input video format
    printf("Setting video decoder format\n");
    OMX_VIDEO_PARAM_PORTFORMATTYPE videoPortFormat;
    //setHeader(&videoPortFormat,  sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
    memset(&videoPortFormat, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
    videoPortFormat.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
    videoPortFormat.nVersion.nVersion = OMX_VERSION;

    videoPortFormat.nPortIndex = 130;
    videoPortFormat.eCompressionFormat = OMX_VIDEO_CodingAVC;
    OMX_SetParameter(ilclient_get_handle(component),
                     OMX_IndexParamVideoPortFormat, &videoPortFormat);

}

OMX_ERRORTYPE read_into_buffer_and_empty(FILE *fp,
					 COMPONENT_T *component,
					 OMX_BUFFERHEADERTYPE *buff_header,
					 int *toread) {
    OMX_ERRORTYPE r;

    int buff_size = buff_header->nAllocLen;
    int nread = fread(buff_header->pBuffer, 1, buff_size, fp);



    buff_header->nFilledLen = nread;
    *toread -= nread;
    printf("Read %d, %d still left\n", nread, *toread);

    if (*toread <= 0) {
	printf("Setting EOS on input\n");
	buff_header->nFlags |= OMX_BUFFERFLAG_EOS;
    }
    r = OMX_EmptyThisBuffer(ilclient_get_handle(component),
			buff_header);
    if (r != OMX_ErrorNone) {
	fprintf(stderr, "Empty buffer error %s\n",
		err2str(r));
    }
    return r;
}

OMX_ERRORTYPE save_info_from_filled_buffer(COMPONENT_T *component,
					   OMX_BUFFERHEADERTYPE * buff_header) {
    OMX_ERRORTYPE r;

    printf("Got a filled buffer with %d, allocated %d\n", 
	   buff_header->nFilledLen,
	   buff_header->nAllocLen);
    if (buff_header->nFlags & OMX_BUFFERFLAG_EOS) {
	printf("Got EOS on output\n");
	exit(0);
    }

    // and then refill it
    r = OMX_FillThisBuffer(ilclient_get_handle(component), 
			   buff_header); 
    if (r != OMX_ErrorNone) {
	fprintf(stderr, "Fill buffer error %s\n",
		err2str(r));
    }
    return r;
}

void get_output_port_settings(COMPONENT_T *component) {
    OMX_PARAM_PORTDEFINITIONTYPE portdef;

    printf("Port settings changed\n");
    // need to setup the input for the resizer with the output of the
    // decoder
    portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
    portdef.nVersion.nVersion = OMX_VERSION;
    portdef.nPortIndex = 131;
    OMX_GetParameter(ilclient_get_handle(component),
                     OMX_IndexParamPortDefinition, &portdef);

    unsigned int uWidth =
        (unsigned int) portdef.format.video.nFrameWidth;
    unsigned int uHeight =
        (unsigned int) portdef.format.video.nFrameHeight;
    unsigned int uStride =
        (unsigned int) portdef.format.video.nStride;
    unsigned int uSliceHeight =
        (unsigned int) portdef.format.video.nSliceHeight;
    printf("Frame width %d, frame height %d, stride %d, slice height %d\n",
	   uWidth,
	   uHeight,
	   uStride,
	   uSliceHeight);
    printf("Getting format Compression 0x%x Color Format: 0x%x\n",
	   (unsigned int) portdef.format.video.eCompressionFormat,
	   (unsigned int) portdef.format.video.eColorFormat);
}

int main(int argc, char** argv) {

    int i;
    char *componentName;
    int err;
    ILCLIENT_T  *handle;
    COMPONENT_T *component;
    FILE *fp = fopen(IMG, "r");
    int toread = get_file_size(IMG);
    OMX_BUFFERHEADERTYPE *buff_header;

    componentName = "video_decode";


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

    // must be before we enable buffers
    set_video_decoder_input_format(component);

    // input port
    ilclient_enable_port_buffers(component, 130, 
				 NULL, NULL, NULL);
    ilclient_enable_port(component, 130);




    err = ilclient_change_component_state(component,
					  OMX_StateExecuting);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Executing\n");
	exit(1);
    }
    printState(ilclient_get_handle(component));


    // Read the first block so that the component can get
    // the dimensions of the video and call port settings
    // changed on the output port to configure it
    int port_settings_changed = 0;
    while (!port_settings_changed) {
    buff_header = 
	ilclient_get_input_buffer(component,
				  130,
				  1 /* block */);
    if (buff_header != NULL) {
	read_into_buffer_and_empty(fp,
				   component,
				   buff_header,
				   &toread);

	// If all the file has been read in, then
	// we have to re-read this first block.
	// Broadcom bug?
	if (toread <= 0) {
	    printf("Rewinding\n");
	    // wind back to start and repeat
	    fp = freopen(IMG, "r", fp);
	    toread = get_file_size(IMG);
	}
    }

    // wait for first input block to set params for output port
    err = ilclient_wait_for_event(component, 
				  OMX_EventPortSettingsChanged, 
				  131, 0, 0, 1,
				  ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 
				  2000);
    if (err < 0) {
	printf("Wait for port settings changed timed out\n");
    } else {
	port_settings_changed = 1;
    }
    err = ilclient_remove_event(component, 
				  OMX_EventPortSettingsChanged, 
				  131, 0, 0, 1);
    if (err < 0) {
	printf("Wait for remove port settings changed timed out\n");
    } else {
	port_settings_changed = 1;
    }
    }

    get_output_port_settings(component);

    // now enable output port since port params have been set
    ilclient_enable_port_buffers(component, 131, 
				 NULL, NULL, NULL);
    ilclient_enable_port(component, 131);

    // now work through the file
    while (toread > 0) {
	OMX_ERRORTYPE r;

	// do we have an input buffer we can fill and empty?
	buff_header = 
	    ilclient_get_input_buffer(component,
				      130,
				      1 /* block */);
	if (buff_header != NULL) {
	    read_into_buffer_and_empty(fp,
				   component,
				   buff_header,
				   &toread);
	}

	// do we have an output buffer that has been filled?
	buff_header = 
	    ilclient_get_output_buffer(component,
				      131,
				      0 /* no block */);
	if (buff_header != NULL) {
	    save_info_from_filled_buffer(component,
					 buff_header);
	} else {
	    printf("No filled buffer\n");
	}
    }

    while (1) {
	printf("Getting last output buffers\n");
	buff_header = 
	    ilclient_get_output_buffer(component,
				       131,
				       1 /* block */);
	if (buff_header != NULL) {
	    save_info_from_filled_buffer(component,
					 buff_header);
	}
    }
    exit(0);
}
