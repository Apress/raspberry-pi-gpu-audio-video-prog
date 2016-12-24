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

#include <glib.h>

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

// from /opt/vc/src/hello_pi/libs/vgfont/vgft.c

#define SEGMENTS_COUNT_MAX 256
#define COORDS_COUNT_MAX 1024

static VGuint segments_count;
static VGubyte segments[SEGMENTS_COUNT_MAX];
static VGuint coords_count;
static VGfloat coords[COORDS_COUNT_MAX];

static VGfloat float_from_26_6(FT_Pos x)
{
   return (VGfloat)x / 64.0f;
}


static void convert_contour(const FT_Vector *points, 
			    const char *tags, short points_count)
{
   int first_coords = coords_count;

   int first = 1;
   char last_tag = 0;
   int c = 0;

   for (; points_count != 0; ++points, ++tags, --points_count) {
      ++c;

      char tag = *tags;
      if (first) {
         assert(tag & 0x1);
         assert(c==1); c=0;
         segments[segments_count++] = VG_MOVE_TO;
         first = 0;
      } else if (tag & 0x1) {
         /* on curve */

         if (last_tag & 0x1) {
            /* last point was also on -- line */
            assert(c==1); c=0;
            segments[segments_count++] = VG_LINE_TO;
         } else {
            /* last point was off -- quad or cubic */
            if (last_tag & 0x2) {
               /* cubic */
               assert(c==3); c=0;
               segments[segments_count++] = VG_CUBIC_TO;
            } else {
               /* quad */
               assert(c==2); c=0;
               segments[segments_count++] = VG_QUAD_TO;
            }
         }
      } else {
         /* off curve */

         if (tag & 0x2) {
            /* cubic */

            assert((last_tag & 0x1) || (last_tag & 0x2)); /* last either on or off and cubic */
         } else {
            /* quad */

            if (!(last_tag & 0x1)) {
               /* last was also off curve */

               assert(!(last_tag & 0x2)); /* must be quad */

               /* add on point half-way between */
               assert(c==2); c=1;
               segments[segments_count++] = VG_QUAD_TO;
               VGfloat x = (coords[coords_count - 2] + float_from_26_6(points->x)) * 0.5f;
               VGfloat y = (coords[coords_count - 1] + float_from_26_6(points->y)) * 0.5f;
               coords[coords_count++] = x;
               coords[coords_count++] = y;
            }
         }
      }
      last_tag = tag;

      coords[coords_count++] = float_from_26_6(points->x);
      coords[coords_count++] = float_from_26_6(points->y);
   }

   if (last_tag & 0x1) {
      /* last point was also on -- line (implicit with close path) */
      assert(c==0);
   } else {
      ++c;

      /* last point was off -- quad or cubic */
      if (last_tag & 0x2) {
         /* cubic */
         assert(c==3); c=0;
         segments[segments_count++] = VG_CUBIC_TO;
      } else {
         /* quad */
         assert(c==2); c=0;
         segments[segments_count++] = VG_QUAD_TO;
      }
      coords[coords_count++] = coords[first_coords + 0];
      coords[coords_count++] = coords[first_coords + 1];
   }

   segments[segments_count++] = VG_CLOSE_PATH;
}

static void convert_outline(const FT_Vector *points, 
			    const char *tags, const short *contours, 
			    short contours_count, short points_count)
{
   segments_count = 0;
   coords_count = 0;

   short last_contour = 0;
   for (; contours_count != 0; ++contours, --contours_count) {
      short contour = *contours + 1;
      convert_contour(points + last_contour, tags + last_contour, contour - last_contour);
      last_contour = contour;
   }
   assert(last_contour == points_count);

   assert(segments_count <= SEGMENTS_COUNT_MAX); /* oops... we overwrote some me
mory */
   assert(coords_count <= COORDS_COUNT_MAX);
}


FT_Library  ft_library;   /* handle to library     */
FT_Face     ft_face;   

