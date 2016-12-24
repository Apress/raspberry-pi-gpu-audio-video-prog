/*
  Copyright (c) 2012, Broadcom Europe Ltd
  Copyright (c) 2012, OtherCrashOverride
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
  * Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
  * Neither the name of the copyright holder nor the
  names of its contributors may be used to endorse or promote products
  derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Video decode demo using OpenMAX IL though the ilcient helper library

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "bcm_host.h"
#include "ilclient.h"

static OMX_BUFFERHEADERTYPE* eglBuffer = NULL;
static COMPONENT_T* egl_render = NULL;

static void* eglImage = 0;

void my_fill_buffer_done(void* data, COMPONENT_T* comp)
{
    if (OMX_FillThisBuffer(ilclient_get_handle(egl_render), eglBuffer) != OMX_ErrorNone)
	{
	    printf("OMX_FillThisBuffer failed in callback\n");
	    exit(1);
	}
}

int get_file_size(char *fname) {
    struct stat st;

    if (stat(fname, &st) == -1) {
	perror("Stat'ing img file");
	return -1;
    }
    return(st.st_size);
}

#define err2str(x) ""

OMX_ERRORTYPE read_into_buffer_and_empty(FILE *fp,
					 COMPONENT_T *component,
					 OMX_BUFFERHEADERTYPE *buff_header,
					 int *toread) {
    OMX_ERRORTYPE r;

    int buff_size = buff_header->nAllocLen;
    int nread = fread(buff_header->pBuffer, 1, buff_size, fp);


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

// Modified function prototype to work with pthreads
void *video_decode_test(void* arg)
{
    const char* filename = "/opt/vc/src/hello_pi/hello_video/test.h264";
    eglImage = arg;

    if (eglImage == 0)
	{
	    printf("eglImage is null.\n");
	    exit(1);
	}

    OMX_VIDEO_PARAM_PORTFORMATTYPE format;
    OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;

    COMPONENT_T *video_decode = NULL;
    COMPONENT_T *list[3];  // last entry should be null
    TUNNEL_T tunnel[2]; // last entry should be null

    ILCLIENT_T *client;
    FILE *in;
    int status = 0;
    unsigned int data_len = 0;

    memset(list, 0, sizeof(list));
    memset(tunnel, 0, sizeof(tunnel));

    if((in = fopen(filename, "rb")) == NULL)
	return (void *)-2;

    if((client = ilclient_init()) == NULL)
	{
	    fclose(in);
	    return (void *)-3;
	}

    if(OMX_Init() != OMX_ErrorNone)
	{
	    ilclient_destroy(client);
	    fclose(in);
	    return (void *)-4;
	}

    // callback
    ilclient_set_fill_buffer_done_callback(client, my_fill_buffer_done, 0);

    // create video_decode
    if(ilclient_create_component(client, &video_decode, "video_decode", ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS) != 0)
	status = -14;
    list[0] = video_decode;

    // create egl_render
    if(status == 0 && ilclient_create_component(client, &egl_render, "egl_render", ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_OUTPUT_BUFFERS) != 0)
	status = -14;
    list[1] = egl_render;

    set_tunnel(tunnel, video_decode, 131, egl_render, 220);
    ilclient_change_component_state(video_decode, OMX_StateIdle);

    memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
    format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
    format.nVersion.nVersion = OMX_VERSION;
    format.nPortIndex = 130;
    format.eCompressionFormat = OMX_VIDEO_CodingAVC;

    if (status != 0) {
	fprintf(stderr, "Error has occurred %d\n", status);
	exit(1);
    }

    if(OMX_SetParameter(ILC_GET_HANDLE(video_decode), 
			OMX_IndexParamVideoPortFormat, &format) != OMX_ErrorNone) {
	fprintf(stderr, "Error setting port format\n");
	exit(1);
    }

    if(ilclient_enable_port_buffers(video_decode, 130, NULL, NULL, NULL) != 0) {
	fprintf(stderr, "Error enablng port buffers\n");
	exit(1);
    }


    OMX_BUFFERHEADERTYPE *buf;
    int port_settings_changed = 0;
    int first_packet = 1;

    ilclient_change_component_state(video_decode, OMX_StateExecuting);

    int toread = get_file_size(filename);
    // Read the first block so that the video_decode can get
    // the dimensions of the video and call port settings
    // changed on the output port to configure it
    while (toread > 0) {
	buf = 
	    ilclient_get_input_buffer(video_decode,
				      130,
				      1 /* block */);
	if (buf != NULL) {
	    read_into_buffer_and_empty(in,
				       video_decode,
				       buf,
				       &toread);

	    // If all the file has been read in, then
	    // we have to re-read this first block.
	    // Broadcom bug?
	    if (toread <= 0) {
		printf("Rewinding\n");
		// wind back to start and repeat
		//fp = freopen(IMG, "r", fp);
		rewind(in);
		toread = get_file_size(filename);
	    }
	}

	if (toread > 0 && ilclient_remove_event(video_decode, 
						OMX_EventPortSettingsChanged, 
						131, 0, 0, 1) == 0) {
	    printf("Removed port settings event\n");
	    break;
	} else {
	    // printf("No portr settting seen yet\n");
	}
	// wait for first input block to set params for output port
	if (toread == 0) {
	    int err;
	    // wait for first input block to set params for output port
	    err = ilclient_wait_for_event(video_decode, 
					  OMX_EventPortSettingsChanged, 
					  131, 0, 0, 1,
					  ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 
					  2000);
	    if (err < 0) {
		fprintf(stderr, "No port settings change\n");
		//exit(1);
	    } else {
		printf("Port settings changed\n");
		break;
	    }
	}
    }

    if(ilclient_setup_tunnel(tunnel, 0, 0) != 0)
	{
	    status = -7;
	    exit(1);
	}

    // Set egl_render to idle
    ilclient_change_component_state(egl_render, OMX_StateIdle);

    // Enable the output port and tell egl_render to use the texture as a buffer
    //ilclient_enable_port(egl_render, 221); THIS BLOCKS SO CANT BE USED
    if (OMX_SendCommand(ILC_GET_HANDLE(egl_render), OMX_CommandPortEnable, 221, NULL) != OMX_ErrorNone)
	{
	    printf("OMX_CommandPortEnable failed.\n");
	    exit(1);
	}

    if (OMX_UseEGLImage(ILC_GET_HANDLE(egl_render), &eglBuffer, 221, NULL, eglImage) != OMX_ErrorNone)
	{
	    printf("OMX_UseEGLImage failed.\n");
	    exit(1);
	}

    // Set egl_render to executing
    ilclient_change_component_state(egl_render, OMX_StateExecuting);


    // Request egl_render to write data to the texture buffer
    if(OMX_FillThisBuffer(ILC_GET_HANDLE(egl_render), eglBuffer) != OMX_ErrorNone)
	{
	    printf("OMX_FillThisBuffer failed.\n");
	    exit(1);
	}


   // now work through the file
    while (toread > 0) {
	OMX_ERRORTYPE r;

	// do we have a decode input buffer we can fill and empty?
	buf = 
	    ilclient_get_input_buffer(video_decode,
				      130,
				      1 /* block */);
	if (buf != NULL) {
	    read_into_buffer_and_empty(in,
				       video_decode,
				       buf,
				       &toread);
	}
    }

    sleep(2);

    // need to flush the renderer to allow video_decode to disable its input port
    ilclient_flush_tunnels(tunnel, 1);

    ilclient_disable_port_buffers(video_decode, 130, NULL, NULL, NULL);

    fclose(in);

    ilclient_disable_tunnel(tunnel);
    ilclient_teardown_tunnels(tunnel);

    ilclient_state_transition(list, OMX_StateIdle);
    ilclient_state_transition(list, OMX_StateLoaded);

    ilclient_cleanup_components(list);

    OMX_Deinit();

    ilclient_destroy(client);
    return (void *)status;
}

