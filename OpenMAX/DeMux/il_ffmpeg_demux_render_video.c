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
    // elided
}

char *err2str(int err) {
    return "error elided";
}

void printClockState(COMPONENT_T *clockComponent) {
    OMX_ERRORTYPE err = OMX_ErrorNone;
    OMX_TIME_CONFIG_CLOCKSTATETYPE clockState;

    memset(&clockState, 0, sizeof( OMX_TIME_CONFIG_CLOCKSTATETYPE));
    clockState.nSize = sizeof( OMX_TIME_CONFIG_CLOCKSTATETYPE);
    clockState.nVersion.nVersion = OMX_VERSION;

    err = OMX_GetConfig(ilclient_get_handle(clockComponent), 
			OMX_IndexConfigTimeClockState, &clockState);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error getting clock state %s\n", err2str(err));
        return;
    }
    switch (clockState.eState) {
    case OMX_TIME_ClockStateRunning:
	printf("Clock running\n");
	break;
    case OMX_TIME_ClockStateWaitingForStartTime:
	printf("Clock waiting for start time\n");
	break;
    case OMX_TIME_ClockStateStopped:
	printf("Clock stopped\n");
	break;
    default:
	printf("Clock in other state\n");
    }
}

void startClock(COMPONENT_T *clockComponent) {
    OMX_ERRORTYPE err = OMX_ErrorNone;
    OMX_TIME_CONFIG_CLOCKSTATETYPE clockState;

    memset(&clockState, 0, sizeof( OMX_TIME_CONFIG_CLOCKSTATETYPE));
    clockState.nSize = sizeof( OMX_TIME_CONFIG_CLOCKSTATETYPE);
    clockState.nVersion.nVersion = OMX_VERSION;

    err = OMX_GetConfig(ilclient_get_handle(clockComponent), 
			OMX_IndexConfigTimeClockState, &clockState);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error getting clock state %s\n", err2str(err));
        return;
    }
    clockState.eState = OMX_TIME_ClockStateRunning;
    err = OMX_SetConfig(ilclient_get_handle(clockComponent), 
			OMX_IndexConfigTimeClockState, &clockState);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error starting clock %s\n", err2str(err));
        return;
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

unsigned int fpsscale;
unsigned int fpsrate;
unsigned int time_base_num;
unsigned int time_base_den;

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
    uint8_t *content = pkt->data;

    while (size > 0) {
	buff_header->nFilledLen = (size > buff_header->nAllocLen-1) ?
	    buff_header->nAllocLen-1 : size;
	memset(buff_header->pBuffer, 0x0, buff_header->nAllocLen);
	memcpy(buff_header->pBuffer, content, buff_header->nFilledLen);
	size -= buff_header->nFilledLen;
	content += buff_header->nFilledLen;
    

	/*
	if (size < buff_size) {
	    memcpy((unsigned char *)buff_header->pBuffer, 
		   pkt->data, size);
	} else {
	    printf("Buffer not big enough %d %d\n", buff_size, size);
	    return -1;
	}
	
	buff_header->nFilledLen = size;
	*/

	buff_header->nFlags = 0;
	if (size <= 0) 
	    buff_header->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

	printf("  DTS is %s %ld\n", "str", pkt->dts);
	printf("  PTS is %s %ld\n", "str", pkt->pts);

	if (pkt->dts == 0) {
	    buff_header->nFlags |= OMX_BUFFERFLAG_STARTTIME;
	} else {
	    buff_header->nTimeStamp = ToOMXTime((uint64_t)
						(pkt->pts * 1000000/
						 time_base_den));

	    printf("Time stamp %d\n", 	buff_header->nTimeStamp);
	}

	r = OMX_EmptyThisBuffer(ilclient_get_handle(component),
				buff_header);
	if (r != OMX_ErrorNone) {
	    fprintf(stderr, "Empty buffer error %s\n",
		    err2str(r));
	} else {
	    printf("Emptying buffer %p\n", buff_header);
	}
	if (size > 0) {
	     buff_header = 
		ilclient_get_input_buffer(component,
					  130,
					  1 /* block */);
	}
    }
    return r;
}

int img_width, img_height;

