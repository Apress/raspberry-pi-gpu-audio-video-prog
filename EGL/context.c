
/*
 * code stolen from openGL-RPi-tutorial-master/encode_OGL/
 */

#include <stdio.h>
#include <assert.h>
#include <math.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

typedef struct
{
    uint32_t screen_width;
    uint32_t screen_height;

    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    EGLConfig config;
} EGL_STATE_T;


EGL_STATE_T state, *p_state = &state;

void init_egl(EGL_STATE_T *state)
{
    EGLBoolean result;
    EGLint num_configs;

    static const EGLint attribute_list[] =
	{
	    EGL_RED_SIZE, 8,
	    EGL_GREEN_SIZE, 8,
	    EGL_BLUE_SIZE, 8,
	    EGL_ALPHA_SIZE, 8,
	    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	    EGL_NONE
	};

    static const EGLint context_attributes[] =
	{
	    EGL_CONTEXT_CLIENT_VERSION, 2,
	    EGL_NONE
	};

    EGLConfig *configs;

    // get an EGL display connection
    state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    // initialize the EGL display connection
    result = eglInitialize(state->display, NULL, NULL);

    eglGetConfigs(state->display, NULL, 0, &num_configs);
    printf("EGL has %d configs\n", num_configs);

    configs = calloc(num_configs, sizeof *configs);
    eglGetConfigs(state->display, configs, num_configs, &num_configs);
    
    // get an appropriate EGL configuration - just use the first available
    result = eglChooseConfig(state->display, attribute_list, 
			     &state->config, 1, &num_configs);
    assert(EGL_FALSE != result);

    // Choose the OpenGL ES API
    result = eglBindAPI(EGL_OPENGL_ES_API);
    assert(EGL_FALSE != result);

    // create an EGL rendering context
    state->context = eglCreateContext(state->display, 
				      state->config, EGL_NO_CONTEXT, 
				      context_attributes);
    assert(state->context!=EGL_NO_CONTEXT);
    printf("Got an EGL context\n");
}

int
main(int argc, char *argv[])
{
    init_egl(p_state);

    return 0;
}
