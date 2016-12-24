#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <OMX_Core.h>
#include <OMX_Component.h>

#include <bcm_host.h>
#include <vcos_logging.h>

#define VCOS_LOG_CATEGORY (&il_ffmpeg_log_category)
static VCOS_LOG_CAT_T il_ffmpeg_log_category;

#include <ilclient.h>

#include "libavcodec/avcodec.h"
#include <libavformat/avformat.h>
#include "libavutil/mathematics.h"
#include "libavutil/samplefmt.h"

#define INBUF_SIZE 4096
#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096

char *IMG = "taichi.mp4";

static AVCodecContext *video_dec_ctx = NULL;
static AVStream *video_stream = NULL;
static AVPacket pkt;
AVFormatContext *pFormatCtx = NULL;

static int video_stream_idx = -1;

uint8_t extradatasize;
void *extradata;

AVCodec *codec;

void printState(OMX_HANDLETYPE handle) {
    OMX_STATETYPE state;
    OMX_ERRORTYPE err;

    err = OMX_GetState(handle, &state);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error on getting state\n");
        exit(1);
    }
    switch (state) {
    case OMX_StateLoaded:           printf("StateLoaded\n"); break;
    case OMX_StateIdle:             printf("StateIdle\n"); break;
    case OMX_StateExecuting:        printf("StateExecuting\n"); break;
    case OMX_StatePause:            printf("StatePause\n"); break;
    case OMX_StateWaitForResources: printf("StateWait\n"); break;
    case OMX_StateInvalid:          printf("StateInvalid\n"); break;
    default:                        printf("State unknown\n"); break;
    }
}

char *err2str(int err) {
    switch (err) {
    case OMX_ErrorInsufficientResources: return "OMX_ErrorInsufficientResources";
    case OMX_ErrorUndefined: return "OMX_ErrorUndefined";
    case OMX_ErrorInvalidComponentName: return "OMX_ErrorInvalidComponentName";
    case OMX_ErrorComponentNotFound: return "OMX_ErrorComponentNotFound";
    case OMX_ErrorInvalidComponent: return "OMX_ErrorInvalidComponent";
    case OMX_ErrorBadParameter: return "OMX_ErrorBadParameter";
    case OMX_ErrorNotImplemented: return "OMX_ErrorNotImplemented";
    case OMX_ErrorUnderflow: return "OMX_ErrorUnderflow";
    case OMX_ErrorOverflow: return "OMX_ErrorOverflow";
    case OMX_ErrorHardware: return "OMX_ErrorHardware";
    case OMX_ErrorInvalidState: return "OMX_ErrorInvalidState";
    case OMX_ErrorStreamCorrupt: return "OMX_ErrorStreamCorrupt";
    case OMX_ErrorPortsNotCompatible: return "OMX_ErrorPortsNotCompatible";
    case OMX_ErrorResourcesLost: return "OMX_ErrorResourcesLost";
    case OMX_ErrorNoMore: return "OMX_ErrorNoMore";
    case OMX_ErrorVersionMismatch: return "OMX_ErrorVersionMismatch";
    case OMX_ErrorNotReady: return "OMX_ErrorNotReady";
    case OMX_ErrorTimeout: return "OMX_ErrorTimeout";
    case OMX_ErrorSameState: return "OMX_ErrorSameState";
    case OMX_ErrorResourcesPreempted: return "OMX_ErrorResourcesPreempted";
    case OMX_ErrorPortUnresponsiveDuringAllocation: return "OMX_ErrorPortUnresponsiveDuringAllocation";
    case OMX_ErrorPortUnresponsiveDuringDeallocation: return "OMX_ErrorPortUnresponsiveDuringDeallocation";
    case OMX_ErrorPortUnresponsiveDuringStop: return "OMX_ErrorPortUnresponsiveDuringStop";
    case OMX_ErrorIncorrectStateTransition: return "OMX_ErrorIncorrectStateTransition";
    case OMX_ErrorIncorrectStateOperation: return "OMX_ErrorIncorrectStateOperation";
    case OMX_ErrorUnsupportedSetting: return "OMX_ErrorUnsupportedSetting";
    case OMX_ErrorUnsupportedIndex: return "OMX_ErrorUnsupportedIndex";
    case OMX_ErrorBadPortIndex: return "OMX_ErrorBadPortIndex";
    case OMX_ErrorPortUnpopulated: return "OMX_ErrorPortUnpopulated";
    case OMX_ErrorComponentSuspended: return "OMX_ErrorComponentSuspended";
    case OMX_ErrorDynamicResourcesUnavailable: return "OMX_ErrorDynamicResourcesUnavailable";
    case OMX_ErrorMbErrorsInFrame: return "OMX_ErrorMbErrorsInFrame";
    case OMX_ErrorFormatNotDetected: return "OMX_ErrorFormatNotDetected";
    case OMX_ErrorContentPipeOpenFailed: return "OMX_ErrorContentPipeOpenFailed";
    case OMX_ErrorContentPipeCreationFailed: return "OMX_ErrorContentPipeCreationFailed";
    case OMX_ErrorSeperateTablesUsed: return "OMX_ErrorSeperateTablesUsed";
    case OMX_ErrorTunnelingUnsupported: return "OMX_ErrorTunnelingUnsupported";
    default: return "unknown error";
    }
}

