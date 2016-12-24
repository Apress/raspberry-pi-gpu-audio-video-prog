
/*
 * code stolen from openGL-RPi-tutorial-master/encode_OGL/
 */

#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <unistd.h>

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

void printConfigInfo(int n, EGLDisplay display, EGLConfig *config) {
    int size;

    printf("Configuration %d is\n", n);

    eglGetConfigAttrib(display,
		       *config, EGL_RED_SIZE, &size);
    printf("  Red size is %d\n", size);
    eglGetConfigAttrib(display,
		       *config, EGL_BLUE_SIZE, &size);
    printf("  Blue size is %d\n", size);
    eglGetConfigAttrib(display,
		       *config, EGL_GREEN_SIZE, &size);
    printf("  Green size is %d\n", size);
    eglGetConfigAttrib(display,
		       *config, EGL_BUFFER_SIZE, &size);
    printf("  Buffer size is %d\n", size);

   eglGetConfigAttrib(display,
		       *config,  EGL_BIND_TO_TEXTURE_RGB , &size);
   if (size == EGL_TRUE)
       printf("  Can be bound to RGB texture\n");
   else
       printf("  Can't be bound to RGB texture\n");

   eglGetConfigAttrib(display,
		       *config,  EGL_BIND_TO_TEXTURE_RGBA , &size);
   if (size == EGL_TRUE)
       printf("  Can be bound to RGBA texture\n");
   else
       printf("  Can't be bound to RGBA texture\n");
}

void init_egl(EGL_STATE_T *state)
{
    EGLBoolean result;
    EGLint num_configs;

    EGLConfig *configs;

    // get an EGL display connection
    state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    // initialize the EGL display connection
    result = eglInitialize(state->display, NULL, NULL);
    if (result == EGL_FALSE) {
	fprintf(stderr, "Can't initialise EGL\n");
	exit(1);
    }

    eglGetConfigs(state->display, NULL, 0, &num_configs);
    printf("EGL has %d configs\n", num_configs);

    configs = calloc(num_configs, sizeof *configs);
    eglGetConfigs(state->display, configs, num_configs, &num_configs);
    
    int i;
    for (i = 0; i < num_configs; i++) {
	printConfigInfo(i, state->display, &configs[i]);
    }
}

int
main(int argc, char *argv[])
{
    init_egl(p_state);

    return 0;
}
