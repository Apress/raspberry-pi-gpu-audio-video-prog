
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include <bcm_host.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <VG/openvg.h>
#include <VG/vgu.h>

typedef struct
{
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    EGLConfig config;
} EGL_STATE_T;

struct egl_manager {
    EGLNativeDisplayType xdpy;
    EGLNativeWindowType xwin;
    EGLNativePixmapType xpix;

    EGLDisplay dpy;
    EGLConfig conf;
    EGLContext ctx;

    EGLSurface win;
    EGLSurface pix;
    EGLSurface pbuf;
    EGLImageKHR image;

    EGLBoolean verbose;
    EGLint major, minor;
};

EGL_STATE_T state, *p_state = &state;


void init_egl(EGL_STATE_T *state)
{
    EGLint num_configs;
    EGLBoolean result;

    //bcm_host_init();

    static const EGLint attribute_list[] =
	{
	    EGL_RED_SIZE, 5,
	    EGL_GREEN_SIZE, 6,
	    EGL_BLUE_SIZE, 5,
	    EGL_ALPHA_SIZE, 0,
	    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	    EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
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

     result = eglBindAPI(EGL_OPENVG_API);
    assert(EGL_FALSE != result);

    // create an EGL rendering context
    state->context = eglCreateContext(state->display, 
				      state->config, EGL_NO_CONTEXT, 
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
				0/*layer*/, &dst_rect, 0/*src*/,
				&src_rect, DISPMANX_PROTECTION_NONE, 
				0 /*alpha*/, 0/*clamp*/, 0/*transform*/);

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

    state->surface = eglCreateWindowSurface(state->display, 
					    state->config, 
					    nativewindow, NULL );
    assert(state->surface != EGL_NO_SURFACE);

    // connect the context to the surface
    result = eglMakeCurrent(state->display, state->surface, state->surface, state->context);
    assert(EGL_FALSE != result);
}

void simple_image() {
    VGImage img;
    img = vgCreateImage(VG_sL_8,
				256, 256,
				VG_IMAGE_QUALITY_NONANTIALIASED);
    if (img == VG_INVALID_HANDLE) {
	fprintf(stderr, "Can't create simple image\n");
	fprintf(stderr, "Error code %x\n", vgGetError());
	exit(2);
    }
    unsigned char val;
    unsigned char data[256*256];
    int n;
    
    for (n = 0; n < 256*256; n++) {
	val = (unsigned char) n;
	data[n] = val;
    }
    vgImageSubData(img, data,
		   256, VG_sL_8,
		   0, 0, 256, 256);
    vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
    vgLoadIdentity();
    vgTranslate(200, 30);

    vgDrawImage(img);

    vgDestroyImage(img);
}

// setfill sets the fill color
void setfill(float color[4]) {
    VGPaint fillPaint = vgCreatePaint();
    vgSetParameteri(fillPaint, VG_PAINT_TYPE, VG_PAINT_TYPE_COLOR);
    vgSetParameterfv(fillPaint, VG_PAINT_COLOR, 4, color);
    //vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_MULTIPLY);
    vgSetPaint(fillPaint, VG_FILL_PATH);
    vgDestroyPaint(fillPaint);
}

VGfloat drawn_color[4] = {0.0, 0.0, 1.0, 1.0};

void drawn_image() {
    EGLSurface img_surface;
    VGImage drawn_image;
    EGLContext context;

    static const EGLint attribute_list[] =
	{
            //EGL_COLOR_BUFFER_TYPE, EGL_LUMINANCE_BUFFER,
	    // EGL_LUMINANCE_SIZE, 8,
	    EGL_RED_SIZE, 8,
	    EGL_GREEN_SIZE, 8,
	    EGL_BLUE_SIZE, 8,
	    EGL_ALPHA_SIZE, 8,
	    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
	    EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
	    EGL_NONE
	};
    EGLConfig config;

    drawn_image = vgCreateImage(VG_sRGBA_8888,
				 256, 256,
				 VG_IMAGE_QUALITY_NONANTIALIASED);
    if (drawn_image == VG_INVALID_HANDLE) {
	fprintf(stderr, "Can't create drawn image\n");
	fprintf(stderr, "Error code %x\n", vgGetError());
	exit(2);
    }
    vgClearImage(drawn_image, 0, 0, 256, 256);

    int result, num_configs;
    result = eglChooseConfig(p_state->display,
			     attribute_list, 
			     &config, 
			     1, 
			     &num_configs);
    assert(EGL_FALSE != result);
    context = eglCreateContext(p_state->display, 
			       config, EGL_NO_CONTEXT, 
			       NULL);

    img_surface = eglCreatePbufferFromClientBuffer(p_state->display,
						   EGL_OPENVG_IMAGE,
						   (EGLClientBuffer) drawn_image,
						   config,
						   NULL);
    if (img_surface == EGL_NO_SURFACE) {
	fprintf(stderr, "Couldn't create pbuffer\n");
	fprintf(stderr, "Error code %x\n", eglGetError());
	exit(1);
    }
    eglMakeCurrent(p_state->display, img_surface, img_surface,
		   context); //p_state->context);

    VGPath path = vgCreatePath(VG_PATH_FORMAT_STANDARD, 
			       VG_PATH_DATATYPE_F, 1.0f, 
			       0.0f, 0, 0, VG_PATH_CAPABILITY_ALL);

    float height = 40.0;
    float arcW = 15.0;
    float arcH = 15.0;

    vguRoundRect(path, 28, 10, 
		 200, 60, arcW, arcH);
    vguEllipse(path, 128, 200, 60, 40);

    setfill(drawn_color);
    vgDrawPath(path, VG_FILL_PATH);

    eglMakeCurrent(p_state->display, p_state->surface, 
		   p_state->surface,
		   p_state->context);

    vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
    vgLoadIdentity();
    vgTranslate(800, 300);

    vgDrawImage(drawn_image);

    vgDestroyImage(drawn_image);
}

void draw(){
    float c = 1.0;
    float clearColor[4] = {c, c, c, c};  // white, no transparency
    vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
 
    vgClear(0, 0, 1920, 1080);

    simple_image();

    drawn_image();

    vgFlush();
}

int
main(int argc, char *argv[])
{ 
    EGL_DISPMANX_WINDOW_T nativewindow;

    init_egl(p_state);
    init_dispmanx(&nativewindow);
    egl_from_dispmanx(p_state, &nativewindow);

    draw();
    eglSwapBuffers(p_state->display, p_state->surface);

    sleep(100);

    eglTerminate(p_state->display);
    exit(0);
}
