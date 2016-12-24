#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <assert.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <VG/openvg.h>
#include <VG/vgu.h>

#include <bcm_host.h>

#include <gtk/gtk.h>

char *lines[] = {"hello", "world", "AWAKE"};
int num_lines = 3;

typedef struct
{
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    EGLConfig config;
} EGL_STATE_T;


EGL_STATE_T state, *p_state = &state;

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
				      state->config, EGL_NO_CONTEXT, 
				      NULL);

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
				0 /*layer*/, &dst_rect, 0 /*src*/,
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

    state->surface = eglCreateWindowSurface(state->display, 
					    state->config, 
					    nativewindow, NULL );
    assert(state->surface != EGL_NO_SURFACE);

    // connect the context to the surface
    result = eglMakeCurrent(state->display, state->surface, state->surface, state->context);
    assert(EGL_FALSE != result);
}

void cairo(char *text) {
    int width = 500;
    int height = 200;

    cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 
							   width, height);
    cairo_t *cr = cairo_create(surface);

    cairo_rectangle(cr, 0, 0, width, height);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.5);
    cairo_fill(cr);

    // draw some white text on top
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    // this is a standard font for Cairo
    cairo_select_font_face (cr, "cairo:serif",
			    CAIRO_FONT_SLANT_NORMAL, 
			    CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size (cr, 36);
    cairo_move_to(cr, 10.0, 50.0);

    cairo_scale(cr, 1.0f, -1.0f);
    cairo_translate(cr, 0.0f, -50);

    cairo_text_extents_t extents;
    cairo_text_extents(cr, text, &extents);
   
    cairo_show_text (cr, text);

    int tex_w = cairo_image_surface_get_width(surface);
    int tex_h = cairo_image_surface_get_height(surface);
    int tex_s = cairo_image_surface_get_stride(surface);
    unsigned char* data = cairo_image_surface_get_data(surface);

    int left = 1920/2 - extents.width/2;
    vgWritePixels(data, tex_s,
		  VG_lBGRA_8888, left, 300, tex_w, tex_h );
}
 
void draw() {
    EGL_DISPMANX_WINDOW_T nativewindow;

    init_egl(p_state);
    init_dispmanx(&nativewindow);

    egl_from_dispmanx(p_state, &nativewindow);

    int n = 0;
    while (1) {
	cairo(lines[n++]);
	n %= num_lines;

	eglSwapBuffers(p_state->display, p_state->surface);
	sleep(3);	
    }
}

int main(int argc, char** argv) {

    draw();

    exit(0);
}
