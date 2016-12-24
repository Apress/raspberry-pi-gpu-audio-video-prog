#include <stdio.h>
#include <stdlib.h>
//#include <sys/stat.h>
#include <signal.h>

#include <assert.h>
#include <vc_dispmanx.h>
#include <bcm_host.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <VG/openvg.h>
#include <VG/vgu.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H

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

FT_Library  ft_library;   /* handle to library     */
FT_Face     ft_face;   

void ft_init() {
    int err;
    
    err = FT_Init_FreeType(&ft_library);
    assert( !err);

    err = FT_New_Face(ft_library,
		      "/usr/share/ghostscript/9.05/Resource/CIDFSubst/DroidSansFallback.ttf",
                     0,
                     &ft_face );
    assert( !err);

    int font_size = 256; //128;
    err = FT_Set_Pixel_Sizes(ft_face, 0, font_size);
    assert( !err);
}

void draw() { 
    EGL_DISPMANX_WINDOW_T nativewindow;

    init_egl(p_state);
    init_dispmanx(&nativewindow);

    egl_from_dispmanx(p_state, &nativewindow);
 
    vgClear(0, 0, 1920, 1080);

    VGFont font, font_border;
    font = vgCreateFont(0);
    font_border = vgCreateFont(0);

    ft_init();

    int glyph_index = FT_Get_Char_Index(ft_face, 0x597d); // '好'
    if (glyph_index == 0) printf("No glyph found\n");
    FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_NO_HINTING);

    FT_Glyph glyph;    
    FT_Get_Glyph(ft_face->glyph, &glyph);

    FT_Stroker ft_stroker;
    FT_Stroker_New(ft_library, &ft_stroker);
    FT_Stroker_Set(ft_stroker,
		   200, // line_height_*border_thickness*64.0f,
		   FT_STROKER_LINECAP_ROUND,
		   FT_STROKER_LINEJOIN_ROUND,
		   0);
    FT_Glyph_StrokeBorder(&glyph, ft_stroker, 0, 1);
    
    FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, NULL, 1);
    FT_BitmapGlyph bit_glyph = (FT_BitmapGlyph) glyph;
    FT_Bitmap bitmap = bit_glyph->bitmap;
    printf("Bitmap mode is %d (2 is gray) with %d levels\n", 
	   bitmap.pixel_mode, bitmap.num_grays);

    int tex_s = bitmap.pitch;
    int tex_w = bitmap.width;
    int tex_h = bitmap.rows;

    VGImage image = vgCreateImage(VG_sL_8, tex_w, tex_h, 
				  VG_IMAGE_QUALITY_NONANTIALIASED);

    // invert bitmap image
    vgImageSubData(image,
		   bitmap.buffer + tex_s*(tex_h-1),
		   -tex_s,
		   VG_sL_8, //VG_A_8, VG_sL_8 seem to be okay too
		   0, 0, tex_w, tex_h);

    vgSetPixels(600, 600, image, 0, 0, tex_w, tex_h);

    vgFlush();

    eglSwapBuffers(p_state->display, p_state->surface);
}

void sig_handler(int sig) {
    eglTerminate(p_state->display);
    exit(1);
}

int main(int argc, char** argv) {
    signal(SIGINT, sig_handler);

    draw();

    sleep(100);
    exit(0);
}
