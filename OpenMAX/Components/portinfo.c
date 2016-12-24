
#include <stdio.h>
#include <stdlib.h>

#include <OMX_Core.h>
#include <OMX_Component.h>

#include <bcm_host.h>

// OMX_ERRORTYPE err;

OMX_ERRORTYPE get_port_info(OMX_HANDLETYPE handle,
			    OMX_PARAM_PORTDEFINITIONTYPE *portdef) {
    return  OMX_GetParameter(handle, 
			     OMX_IndexParamPortDefinition, 
			     portdef);

}

void print_port_info(OMX_PARAM_PORTDEFINITIONTYPE *portdef) {
    char *domain;

    printf("Port %d\n", portdef->nPortIndex);
    if (portdef->eDir ==  OMX_DirInput) {
	printf("  is input port\n");
    } else {
	printf("  is output port\n");
    }

    switch (portdef->eDomain) {
    case OMX_PortDomainAudio: domain = "Audio"; break;
    case OMX_PortDomainVideo: domain = "Video"; break;
    case OMX_PortDomainImage: domain = "Image"; break;
    case OMX_PortDomainOther: domain = "Other"; break;
    }
    printf("  Domain is %s\n", domain);

    printf("  Buffer count %d\n", portdef->nBufferCountActual);
    printf("  Buffer minimum count %d\n", portdef->nBufferCountMin);
    printf("  Buffer size %d bytes\n", portdef->nBufferSize);
}

OMX_CALLBACKTYPE callbacks  = { .EventHandler = NULL,
                                .EmptyBufferDone = NULL,
                                .FillBufferDone = NULL
};

int main(int argc, char** argv) {

    int i;
    char componentName[128]; // min space required see /opt/vc/include/IL/OMX_Core.h
                             // thanks to Peter Maersk-Moller
    OMX_ERRORTYPE err;
    OMX_HANDLETYPE handle;
    OMX_PARAM_PORTDEFINITIONTYPE portdef;
    OMX_VERSIONTYPE specVersion, compVersion;
    OMX_UUIDTYPE uid;
    int portindex;

    if (argc < 3) {
	fprintf(stderr, "Usage: %s component-name port-index\n", argv[0]);
	exit(1);
    }
    strncpy(componentName, argv[1], 128);
    portindex = atoi(argv[2]);

    bcm_host_init();

    err = OMX_Init();
    if(err != OMX_ErrorNone) {
	fprintf(stderr, "OMX_Init() failed\n", 0);
	exit(1);
    }
    /** Ask the core for a handle to the component
     */
    err = OMX_GetHandle(&handle, componentName,
			// the next two fields are discussed later 
			NULL, &callbacks);
    if (err != OMX_ErrorNone) {
	fprintf(stderr, "OMX_GetHandle failed\n", 0);
	exit(1);
    }

    // Get some version info
    err = OMX_GetComponentVersion(handle, componentName, 
				  &compVersion, &specVersion, 
				  &uid);
    if (err != OMX_ErrorNone) {
	fprintf(stderr, "OMX_GetComponentVersion failed\n", 0);
	exit(1);
    }
    printf("Component name: %s version %d.%d, Spec version %d.%d\n",
	   componentName, compVersion.s.nVersionMajor,
	   compVersion.s.nVersionMinor,
	   specVersion.s.nVersionMajor,
	   specVersion.s.nVersionMinor);

    memset(&portdef, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
    portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
    portdef.nVersion.nVersion = OMX_VERSION;
    portdef.nPortIndex = portindex;

    if (get_port_info(handle, &portdef) == OMX_ErrorNone) {
	print_port_info(&portdef);
    }
 
    exit(0);
}
