 /*
 * code stolen from openGL-RPi-tutorial-master/encode_OGL/
 */

#include <stdio.h>
#include <assert.h>

#include <bcm_host.h>

typedef struct
{
    uint32_t screen_width;
    uint32_t screen_height;
    DISPMANX_DISPLAY_HANDLE_T dispman_display;
} DISPMANX_STATE_T;

DISPMANX_STATE_T state, *p_state = &state;

void init_dispmanx(DISPMANX_STATE_T *state) {
    int32_t success = 0;

    bcm_host_init();

    DISPMANX_ELEMENT_HANDLE_T dispman_element;
    
    DISPMANX_UPDATE_HANDLE_T dispman_update;
    VC_RECT_T dst_rect;
    VC_RECT_T src_rect;

    success = graphics_get_display_size(0 /* LCD */, 
					&state->screen_width, 
					&state->screen_height);
    assert( success >= 0 );

    printf("Screen height %d, width %d\n",
	   state->screen_height, state->screen_width);

    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.width = state->screen_width;
    dst_rect.height = state->screen_height;

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = state->screen_width << 16;
    src_rect.height = state->screen_height << 16;        

    state->dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
    dispman_update = vc_dispmanx_update_start( 0 );

    dispman_element = 
	vc_dispmanx_element_add(dispman_update, state->dispman_display,
				0/*layer*/, &dst_rect, 0/*src*/,
				&src_rect, DISPMANX_PROTECTION_NONE, 
				0 /*alpha*/, 0/*clamp*/, 0/*transform*/);

    /* Now we have created a native Dispmanx window
     * Toolkits such as OpenGL ES will use this for
     * their own 'native window' type, with code like this:

    static EGL_DISPMANX_WINDOW_T nativewindow;

    nativewindow.element = dispman_element;
    nativewindow.width = state->screen_width;
    nativewindow.height = state->screen_height;
    vc_dispmanx_update_submit_sync( dispman_update );
    assert(vc_dispmanx_element_remove(dispman_update, dispman_element) == 0);
    */
}

int
main(int argc, char *argv[])
{
    init_dispmanx(p_state);

    assert( vc_dispmanx_display_close(p_state->dispman_display) == 0);

    return 0;
}

