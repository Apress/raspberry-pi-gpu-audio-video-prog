#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <OMX_Core.h>
#include <OMX_Component.h>

#include <bcm_host.h>
#include <ilclient.h>

#define IMG  "cimg0135.jpg"
//#define IMG "hype.jpg"

void printState(OMX_HANDLETYPE handle) {
    OMX_STATETYPE state;
    OMX_ERRORTYPE err;

    err = OMX_GetState(handle, &state);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error on getting state\n");
        exit(1);
    }
    switch (state) {
    case OMX_StateLoaded:           printf("StateLoaded\n"); break;
    case OMX_StateIdle:             printf("StateIdle\n"); break;
    case OMX_StateExecuting:        printf("StateExecuting\n"); break;
    case OMX_StatePause:            printf("StatePause\n"); break;
    case OMX_StateWaitForResources: printf("StateWait\n"); break;
    case OMX_StateInvalid:          printf("StateInvalid\n"); break;
    default:                        printf("State unknown\n"); break;
    }
}

char *err2str(int err) {
    return "error deleted";
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

static void set_image_decoder_input_format(COMPONENT_T *component) {
   // set input image format
    OMX_IMAGE_PARAM_PORTFORMATTYPE imagePortFormat;

    memset(&imagePortFormat, 0, sizeof(OMX_IMAGE_PARAM_PORTFORMATTYPE));
    imagePortFormat.nSize = sizeof(OMX_IMAGE_PARAM_PORTFORMATTYPE);
    imagePortFormat.nVersion.nVersion = OMX_VERSION;

    imagePortFormat.nPortIndex = 320;
    imagePortFormat.eCompressionFormat = OMX_IMAGE_CodingJPEG;
    OMX_SetParameter(ilclient_get_handle(component),
                     OMX_IndexParamImagePortFormat, &imagePortFormat);

}

OMX_ERRORTYPE read_into_buffer_and_empty(FILE *fp,
					 COMPONENT_T *component,
					 OMX_BUFFERHEADERTYPE *buff_header,
					 int *toread) {
    OMX_ERRORTYPE r;

    int buff_size = buff_header->nAllocLen;
    int nread = fread(buff_header->pBuffer, 1, buff_size, fp);

    printf("Read %d\n", nread);

    buff_header->nFilledLen = nread;
    *toread -= nread;
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

    // do something here, like save the data - do nothing this time

    // and then refill it
    r = OMX_FillThisBuffer(ilclient_get_handle(component), 
			   buff_header); 
    if (r != OMX_ErrorNone) {
	fprintf(stderr, "Fill buffer error %s\n",
		err2str(r));
    }
    return r;
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

    componentName = "image_decode";


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
    set_image_decoder_input_format(component);

    // input port
    ilclient_enable_port_buffers(component, 320, 
				 NULL, NULL, NULL);
    ilclient_enable_port(component, 320);




    err = ilclient_change_component_state(component,
					  OMX_StateExecuting);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Executing\n");
	exit(1);
    }
    printState(ilclient_get_handle(component));


    // Read the first block so that the component can get
    // the dimensions of the image and call port settings
    // changed on the output port to configure it
    buff_header = 
	ilclient_get_input_buffer(component,
				  320,
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
    ilclient_wait_for_event(component, 
			    OMX_EventPortSettingsChanged, 
			    321, 0, 0, 1,
			    ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 
			    10000);

    // now enable output port since port params have been set
    ilclient_enable_port_buffers(component, 321, 
				 NULL, NULL, NULL);
    ilclient_enable_port(component, 321);

    // now work through the file
    while (toread > 0) {
	OMX_ERRORTYPE r;

	// do we have an input buffer we can fill and empty?
	buff_header = 
	    ilclient_get_input_buffer(component,
				      320,
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
				      321,
				      0 /* no block */);
	if (buff_header != NULL) {
	    save_info_from_filled_buffer(component,
					 buff_header);
	}
    }

    while (1) {
	printf("Getting last output buffers\n");
	buff_header = 
	    ilclient_get_output_buffer(component,
				       321,
				       1 /* block */);
	if (buff_header != NULL) {
	    save_info_from_filled_buffer(component,
					 buff_header);
	}
    }
    exit(0);
}