int SendDecoderConfig(COMPONENT_T *component, FILE *out)
{
    OMX_ERRORTYPE omx_err   = OMX_ErrorNone;

    /* send decoder config */
    if(extradatasize > 0 && extradata != NULL)
	{
	    fwrite(extradata, 1, extradatasize, out);

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

#if 1 // doesn't seem to make any difference!!!
    if (fpsscale > 0 && fpsrate > 0) {
	videoPortFormat.xFramerate = 
	    (long long)(1<<16)*fpsrate / fpsscale;
    } else {
	videoPortFormat.xFramerate = 25 * (1<<16);
    }
    printf("FPS num %d den %d\n", fpsrate, fpsscale);
    printf("Set frame rate to %d\n", videoPortFormat.xFramerate);
#endif
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

    printf("Default framerate %d\n", portParam.format.video.xFramerate);

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
	fpsscale          = video_stream->r_frame_rate.den;
	fpsrate           = video_stream->r_frame_rate.num;
	time_base_num         = video_stream->time_base.num;
	time_base_den         = video_stream->time_base.den;

	printf("Rate %d scale %d time base %d %d\n",
	       video_stream->r_frame_rate.num,
	       video_stream->r_frame_rate.den,
	       video_stream->time_base.num,
	       video_stream->time_base.den);

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

void setup_schedulerComponent(ILCLIENT_T  *handle, 
			      char *schedulerComponentName, 
			      COMPONENT_T **schedulerComponent) {
    int err;

    err = ilclient_create_component(handle,
				    schedulerComponent,
				    schedulerComponentName,
				    ILCLIENT_DISABLE_ALL_PORTS
				    |
				    ILCLIENT_ENABLE_INPUT_BUFFERS
				    );
    if (err == -1) {
	fprintf(stderr, "SchedulerComponent create failed\n");
	exit(1);
    }
    printState(ilclient_get_handle(*schedulerComponent));

    err = ilclient_change_component_state(*schedulerComponent,
					  OMX_StateIdle);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Idle\n");
	exit(1);
    }
    printState(ilclient_get_handle(*schedulerComponent));
}

void setup_clockComponent(ILCLIENT_T  *handle, 
			  char *clockComponentName, 
			  COMPONENT_T **clockComponent) {
    int err;

    err = ilclient_create_component(handle,
				    clockComponent,
				    clockComponentName,
				    ILCLIENT_DISABLE_ALL_PORTS
				    );
    if (err == -1) {
	fprintf(stderr, "ClockComponent create failed\n");
	exit(1);
    }
    printState(ilclient_get_handle(*clockComponent));

    err = ilclient_change_component_state(*clockComponent,
					  OMX_StateIdle);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Idle\n");
	exit(1);
    }
    printState(ilclient_get_handle(*clockComponent));
    printClockState(*clockComponent);

    OMX_COMPONENTTYPE*clock = ilclient_get_handle(*clockComponent);

    OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refClock;
    refClock.nSize = sizeof(OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE);
    refClock.nVersion.nVersion = OMX_VERSION;
    refClock.eClock = OMX_TIME_RefClockVideo; // OMX_CLOCKPORT0;

    err = OMX_SetConfig(ilclient_get_handle(*clockComponent), 
			OMX_IndexConfigTimeActiveRefClock, &refClock);
    if(err != OMX_ErrorNone) {
	fprintf(stderr, "COMXCoreComponent::SetConfig - %s failed with omx_err(0x%x)\n", 
		"clock", err);
    }

    OMX_TIME_CONFIG_SCALETYPE scaleType;
    scaleType.nSize = sizeof(OMX_TIME_CONFIG_SCALETYPE);
    scaleType.nVersion.nVersion = OMX_VERSION;
    scaleType.xScale = 0x00010000;

    err = OMX_SetConfig(ilclient_get_handle(*clockComponent), 
			OMX_IndexConfigTimeScale, &scaleType);
    if(err != OMX_ErrorNone) {
	fprintf(stderr, "COMXCoreComponent::SetConfig - %s failed with omx_err(0x%x)\n", 
		"clock", err);
    }
}