void ft_init() {
    int err;
    
    err = FT_Init_FreeType(&ft_library);
    assert( !err);

    err = FT_New_Face(ft_library,
		      //"/usr/share/fonts/truetype/freefont/FreeSansBold.ttf",
		      //"/usr/share/fonts/truetype/droid/DroidSansJapanese.ttf",
		      //"/usr/share/ghostscript/9.05/Resource/CIDFSubst/DroidSansFallback.ttf",
		      //"zysong.ttf",
		      "/usr/share/ghostscript/9.05/Resource/CIDFSubst/DroidSansFallback.ttf",
                     0,
                     &ft_face );
    assert( !err);

    /*
    err = FT_Set_Pixel_Sizes(ft_face, 
			     0,     
			     16 );  
    assert( !err);
    */

    int font_size = 256; //128;
    err = FT_Set_Pixel_Sizes(ft_face, 0, font_size);
    assert( !err);
}

VGFont font, font_border;
GHashTable *glyphs_seen = NULL;

void add_glyph(int *ch) {
    if (g_hash_table_lookup(glyphs_seen, GINT_TO_POINTER(ch)) != NULL) {
	printf("Glyph %d already seen\n", *ch);
	return;
    }
    g_hash_table_insert(glyphs_seen, 
			GINT_TO_POINTER(ch), 
			GINT_TO_POINTER(ch));
    printf("Inserting %d\n", *ch);

    int glyph_index = FT_Get_Char_Index(ft_face, *ch);
    if (glyph_index == 0) printf("No glyph found\n");
    FT_Load_Glyph(ft_face, glyph_index, 
		  FT_LOAD_NO_HINTING| FT_LOAD_LINEAR_DESIGN); // | FT_LOAD_NO_SCALE);
    
    VGPath vg_path;
    
    FT_Outline *outline = &ft_face->glyph->outline;
    if (outline->n_contours != 0) {
	vg_path = vgCreatePath(VG_PATH_FORMAT_STANDARD, 
			       VG_PATH_DATATYPE_F, 1.0f, 
			       0.0f, 0, 0, VG_PATH_CAPABILITY_ALL);
	assert(vg_path != VG_INVALID_HANDLE);
	
	convert_outline(outline->points, outline->tags, 
			outline->contours, outline->n_contours, 
			outline->n_points);
	vgAppendPathData(vg_path, segments_count, segments, coords);
    } else {
	vg_path = VG_INVALID_HANDLE;
    }

    VGfloat glyphOrigin[2] = {0.0f, 0.0f};
    printf("Width %d, height %d advance %d\n", 
	   ft_face->glyph->metrics.width,
	   ft_face->glyph->metrics.height,
	   ft_face->glyph->linearHoriAdvance);
    VGfloat escapement[2] = {ft_face->glyph->linearHoriAdvance, //ft_face->glyph->metrics.width/53.2, // 2*26.6
			     0.0f}; //ft_face->glyph->metrics.height}; //{300.0f, 0.0f};

    vgSetGlyphToPath(font, *ch,
		     vg_path, 0,
		     glyphOrigin,
		     escapement);
}

void draw() { 
    EGL_DISPMANX_WINDOW_T nativewindow;
    glyphs_seen = g_hash_table_new(g_int_hash, g_int_equal);

    init_egl(p_state);
    init_dispmanx(&nativewindow);

    egl_from_dispmanx(p_state, &nativewindow);
 
    vgClear(0, 0, 1920, 1080);

    ft_init();

    font = vgCreateFont(0);
    //font_border = vgCreateFont(0);
    
    VGfloat strokeColor[4] = {0.4, 0.1, 1.0, 1.0}; // purple
    VGfloat fillColor[4] = {1.0, 1.0, 0.0, 1.0}; // yellowish

    setfill(fillColor);
    setstroke(strokeColor, 5);

    VGfloat origin[2] = {300.0f, 300.0f};
    vgSetfv(VG_GLYPH_ORIGIN, 2, origin);

    int ch;

    ch = 0x597d;
    add_glyph(&ch);
    vgDrawGlyph(font, ch,
		VG_STROKE_PATH,
		VG_FALSE);

    ch = 'A';
    add_glyph(&ch);
    vgDrawGlyph(font, ch,
		VG_STROKE_PATH,
		VG_FALSE);

    ch = 'b';
    add_glyph(&ch);
    vgDrawGlyph(font, ch,
		VG_STROKE_PATH | VG_FILL_PATH,
		VG_FALSE);
    ch = 'g';
    add_glyph(&ch);
    vgDrawGlyph(font, ch,
		VG_STROKE_PATH | VG_FILL_PATH,
		VG_FALSE);
    ch = 'b';    
    add_glyph(&ch);
    vgDrawGlyph(font, ch,
		VG_STROKE_PATH,
		VG_FALSE);

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