void eos_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data) {
    printf("Got eos event\n");
}

void error_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data) {
    printf("OMX error %s\n", err2str(data));
}

void port_settings_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data) {
    printf("Got port Settings event\n");
    // exit(0);
}

void empty_buffer_done_callback(void *userdata, COMPONENT_T *comp) {
    printf("Got empty buffer done\n");
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

#ifdef OMX_SKIP64BIT
OMX_TICKS ToOMXTime(int64_t pts)
{
  OMX_TICKS ticks;
  ticks.nLowPart = pts;
  ticks.nHighPart = pts >> 32;
  return ticks;
}
#else
#define FromOMXTime(x) (x)
#endif

OMX_ERRORTYPE copy_into_buffer_and_empty(AVPacket *pkt,
					 COMPONENT_T *component,
					 OMX_BUFFERHEADERTYPE *buff_header) {
    OMX_ERRORTYPE r;

    int buff_size = buff_header->nAllocLen;
    int size = pkt->size;

    if (size < buff_size) {
	memcpy((unsigned char *)buff_header->pBuffer, 
	       pkt->data, size);
    } else {
	printf("Buffer not big enough %d %d\n", buff_size, size);
	return -1;
    }
	
    buff_header->nFilledLen = size;
    buff_header->nFlags = 0;
    buff_header->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
    if (pkt->dts == 0) {
	buff_header->nFlags |= OMX_BUFFERFLAG_STARTTIME;
    } else {
	printf("DTS is %s %ld\n", "str", pkt->dts);
	//buff_header->nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
	buff_header->nTimeStamp = ToOMXTime((uint64_t)(pkt->dts));
    }

    r = OMX_EmptyThisBuffer(ilclient_get_handle(component),
			    buff_header);
    if (r != OMX_ErrorNone) {
	fprintf(stderr, "Empty buffer error %s\n",
		err2str(r));
    } else {
	printf("Emptying buffer %p\n", buff_header);
    }
    return r;
}

int img_width, img_height;

int SendDecoderConfig(COMPONENT_T *component)
{
  OMX_ERRORTYPE omx_err   = OMX_ErrorNone;

  /* send decoder config */
  if(extradatasize > 0 && extradata != NULL)
  {
      //fwrite(extradata, 1, extradatasize, out);

      OMX_BUFFERHEADERTYPE *omx_buffer = ilclient_get_input_buffer(component,
					  130,
					  1 /* block */);

    if(omx_buffer == NULL)
    {
	fprintf(stderr, "%s - buffer error 0x%08x", __func__, omx_err);
      return 0;
    }

    omx_buffer->nOffset = 0;
    omx_buffer->nFilledLen = extradatasize;
    if(omx_buffer->nFilledLen > omx_buffer->nAllocLen)
    {
	fprintf(stderr, "%s - omx_buffer->nFilledLen > omx_buffer->nAllocLen",  __func__);
      return 0;
    }

    memset((unsigned char *)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
    memcpy((unsigned char *)omx_buffer->pBuffer, extradata, omx_buffer->nFilledLen);
    omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
  
    omx_err =  OMX_EmptyThisBuffer(ilclient_get_handle(component),
				   omx_buffer);
    if (omx_err != OMX_ErrorNone)
    {
	fprintf(stderr, "%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", __func__, omx_err);
      return 0;
    } else {
	printf("Config sent, emptying buffer %d\n", extradatasize);
    }
  }
  return 1;
}

OMX_ERRORTYPE set_video_decoder_input_format(COMPONENT_T *component) {
    int err;

    // set input video format
    printf("Setting video decoder format\n");
    OMX_VIDEO_PARAM_PORTFORMATTYPE videoPortFormat;

    memset(&videoPortFormat, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
    videoPortFormat.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
    videoPortFormat.nVersion.nVersion = OMX_VERSION;
    videoPortFormat.nPortIndex = 130;

    err = OMX_GetParameter(ilclient_get_handle(component),
			   OMX_IndexParamVideoPortFormat, &videoPortFormat);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error getting video decoder format %s\n", err2str(err));
        return err;
    }

    videoPortFormat.nPortIndex = 130;
    videoPortFormat.nIndex = 0;
    videoPortFormat.eCompressionFormat = OMX_VIDEO_CodingAVC;
    videoPortFormat.eColorFormat = OMX_COLOR_FormatUnused;
    videoPortFormat.xFramerate = 0;

    err = OMX_SetParameter(ilclient_get_handle(component),
			   OMX_IndexParamVideoPortFormat, &videoPortFormat);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error setting video decoder format %s\n", err2str(err));
        return err;
    } else {
        printf("Video decoder format set up ok\n");
    }

  OMX_PARAM_PORTDEFINITIONTYPE portParam;
  memset(&portParam, 0, sizeof( OMX_PARAM_PORTDEFINITIONTYPE));
    portParam.nSize = sizeof( OMX_PARAM_PORTDEFINITIONTYPE);
    portParam.nVersion.nVersion = OMX_VERSION;

  portParam.nPortIndex = 130;

  err =  OMX_GetParameter(ilclient_get_handle(component),
			  OMX_IndexParamPortDefinition, &portParam);
  if(err != OMX_ErrorNone)
  {
      fprintf(stderr, "COMXVideo::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", err);
    return err;
  }

  portParam.nPortIndex = 130;

  portParam.format.video.nFrameWidth  = img_width;
  portParam.format.video.nFrameHeight = img_height;

  err =  OMX_SetParameter(ilclient_get_handle(component),
			  OMX_IndexParamPortDefinition, &portParam);
  if(err != OMX_ErrorNone)
  {
      fprintf(stderr, "COMXVideo::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", err);
    return err;
  }

  return OMX_ErrorNone;
}

int setup_demuxer(const char *filename) {
    // Register all formats and codecs
    av_register_all();
    if(avformat_open_input(&pFormatCtx, filename, NULL, NULL)!=0) {
	fprintf(stderr, "Can't get format\n");
        return -1; // Couldn't open file
    }
    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
	return -1; // Couldn't find stream information
    }
    printf("Format:\n");
    av_dump_format(pFormatCtx, 0, filename, 0);

    int ret;
    ret = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (ret >= 0) {
	video_stream_idx = ret;

	video_stream = pFormatCtx->streams[video_stream_idx];
	video_dec_ctx = video_stream->codec;

	img_width         = video_stream->codec->width;
	img_height        = video_stream->codec->height;
	extradata         = video_stream->codec->extradata;
	extradatasize     = video_stream->codec->extradata_size;

	AVCodec *codec = avcodec_find_decoder(video_stream->codec->codec_id);
	
	if (codec) {
	    printf("Codec name %s\n", codec->name);
	}
    }
    return 0;
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



int main(int argc, char** argv) {

    char *decodeComponentName;
    char *renderComponentName;

    int err;
    ILCLIENT_T  *handle;
    COMPONENT_T *decodeComponent;
    COMPONENT_T *renderComponent;

    if (argc > 1) {
	IMG = argv[1];
    }

    OMX_BUFFERHEADERTYPE *buff_header;

    setup_demuxer(IMG);

    decodeComponentName = "video_decode";
    renderComponentName = "video_render";

    bcm_host_init();

    handle = ilclient_init();
    vcos_log_set_level(VCOS_LOG_CATEGORY, VCOS_LOG_TRACE);
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
    ilclient_set_port_settings_callback(handle,
			      port_settings_callback,
			      NULL);
    ilclient_set_empty_buffer_done_callback(handle,
			      empty_buffer_done_callback,
			      NULL);


    setup_decodeComponent(handle, decodeComponentName, &decodeComponent);
    setup_renderComponent(handle, renderComponentName, &renderComponent);

    // both components now in Idle state, no buffers, ports disabled

    // input port
    err = ilclient_enable_port_buffers(decodeComponent, 130, 
				 NULL, NULL, NULL);
    if (err < 0) {
	fprintf(stderr, "Couldn't enable buffers\n");
	exit(1);
    }
    ilclient_enable_port(decodeComponent, 130);

    err = ilclient_change_component_state(decodeComponent,
					  OMX_StateExecuting);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Executing\n");
	exit(1);
    }
    printState(ilclient_get_handle(decodeComponent));
 
    SendDecoderConfig(decodeComponent);


    /* read frames from the file */
    while (av_read_frame(pFormatCtx, &pkt) >= 0) {
	printf("Read pkt\n");

	AVPacket orig_pkt = pkt;
	if (pkt.stream_index == video_stream_idx) {
	    printf("  read video pkt %d\n", pkt.size);

	    buff_header = 
		ilclient_get_input_buffer(decodeComponent,
					  130,
					  1 /* block */);
	    if (buff_header != NULL) {
		copy_into_buffer_and_empty(&pkt,
					   decodeComponent,
					   buff_header);
	    } else {
		fprintf(stderr, "Couldn't get a buffer\n");
	    }


	    err = ilclient_wait_for_event(decodeComponent, 
					 OMX_EventPortSettingsChanged, 
					 131, 0, 0, 1,
					 ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 
					 0);
	    if (err < 0) {
		printf("No port settings change\n");
		//exit(1);
	    } else {
		printf("Port settings changed\n");
		// exit(0);
		break;
	    }


	    if (ilclient_remove_event(decodeComponent, 
				      OMX_EventPortSettingsChanged, 
				      131, 0, 0, 1) == 0) {
		printf("Removed port settings event\n");
		//exit(0);
		break;
	    } else {
		printf("No portr settting seen yet\n");
	    }
	}
	av_free_packet(&orig_pkt);
    }

    // set up the tunnel between decode and render ports
    TUNNEL_T tunnel;
    set_tunnel(&tunnel, decodeComponent, 131, renderComponent, 90);
    if ((err = ilclient_setup_tunnel(&tunnel, 0, 0)) < 0) {
	fprintf(stderr, "Error setting up tunnel %X\n", err);
	exit(1);
    } else {
	printf("Decode tunnel set up ok\n");
    }	

    // Okay to go back to processing data
    // enable the decode output ports
   
    OMX_SendCommand(ilclient_get_handle(decodeComponent), 
		    OMX_CommandPortEnable, 131, NULL);
   
    ilclient_enable_port(decodeComponent, 131);

    // enable the render input ports
   
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
    while (av_read_frame(pFormatCtx, &pkt) >= 0) {
	printf("Read pkt\n");
	
	if (pkt.stream_index != video_stream_idx) {
	    continue;
	}

	// do we have a decode input buffer we can fill and empty?
	buff_header = 
	    ilclient_get_input_buffer(decodeComponent,
				      130,
				      1 /* block */);
	if (buff_header != NULL) {
	    copy_into_buffer_and_empty(&pkt,
				       decodeComponent,
				       buff_header);
	}
    }

    ilclient_wait_for_event(renderComponent, 
			    OMX_EventBufferFlag, 
			    90, 0, OMX_BUFFERFLAG_EOS, 0,
			    ILCLIENT_BUFFER_FLAG_EOS, 10000);
    printf("EOS on render\n");

    exit(0);
}