int main(int argc, char** argv) {

    char *decodeComponentName;
    char *renderComponentName;
    char *schedulerComponentName;
    char *clockComponentName;
    int err;
    ILCLIENT_T  *handle;
    COMPONENT_T *decodeComponent;
    COMPONENT_T *renderComponent;
    COMPONENT_T *schedulerComponent;
    COMPONENT_T *clockComponent;

    if (argc > 1) {
	IMG = argv[1];
    }

    OMX_BUFFERHEADERTYPE *buff_header;

    setup_demuxer(IMG);

    decodeComponentName = "video_decode";
    renderComponentName = "video_render";
    schedulerComponentName = "video_scheduler";
    clockComponentName = "clock";

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
    setup_schedulerComponent(handle, schedulerComponentName, &schedulerComponent);
    setup_clockComponent(handle, clockComponentName, &clockComponent);
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
 
    FILE *out = fopen("tmp.h264", "wb");
    SendDecoderConfig(decodeComponent, out);


    /* read frames from the file */
    while (av_read_frame(pFormatCtx, &pkt) >= 0) {
	printf("Read pkt %d\n", pkt.size);

	AVPacket orig_pkt = pkt;
	if (pkt.stream_index == video_stream_idx) {
	    printf("  read video pkt %d\n", pkt.size);
	    fwrite(pkt.data, 1, pkt.size, out);
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


    TUNNEL_T decodeTunnel;
    set_tunnel(&decodeTunnel, decodeComponent, 131, schedulerComponent, 10);
    if ((err = ilclient_setup_tunnel(&decodeTunnel, 0, 0)) < 0) {
	fprintf(stderr, "Error setting up decode tunnel %X\n", err);
	exit(1);
    } else {
	printf("Decode tunnel set up ok\n");
    }

    TUNNEL_T schedulerTunnel;
    set_tunnel(&schedulerTunnel, schedulerComponent, 11, renderComponent, 90);
    if ((err = ilclient_setup_tunnel(&schedulerTunnel, 0, 0)) < 0) {
	fprintf(stderr, "Error setting up scheduler tunnel %X\n", err);
	exit(1);
    } else {
	printf("Scheduler tunnel set up ok\n");
    }

    TUNNEL_T clockTunnel;
    set_tunnel(&clockTunnel, clockComponent, 80, schedulerComponent, 12);
    if ((err = ilclient_setup_tunnel(&clockTunnel, 0, 0)) < 0) {
	fprintf(stderr, "Error setting up clock tunnel %X\n", err);
	exit(1);
    } else {
	printf("Clock tunnel set up ok\n");
    }
    startClock(clockComponent);
    printClockState(clockComponent);

    // Okay to go back to processing data
    // enable the decode output ports
   
    OMX_SendCommand(ilclient_get_handle(decodeComponent), 
		    OMX_CommandPortEnable, 131, NULL);
   
    ilclient_enable_port(decodeComponent, 131);

    // enable the clock output ports
    OMX_SendCommand(ilclient_get_handle(clockComponent), 
		    OMX_CommandPortEnable, 80, NULL);
   
    ilclient_enable_port(clockComponent, 80);

    // enable the scheduler ports
    OMX_SendCommand(ilclient_get_handle(schedulerComponent), 
		    OMX_CommandPortEnable, 10, NULL);
   
    ilclient_enable_port(schedulerComponent, 10);

    OMX_SendCommand(ilclient_get_handle(schedulerComponent), 
		    OMX_CommandPortEnable, 11, NULL);

    ilclient_enable_port(schedulerComponent, 11);


    OMX_SendCommand(ilclient_get_handle(schedulerComponent), 
		    OMX_CommandPortEnable, 12, NULL);
   
    ilclient_enable_port(schedulerComponent, 12);

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

    err = ilclient_change_component_state(schedulerComponent,
					  OMX_StateExecuting);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Idle\n");
	exit(1);
    }

    err = ilclient_change_component_state(clockComponent,
					  OMX_StateExecuting);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Idle\n");
	exit(1);
    }

    // now work through the file
    while (av_read_frame(pFormatCtx, &pkt) >= 0) {
	printf("Read pkt after port settings %d\n", pkt.size);
	fwrite(pkt.data, 1, pkt.size, out);
	
	if (pkt.stream_index != video_stream_idx) {
	    continue;
	}
	printf("  is video pkt\n");
	//printf("  Best timestamp is %d\n", );

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

	err = ilclient_wait_for_event(decodeComponent, 
				      OMX_EventPortSettingsChanged, 
				      131, 0, 0, 1,
				      ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 
				      0);
	if (err >= 0) {
	    printf("Another port settings change\n");
	}

    }

    ilclient_wait_for_event(renderComponent, 
			    OMX_EventBufferFlag, 
			    90, 0, OMX_BUFFERFLAG_EOS, 0,
			    ILCLIENT_BUFFER_FLAG_EOS, 10000);
    printf("EOS on render\n");

    exit(0);
}
