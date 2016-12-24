#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <OMX_Component.h>

#include <bcm_host.h>
#include <ilclient.h>

//#define IMG  "cimg0135.jpg"
//#define IMG  "jan.jpg";
#define IMG "hype.jpg"
char *img_file = IMG;

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

unsigned int uWidth;
unsigned int uHeight;

unsigned int nBufferSize;

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

static void set_image_decoder_input_format(COMPONENT_T *component) {

    printf("Setting image decoder format\n");
    OMX_IMAGE_PARAM_PORTFORMATTYPE imagePortFormat;

    memset(&imagePortFormat, 0, sizeof(OMX_IMAGE_PARAM_PORTFORMATTYPE));
    imagePortFormat.nSize = sizeof(OMX_IMAGE_PARAM_PORTFORMATTYPE);
    imagePortFormat.nVersion.nVersion = OMX_VERSION;

    imagePortFormat.nPortIndex = 320;
    imagePortFormat.eCompressionFormat = OMX_IMAGE_CodingJPEG;
    OMX_SetParameter(ilclient_get_handle(component),
                     OMX_IndexParamImagePortFormat, &imagePortFormat);

}

void setup_decodeComponent(ILCLIENT_T  *handle, 
			   char *decodeComponentName, 
			   COMPONENT_T **decodeComponent) {
    int err;

    err = ilclient_create_component(handle,
				    decodeComponent,
				    decodeComponentName,
				    ILCLIENT_DISABLE_ALL_PORTS
				    |
				    ILCLIENT_ENABLE_INPUT_BUFFERS
				    |
				    ILCLIENT_ENABLE_OUTPUT_BUFFERS
				    );
    if (err == -1) {
	fprintf(stderr, "DecodeComponent create failed\n");
	exit(1);
    }
    printState(ilclient_get_handle(*decodeComponent));

    err = ilclient_change_component_state(*decodeComponent,
					  OMX_StateIdle);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Idle\n");
	exit(1);
    }
    printState(ilclient_get_handle(*decodeComponent));

    // must be before we enable buffers
    set_image_decoder_input_format(*decodeComponent);
}

void setup_renderComponent(ILCLIENT_T  *handle, 
			   char *renderComponentName, 
			   COMPONENT_T **renderComponent) {
    int err;

    err = ilclient_create_component(handle,
				    renderComponent,
				    renderComponentName,
				    ILCLIENT_DISABLE_ALL_PORTS
				    |
				    ILCLIENT_ENABLE_INPUT_BUFFERS
				    );
    if (err == -1) {
	fprintf(stderr, "RenderComponent create failed\n");
	exit(1);
    }
    printState(ilclient_get_handle(*renderComponent));

    err = ilclient_change_component_state(*renderComponent,
					  OMX_StateIdle);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Idle\n");
	exit(1);
    }
    printState(ilclient_get_handle(*renderComponent));
}

OMX_BUFFERHEADERTYPE *pOutputBufferHeader = NULL;
OMX_BUFFERHEADERTYPE **ppRenderInputBufferHeader;

int setup_shared_buffer_format(COMPONENT_T *decodeComponent, 
			       COMPONENT_T *renderComponent) {
    OMX_PARAM_PORTDEFINITIONTYPE portdef,  rportdef;;
    int ret;
    OMX_ERRORTYPE err;

    // need to setup the input for the render with the output of the
    // decoder
    portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
    portdef.nVersion.nVersion = OMX_VERSION;
    portdef.nPortIndex = 321;
    OMX_GetParameter(ilclient_get_handle(decodeComponent),
                     OMX_IndexParamPortDefinition, &portdef);

    // Get default values of render
    rportdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
    rportdef.nVersion.nVersion = OMX_VERSION;
    rportdef.nPortIndex = 90;
    rportdef.nBufferSize = portdef.nBufferSize;
    nBufferSize = portdef.nBufferSize;

    err = OMX_GetParameter(ilclient_get_handle(renderComponent),
                           OMX_IndexParamPortDefinition, &rportdef);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error getting render port params %s\n", err2str(err));
        return err;
    }

    // tell render input what the decoder output will be providing
    //Copy some
    rportdef.format.video.nFrameWidth = portdef.format.image.nFrameWidth;
    rportdef.format.video.nFrameHeight = portdef.format.image.nFrameHeight;
    rportdef.format.video.nStride = portdef.format.image.nStride;
    rportdef.format.video.nSliceHeight = portdef.format.image.nSliceHeight;

    err = OMX_SetParameter(ilclient_get_handle(renderComponent),
                           OMX_IndexParamPortDefinition, &rportdef);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error setting render port params %s\n", err2str(err));
        return err;
    } else {
        printf("Render port params set up ok, buf size %d\n",
	       portdef.nBufferSize);
    }

    return  OMX_ErrorNone;
}

