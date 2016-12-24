#include <stdio.h>
#include <stdlib.h>

#include <OMX_Core.h>
#include <OMX_Component.h>

#include <bcm_host.h>

char *err2str(int err) {
    return "omitted";
}

void printState(OMX_HANDLETYPE handle) {
    OMX_STATETYPE state;
    OMX_ERRORTYPE err;

    err = OMX_GetState(handle, &state);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error on getting state\n");
        exit(1);
    }
    switch (state) {
    case OMX_StateLoaded: printf("StateLoaded\n"); break;
    case OMX_StateIdle: printf("StateIdle\n"); break;
    case OMX_StateExecuting: printf("StateExecuting\n"); break;
    case OMX_StatePause: printf("StatePause\n"); break;
    case OMX_StateWaitForResources: printf("StateWait\n"); break;
    case OMX_StateInvalid: printf("StateInvalid\n"); break;
    default:  printf("State unknown\n"); break;
    }
}

OMX_ERRORTYPE cEventHandler(
                            OMX_HANDLETYPE hComponent,
                            OMX_PTR pAppData,
                            OMX_EVENTTYPE eEvent,
                            OMX_U32 Data1,
                            OMX_U32 Data2,
                            OMX_PTR pEventData) {

    if(eEvent == OMX_EventCmdComplete) {
        if (Data1 == OMX_CommandStateSet) {
            printf("Component State changed to ");
            switch ((int)Data2) {
            case OMX_StateInvalid:
                printf("OMX_StateInvalid\n");
                break;
            case OMX_StateLoaded:
                printf("OMX_StateLoaded\n");
                break;
            case OMX_StateIdle:
                printf("OMX_StateIdle\n");
                break;
            case OMX_StateExecuting:
                printf("OMX_StateExecuting\n");
                break;
            case OMX_StatePause:
                printf("OMX_StatePause\n");
                break;
            case OMX_StateWaitForResources:
                printf("OMX_StateWaitForResources\n");
               break;
            }
        } else  if (Data1 == OMX_CommandPortEnable){
            printf("OMX State Port enabled %d\n", (int) Data2);
        } else if (Data1 == OMX_CommandPortDisable){
            printf("OMX State Port disabled %d\n", (int) Data2); 
        }
    } else if(eEvent == OMX_EventBufferFlag) {
        if((int)Data2 == OMX_BUFFERFLAG_EOS) {
	    printf("Event is buffer end of stream\n");
        }
    } else if(eEvent == OMX_EventError) {
      if (Data1 == OMX_ErrorSameState) {
        printf("Already in requested state\n");
      } else {
        printf("Event is Error %X\n", Data1);
      }
    } else  if(eEvent == OMX_EventMark) {
        printf("Event is Buffer Mark\n");
    } else  if(eEvent == OMX_EventPortSettingsChanged) {
        printf("Event is PortSettingsChanged\n");
    }

    return OMX_ErrorNone;
}

OMX_CALLBACKTYPE callbacks  = { .EventHandler = cEventHandler,
                                .EmptyBufferDone = NULL,
                                .FillBufferDone = NULL
};

void disableSomePorts(OMX_HANDLETYPE handle, OMX_INDEXTYPE indexType) {
    OMX_PORT_PARAM_TYPE param;
    int startPortNumber, endPortNumber;
    int nPorts;
    int n;
    OMX_ERRORTYPE err;

    //setHeader(&param, sizeof(OMX_PORT_PARAM_TYPE));

    memset(&param, 0, sizeof(OMX_PORT_PARAM_TYPE));
    param.nSize = sizeof(OMX_PORT_PARAM_TYPE);
    param.nVersion.nVersion = OMX_VERSION;

    err = OMX_GetParameter(handle, indexType, &param);
    if(err != OMX_ErrorNone){
        fprintf(stderr, "Error in getting image OMX_PORT_PARAM_TYPE parameter\n"
, 0);
        return;
    }

    startPortNumber = param.nStartPortNumber;
    nPorts = param.nPorts;
    endPortNumber = startPortNumber + nPorts;

    for (n = startPortNumber; n < endPortNumber; n++) {
	OMX_SendCommand(handle, OMX_CommandPortDisable,
			n, NULL);
    }
}

void disableAllPorts(OMX_HANDLETYPE handle) {
    disableSomePorts(handle, OMX_IndexParamVideoInit);
    disableSomePorts(handle, OMX_IndexParamImageInit);
    disableSomePorts(handle, OMX_IndexParamAudioInit);
    disableSomePorts(handle, OMX_IndexParamOtherInit);
}

int main(int argc, char** argv) {

    int i;
    char *componentName;
    OMX_ERRORTYPE err;
    OMX_HANDLETYPE handle;

    if (argc < 2) {
	fprintf(stderr, "Usage: %s component-name\n", argv[0]);
	exit(1);
    }
    componentName = argv[1];

    bcm_host_init();

    err = OMX_Init();
    if(err != OMX_ErrorNone) {
	fprintf(stderr, "OMX_Init() failed %s\n", err2str(err));
	exit(1);
    }
    /** Ask the core for a handle to the component
     */
    err = OMX_GetHandle(&handle, componentName,
                        // the next two fields are discussed later 
                        NULL, &callbacks);
    if (err != OMX_ErrorNone) {
	fprintf(stderr, "OMX_GetHandle failed %s\n", err2str(err));
	exit(1);
    }

    sleep(1);
    // check our current state - should be Loaded
    printState(handle);

    disableAllPorts(handle);

    // request a move to idle
    OMX_SendCommand(handle,
		    OMX_CommandStateSet, 
		    OMX_StateIdle,
		    NULL);

    sleep(2);
    printState(handle);
 
    // and to executing
    OMX_SendCommand(handle,
		    OMX_CommandStateSet, 
		    OMX_StateExecuting,
		    NULL);
    sleep(2);
    printState(handle);
 
    exit(0);
}
