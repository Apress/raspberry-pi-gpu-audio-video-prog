#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <OMX_Core.h>
#include <OMX_Component.h>

#include <bcm_host.h>
#include <ilclient.h>

#include <assert.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <VG/openvg.h>
#include <VG/vgu.h>
//#include "esUtil.h"

//#define IMG  "cimg0135.jpg"
#define IMG "/opt/vc/src/hello_pi/hello_video/test.h264"
//#define IMG "/home/pi/timidity/short.mpg"
//#define IMG "small.ogv"

pthread_t tid;

typedef struct
{
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    EGLConfig config;
} EGL_STATE_T;


EGL_STATE_T state, *p_state = &state;

typedef struct
{
   // Handle to a program object
   GLuint programObject;

} UserData;


void printState(OMX_HANDLETYPE handle) {
    // elided
}

char *err2str(int err) {
    return "error elided";
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

static void set_video_decoder_input_format(COMPONENT_T *component) {
    int err;

    // set input video format
    printf("Setting video decoder format\n");
    OMX_VIDEO_PARAM_PORTFORMATTYPE videoPortFormat;

    memset(&videoPortFormat, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
    videoPortFormat.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
    videoPortFormat.nVersion.nVersion = OMX_VERSION;

    videoPortFormat.nPortIndex = 130;
    videoPortFormat.eCompressionFormat = OMX_VIDEO_CodingAVC;

    err = OMX_SetParameter(ilclient_get_handle(component),
			   OMX_IndexParamVideoPortFormat, &videoPortFormat);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error setting video decoder format %s\n", err2str(err));
        return err;
    } else {
        printf("Video decoder format set up ok\n");
    }


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
    set_video_decoder_input_format(*decodeComponent);
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

void init_egl(EGL_STATE_T *state)
{
    EGLint num_configs;
    EGLBoolean result;

    //bcm_host_init();

    static const EGLint attribute_list[] =
	{
	    EGL_RED_SIZE, 8,
	    EGL_GREEN_SIZE, 8,
	    EGL_BLUE_SIZE, 8,
	    EGL_ALPHA_SIZE, 8,
	    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	    EGL_SAMPLES, 1,
	    EGL_NONE
	};

    static const EGLint context_attributes[] =
	{
	    EGL_CONTEXT_CLIENT_VERSION, 2,
	    EGL_NONE
	};

    // get an EGL display connection
    state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    // initialize the EGL display connection
    result = eglInitialize(state->display, NULL, NULL);

    // get an appropriate EGL frame buffer configuration
    result = eglChooseConfig(state->display, attribute_list, &state->config, 1, &num_configs);
    assert(EGL_FALSE != result);

    // Choose the OpenVG API
    result = eglBindAPI(EGL_OPENVG_API);
    assert(EGL_FALSE != result);

    // create an EGL rendering context
    state->context = eglCreateContext(state->display, 
				      state->config,
				      NULL, // EGL_NO_CONTEXT, 
				      NULL);
				      // breaks if we use this: context_attributes);
    assert(state->context!=EGL_NO_CONTEXT);
}

void init_dispmanx(EGL_DISPMANX_WINDOW_T *nativewindow) {   
    int32_t success = 0;   
    uint32_t screen_width;
    uint32_t screen_height;

    DISPMANX_ELEMENT_HANDLE_T dispman_element;
    DISPMANX_DISPLAY_HANDLE_T dispman_display;
    DISPMANX_UPDATE_HANDLE_T dispman_update;
    VC_RECT_T dst_rect;
    VC_RECT_T src_rect;

    bcm_host_init();

    // create an EGL window surface
    success = graphics_get_display_size(0 /* LCD */, 
					&screen_width, 
					&screen_height);
    assert( success >= 0 );

    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.width = screen_width;
    dst_rect.height = screen_height;

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = screen_width << 16;
    src_rect.height = screen_height << 16;        

    dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
    dispman_update = vc_dispmanx_update_start( 0 );

    dispman_element = 
	vc_dispmanx_element_add(dispman_update, dispman_display,
				1 /*layer*/, &dst_rect, 0 /*src*/,
				&src_rect, DISPMANX_PROTECTION_NONE, 
				0 /*alpha*/, 0 /*clamp*/, 0 /*transform*/);

    // Build an EGL_DISPMANX_WINDOW_T from the Dispmanx window
    nativewindow->element = dispman_element;
    nativewindow->width = screen_width;
    nativewindow->height = screen_height;
    vc_dispmanx_update_submit_sync(dispman_update);

    printf("Got a Dispmanx window\n");
}

void egl_from_dispmanx(EGL_STATE_T *state, 
		       EGL_DISPMANX_WINDOW_T *nativewindow) {
    EGLBoolean result;
    static const EGLint attribute_list[] =
	{
	    EGL_RENDER_BUFFER, EGL_SINGLE_BUFFER,
	    EGL_NONE
	};

    state->surface = eglCreateWindowSurface(state->display, 
					    state->config, 
					    nativewindow, 
					    NULL );
					    //attribute_list);
    assert(state->surface != EGL_NO_SURFACE);

    // connect the context to the surface
    result = eglMakeCurrent(state->display, state->surface, state->surface, state->context);
    assert(EGL_FALSE != result);
}

// setfill sets the fill color
void setfill(float color[4]) {
    VGPaint fillPaint = vgCreatePaint();
    vgSetParameteri(fillPaint, VG_PAINT_TYPE, VG_PAINT_TYPE_COLOR);
    vgSetParameterfv(fillPaint, VG_PAINT_COLOR, 4, color);
    vgSetPaint(fillPaint, VG_FILL_PATH);
    vgDestroyPaint(fillPaint);
}
 
 
// setstroke sets the stroke color and width
void setstroke(float color[4], float width) {
    VGPaint strokePaint = vgCreatePaint();
    vgSetParameteri(strokePaint, VG_PAINT_TYPE, VG_PAINT_TYPE_COLOR);
    vgSetParameterfv(strokePaint, VG_PAINT_COLOR, 4, color);
    vgSetPaint(strokePaint, VG_STROKE_PATH);
    vgSetf(VG_STROKE_LINE_WIDTH, width);
    vgSeti(VG_STROKE_CAP_STYLE, VG_CAP_BUTT);
    vgSeti(VG_STROKE_JOIN_STYLE, VG_JOIN_MITER);
    vgDestroyPaint(strokePaint);
}

// Ellipse makes an ellipse at the specified location and dimensions, applying style
void Ellipse(float x, float y, float w, float h, float sw, float fill[4], float stroke[4]) {
    VGPath path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F, 1.0f, 0.0f, 0, 0, VG_PATH_CAPABILITY_ALL);
    vguEllipse(path, x, y, w, h);
    setfill(fill);
    setstroke(stroke, sw);
    vgDrawPath(path, VG_FILL_PATH | VG_STROKE_PATH);
    vgDestroyPath(path);
}

void draw() { 
    EGL_DISPMANX_WINDOW_T nativewindow;

    init_egl(p_state);
    init_dispmanx(&nativewindow);

    egl_from_dispmanx(p_state, &nativewindow);
 
    vgClear(0, 0, 1920, 1080);

    VGfloat color[4] = {0.4, 0.1, 1.0, 1.0}; // purple
    float c = 1.0;    
    float clearColor[4] = {c, c, c, 0.0}; // white transparent
    vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
    vgClear(0, 0, 1920, 1080);

    vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
    static float ht = 400;
    while (1) {
	vgClear(0, 0, 1920, 1080);
	Ellipse(1920/2, 1080/2, 600, ht--, 0, color, color);
	if (ht <= 0) {
	    ht = 200;
	}
	eglSwapBuffers(p_state->display, p_state->surface);
    }
    assert(vgGetError() == VG_NO_ERROR);

    vgFlush();

    eglSwapBuffers(p_state->display, p_state->surface);
}


void *overlay_vg(void *args) {
    draw();
}

void draw_openmax_video(int argc, char** argv) {
    int i;
    char *decodeComponentName;
    char *renderComponentName;
    int err;
    ILCLIENT_T  *handle;
    COMPONENT_T *decodeComponent;
    COMPONENT_T *renderComponent;
    FILE *fp; // = fopen(IMG, "r");
    int toread = get_file_size(IMG);
    OMX_BUFFERHEADERTYPE *buff_header;

    decodeComponentName = "video_decode";
    renderComponentName = "video_render";

    bcm_host_init();

    if (argc > 1) {
	fp = fopen(argv[1], "r");
    } else {
	fp =fopen(IMG, "r");
    }
    if (fp == NULL) {
	fprintf(stderr, "Can't open file\n");
	exit(1);
    }

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
    ilclient_enable_port_buffers(decodeComponent, 130, 
				 NULL, NULL, NULL);
    ilclient_enable_port(decodeComponent, 130);


    err = ilclient_change_component_state(decodeComponent,
					  OMX_StateExecuting);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Executing\n");
	exit(1);
    }
    printState(ilclient_get_handle(decodeComponent));


    // Read the first block so that the decodeComponent can get
    // the dimensions of the video and call port settings
    // changed on the output port to configure it
    while (toread > 0) {
	buff_header = 
	    ilclient_get_input_buffer(decodeComponent,
				      130,
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
		fp = freopen(IMG, "r", fp);
		toread = get_file_size(IMG);
	    }
	}

	if (toread > 0 && ilclient_remove_event(decodeComponent, 
						OMX_EventPortSettingsChanged, 
						131, 0, 0, 1) == 0) {
	    printf("Removed port settings event\n");
	    break;
	} else {
	    printf("No portr settting seen yet\n");
	}
	// wait for first input block to set params for output port
	if (toread == 0) {
	    // wait for first input block to set params for output port
	    err = ilclient_wait_for_event(decodeComponent, 
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

    // set up the tunnel between decode and render ports
    TUNNEL_T tunnel;
    set_tunnel(&tunnel, decodeComponent, 131, renderComponent, 90);
    if ((err = ilclient_setup_tunnel(&tunnel, 0, 0)) < 0) {
	fprintf(stderr, "Error setting up tunnel %X\n", err);
	exit(1);
    } else {
	printf("Tunnel set up ok\n");
    }	

    // Okay to go back to processing data
    // enable the decode output ports
   
    OMX_SendCommand(ilclient_get_handle(decodeComponent), 
		    OMX_CommandPortEnable, 131, NULL);
   
    ilclient_enable_port(decodeComponent, 131);

    // enable the render output ports
   
    OMX_SendCommand(ilclient_get_handle(renderComponent), 
		    OMX_CommandPortEnable, 90, NULL);
   
    ilclient_enable_port(renderComponent, 90);

    // set both components to executing state
    err = ilclient_change_component_state(decodeComponent,
					  OMX_StateExecuting);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Idle\n");
	exit(1);
    }
    err = ilclient_change_component_state(renderComponent,
					  OMX_StateExecuting);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Idle\n");
	exit(1);
    }

    // now work through the file
    while (toread > 0) {
	OMX_ERRORTYPE r;

	// do we have a decode input buffer we can fill and empty?
	buff_header = 
	    ilclient_get_input_buffer(decodeComponent,
				      130,
				      1 /* block */);
	if (buff_header != NULL) {
	    read_into_buffer_and_empty(fp,
				       decodeComponent,
				       buff_header,
				       &toread);
	}
    }

    ilclient_wait_for_event(renderComponent, 
			    OMX_EventBufferFlag, 
			    90, 0, OMX_BUFFERFLAG_EOS, 0,
			    ILCLIENT_BUFFER_FLAG_EOS, 10000);
    printf("EOS on render\n");
}

int main(int argc, char** argv) {

    pthread_create(&tid, NULL, overlay_vg, NULL);
    draw_openmax_video(argc, argv);

    exit(0);
}