int use_buffer(COMPONENT_T *renderComponent, 
		OMX_BUFFERHEADERTYPE *buff_header) {
    int ret;
    OMX_PARAM_PORTDEFINITIONTYPE portdef;

    ppRenderInputBufferHeader =
	(OMX_BUFFERHEADERTYPE **) malloc(sizeof(void) *
					 3);

    OMX_SendCommand(ilclient_get_handle(renderComponent), 
		    OMX_CommandPortEnable, 90, NULL);
    
    ilclient_wait_for_event(renderComponent,
			    OMX_EventCmdComplete,
			    OMX_CommandPortEnable, 1,
			    90, 1, 0,
			    5000);
    
    printState(ilclient_get_handle(renderComponent));

    ret = OMX_UseBuffer(ilclient_get_handle(renderComponent),
			&ppRenderInputBufferHeader[0],
			90,
			NULL,
			nBufferSize,
			buff_header->pBuffer);
    if (ret != OMX_ErrorNone) {
	fprintf(stderr, "Eror sharing buffer %s\n", err2str(ret));
	return ret;
    } else {
	printf("Sharing buffer ok\n");
    }

    ppRenderInputBufferHeader[0]->nAllocLen =
	buff_header->nAllocLen;

    int n;
    for (n = 1; n < 3; n++) {
	printState(ilclient_get_handle(renderComponent));
	ret = OMX_UseBuffer(ilclient_get_handle(renderComponent),
			    &ppRenderInputBufferHeader[n],
			    90,
			    NULL,
			    0,
			    NULL);
	if (ret != OMX_ErrorNone) {
	    fprintf(stderr, "Eror sharing null buffer %s\n", err2str(ret));
	    return ret;
	}
    }

    ilclient_enable_port(renderComponent, 90);

    ret = ilclient_change_component_state(renderComponent,
					  OMX_StateExecuting);
    if (ret < 0) {
	fprintf(stderr, "Couldn't change render state to Executing\n");
	exit(1);
    }
    return 0;
}

int main(int argc, char** argv) {

    int i;
    char *decodeComponentName;
    char *renderComponentName;
    int err;
    ILCLIENT_T  *handle;
    COMPONENT_T *decodeComponent;
    COMPONENT_T *renderComponent;

    if (argc == 2) {
	img_file = argv[1];
    }
    FILE *fp = fopen(img_file, "r");
    int toread = get_file_size(img_file);
  
    OMX_BUFFERHEADERTYPE *buff_header;

    decodeComponentName = "image_decode";
    renderComponentName = "video_render";

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


    setup_decodeComponent(handle, decodeComponentName, &decodeComponent);
    setup_renderComponent(handle, renderComponentName, &renderComponent);
    // both components now in Idle state, no buffers, ports disabled

    // input port
    ilclient_enable_port_buffers(decodeComponent, 320, 
				 NULL, NULL, NULL);
    ilclient_enable_port(decodeComponent, 320);


    err = ilclient_change_component_state(decodeComponent,
					  OMX_StateExecuting);
    if (err < 0) {
	fprintf(stderr, "Couldn't change decode state to Executing\n");
	exit(1);
    }
    printState(ilclient_get_handle(decodeComponent));


    // Read the first block so that the decodeComponent can get
    // the dimensions of the image and call port settings
    // changed on the output port to configure it
    buff_header = 
	ilclient_get_input_buffer(decodeComponent,
				  320,
				  1 /* block */);
    if (buff_header != NULL) {
	read_into_buffer_and_empty(fp,
				   decodeComponent,
				   buff_header,
				   &toread);

	// If all the file has been read in, then
	// we have to re-read this first block.
	// Broadcom bug?
	if (toread <= 0) {
	    printf("Rewinding\n");
	    // wind back to start and repeat
	    fp = freopen(img_file, "r", fp);
	    toread = get_file_size(img_file);
	}
    }

    // wait for first input block to set params for output port
    ilclient_wait_for_event(decodeComponent, 
			    OMX_EventPortSettingsChanged, 
			    321, 0, 0, 1,
			    ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 
			    10000);
    printf("Port settings changed\n");

    setup_shared_buffer_format(decodeComponent, renderComponent);

   ilclient_enable_port_buffers(decodeComponent, 321, 
				 NULL, NULL, NULL);
    ilclient_enable_port(decodeComponent, 321);

    // set decoder only to executing state
    err = ilclient_change_component_state(decodeComponent,
					  OMX_StateExecuting);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Executing\n");
	exit(1);
    }

    // now work through the file
    while (toread > 0) {
	OMX_ERRORTYPE r;

	// do we have a decode input buffer we can fill and empty?
	buff_header = 
	    ilclient_get_input_buffer(decodeComponent,
				      320,
				      1 /* block */);
	if (buff_header != NULL) {
	    read_into_buffer_and_empty(fp,
				       decodeComponent,
				       buff_header,
				       &toread);
	}

	// do we have an output buffer that has been filled?
	buff_header = 
	    ilclient_get_output_buffer(decodeComponent,
				      321,
				      0 /* no block */);
	if (buff_header != NULL) {
	    printf("Got an output buffer length %d\n",
		   buff_header->nFilledLen);
	    if (buff_header->nFlags & OMX_BUFFERFLAG_EOS) {
		printf("Got EOS\n");
	    }
	    if (pOutputBufferHeader == NULL) {
		use_buffer(renderComponent, buff_header);
		pOutputBufferHeader = buff_header;
	    }

	    if (buff_header->nFilledLen > 0) {
		OMX_EmptyThisBuffer(ilclient_get_handle(renderComponent),
				    buff_header);
	    }

	    OMX_FillThisBuffer(ilclient_get_handle(decodeComponent), 
			       buff_header); 
	}
    }

    int done = 0;
    while ( !done ) {
	printf("Getting last output buffers\n");
	buff_header = 
	    ilclient_get_output_buffer(decodeComponent,
				       321,
				       1 /* block */);
	printf("Got a final output buffer length %d\n",
	       buff_header->nFilledLen);
	if (buff_header->nFlags & OMX_BUFFERFLAG_EOS) {
	    printf("Got EOS\n");
	    done = 1;
	}

	if (pOutputBufferHeader == NULL) {
	    use_buffer(renderComponent, buff_header);
	    pOutputBufferHeader = buff_header;
	}

	ppRenderInputBufferHeader[0]->nFilledLen = buff_header->nFilledLen;
	OMX_EmptyThisBuffer(ilclient_get_handle(renderComponent),
			    ppRenderInputBufferHeader[0]);
   }

    sleep(100);

    exit(0);
}
