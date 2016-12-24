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
#include "libavresample/avresample.h"

#define INBUF_SIZE 4096
#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096

char *IMG = "taichi.mp4";

static AVCodecContext *audio_dec_ctx = NULL;
static AVStream *audio_stream = NULL;
static AVPacket pkt;
AVFormatContext *pFormatCtx = NULL;
AVAudioResampleContext *swr;

int sample_rate;
int channels;

static int audio_stream_idx = -1;

AVCodec *codec;

void setPCMMode(OMX_HANDLETYPE handle, int startPortNumber) {
    OMX_AUDIO_PARAM_PCMMODETYPE sPCMMode;
    OMX_ERRORTYPE err;
 
    memset(&sPCMMode, 0, sizeof(OMX_AUDIO_PARAM_PCMMODETYPE));
    sPCMMode.nSize = sizeof(OMX_AUDIO_PARAM_PCMMODETYPE);
    sPCMMode.nVersion.nVersion = OMX_VERSION;

    sPCMMode.nPortIndex = startPortNumber;

    err = OMX_GetParameter(handle, OMX_IndexParamAudioPcm, &sPCMMode);
    printf("Sampling rate %d, channels %d\n",
	   sPCMMode.nSamplingRate, 
	   sPCMMode.nChannels);

    //sPCMMode.nSamplingRate = 44100; // for taichi
    //sPCMMode.nSamplingRate = 22050; // for big buck bunny
    sPCMMode.nSamplingRate = sample_rate; // for anything
    sPCMMode.nChannels = channels;

    err = OMX_SetParameter(handle, OMX_IndexParamAudioPcm, &sPCMMode);
    if(err != OMX_ErrorNone){
	fprintf(stderr, "PCM mode unsupported\n");
	return;
    } else {
	fprintf(stderr, "PCM mode supported\n");
	fprintf(stderr, "PCM sampling rate %d\n", sPCMMode.nSamplingRate);
	fprintf(stderr, "PCM nChannels %d\n", sPCMMode.nChannels);
    } 
}

static void set_audio_render_input_format(COMPONENT_T *component) {
    // set input audio format
    printf("Setting audio render format\n");
    OMX_AUDIO_PARAM_PORTFORMATTYPE audioPortFormat;
    //setHeader(&audioPortFormat,  sizeof(OMX_AUDIO_PARAM_PORTFORMATTYPE));
    memset(&audioPortFormat, 0, sizeof(OMX_AUDIO_PARAM_PORTFORMATTYPE));
    audioPortFormat.nSize = sizeof(OMX_AUDIO_PARAM_PORTFORMATTYPE);
    audioPortFormat.nVersion.nVersion = OMX_VERSION;

    audioPortFormat.nPortIndex = 100;

    OMX_GetParameter(ilclient_get_handle(component),
                     OMX_IndexParamAudioPortFormat, &audioPortFormat);

    audioPortFormat.eEncoding = OMX_AUDIO_CodingPCM;
    //audioPortFormat.eEncoding = OMX_AUDIO_CodingMP3;
    OMX_SetParameter(ilclient_get_handle(component),
                     OMX_IndexParamAudioPortFormat, &audioPortFormat);

    setPCMMode(ilclient_get_handle(component), 100);

}

void printState(OMX_HANDLETYPE handle) {
    // elided
}

char *err2str(int err) {
    return "error elided";
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
    // printf("Got empty buffer done\n");
}


int get_file_size(char *fname) {
    struct stat st;

    if (stat(fname, &st) == -1) {
	perror("Stat'ing img file");
	return -1;
    }
    return(st.st_size);
}

OMX_ERRORTYPE read_audio_into_buffer_and_empty(AVFrame *decoded_frame,
					       COMPONENT_T *component,
					       // OMX_BUFFERHEADERTYPE *buff_header,
					       int total_len) {
    OMX_ERRORTYPE r;
    OMX_BUFFERHEADERTYPE *buff_header = NULL;

#if AUDIO_DECODE
    int port_index = 120;
#else
    int port_index = 100;
#endif

    int required_decoded_size = 0;
    
    int out_linesize;
    required_decoded_size = 
	av_samples_get_buffer_size(&out_linesize, 2, 
				   decoded_frame->nb_samples,
				   AV_SAMPLE_FMT_S16, 0);
    uint8_t *buffer, *start_buffer;
    av_samples_alloc(&buffer, &out_linesize, 2, decoded_frame->nb_samples,
		     AV_SAMPLE_FMT_S16, 0);
    start_buffer = buffer;
    avresample_convert(swr, &buffer, 
		       decoded_frame->linesize[0], 
		       decoded_frame->nb_samples, 
		       // decoded_frame->extended_data, 
		       decoded_frame->data, 
		       decoded_frame->linesize[0], 
		       decoded_frame->nb_samples);
    // printf("Decoded audio size %d\n", required_decoded_size);

    /* printf("Audio timestamp %lld\n", (decoded_frame->pkt_pts * USECS_IN_SEC *
       atime_base_num /
       atime_base_den));
    */
    //print_clock_time("OMX Audio empty timestamp");

    while (required_decoded_size > 0) {
	buff_header = 
	    ilclient_get_input_buffer(component,
				      port_index,
				      1 /* block */);

	/*
	buff_header->nTimeStamp = ToOMXTime((uint64_t)
					    (decoded_frame->pkt_pts * USECS_IN_SEC *
					     atime_base_num /
					     atime_base_den));
	*/
	int len = buff_header->nAllocLen;
	    
	if (required_decoded_size > len) {
	    // fprintf(stderr, "Buffer not big enough %d, looping\n", len);
	    memcpy(buff_header->pBuffer,
		   buffer, len);
	    buff_header->nFilledLen = len;
	    buffer += len;
	} else {
	    memcpy(buff_header->pBuffer,
		   buffer, required_decoded_size);
	    buff_header->nFilledLen = required_decoded_size;
	}
	// gettimeofday(&tv, NULL);
	// printf("Time audio empty start %ld\n", 
	// tv.tv_sec * USECS_IN_SEC + tv.tv_usec - starttime);
	r = OMX_EmptyThisBuffer(ilclient_get_handle(component),
				buff_header);


	// gettimeofday(&tv, NULL);
	// printf("Time audio empty stop  %ld\n", 
	//  tv.tv_sec * USECS_IN_SEC + tv.tv_usec - starttime);

	//exit_on_omx_error(r, "Empty buffer error %s\n");

	required_decoded_size -= len;   
    }
    //av_freep(&start_buffer[0]);
    av_free(&start_buffer[0]);
    return r;
}

AVCodecContext* codec_context;

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
    ret = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (ret >= 0) {
	audio_stream_idx = ret;
	// audio_stream_idx = 2; // JN

	audio_stream = pFormatCtx->streams[audio_stream_idx];
	audio_dec_ctx = audio_stream->codec;

	sample_rate = audio_dec_ctx->sample_rate;
	channels =  audio_dec_ctx->channels;
	printf("Sample rate is %d channels %d\n", sample_rate, channels);

	AVCodec *codec = avcodec_find_decoder(audio_stream->codec->codec_id);
	codec_context = avcodec_alloc_context3(codec);

	// copy across info from codec about extradata
	codec_context->extradata =  audio_stream->codec->extradata;
	codec_context->extradata_size =  audio_stream->codec->extradata_size;


	if (codec) {
	    printf("Codec name %s\n", codec->name);
	}

	if (!avcodec_open2(codec_context, codec, NULL) < 0) {
	    fprintf(stderr, "Could not find open the needed codec");
	    exit(1);
	}
    }
    return 0;
}

/* For the RPi name can be "hdmi" or "local" */
void setOutputDevice(OMX_HANDLETYPE handle, const char *name) {
    OMX_ERRORTYPE err;
    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE arDest;

    if (name && strlen(name) < sizeof(arDest.sName)) {
	memset(&arDest, 0, sizeof(OMX_CONFIG_BRCMAUDIODESTINATIONTYPE));
	arDest.nSize = sizeof(OMX_CONFIG_BRCMAUDIODESTINATIONTYPE);
	arDest.nVersion.nVersion = OMX_VERSION;

	strcpy((char *)arDest.sName, name);
       
	err = OMX_SetParameter(handle, OMX_IndexConfigBrcmAudioDestination, &arDest);
	if (err != OMX_ErrorNone) {
	    fprintf(stderr, "Error on setting audio destination\n");
	    exit(1);
	}
    }
}

void setup_audio_renderComponent(ILCLIENT_T  *handle, 
				 char *componentName, 
				 COMPONENT_T **component) {
    int err;
    err = ilclient_create_component(handle,
				    component,
				    componentName,
				    ILCLIENT_DISABLE_ALL_PORTS
				    |
				    ILCLIENT_ENABLE_INPUT_BUFFERS
				    );
    if (err == -1) {
	fprintf(stderr, "Component create failed\n");
	exit(1);
    }
    printState(ilclient_get_handle(*component));

    err = ilclient_change_component_state(*component,
					  OMX_StateIdle);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Idle\n");
	exit(1);
    }
    printState(ilclient_get_handle(*component));

    // must be before we enable buffers
    set_audio_render_input_format(*component);

    setOutputDevice(ilclient_get_handle(*component), "local");
    //setOutputDevice(ilclient_get_handle(*component), "hdmi");

    // input port
    ilclient_enable_port_buffers(*component, 100, 
				 NULL, NULL, NULL);
    ilclient_enable_port(*component, 100);




    err = ilclient_change_component_state(*component,
					  OMX_StateExecuting);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Executing\n");
	exit(1);
    }
    printState(ilclient_get_handle(*component));
}

int main(int argc, char** argv) {

    char *renderComponentName;

    int err;
    ILCLIENT_T  *handle;
    COMPONENT_T *renderComponent;

    if (argc > 1) {
	IMG = argv[1];
    }

    OMX_BUFFERHEADERTYPE *buff_header;

    setup_demuxer(IMG);

    renderComponentName = "audio_render";

    bcm_host_init();

    handle = ilclient_init();
    // vcos_log_set_level(VCOS_LOG_CATEGORY, VCOS_LOG_TRACE);
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

    setup_audio_renderComponent(handle, renderComponentName, &renderComponent);

    FILE *out = fopen("tmp.se16", "wb");

    // for converting from AV_SAMPLE_FMT_FLTP to AV_SAMPLE_FMT_S16
    // Set up SWR context once you've got codec information
    //AVAudioResampleContext *swr = avresample_alloc_context();
    swr = avresample_alloc_context();

    av_opt_set_int(swr, "in_channel_layout", 
		   av_get_default_channel_layout(audio_dec_ctx->channels) , 0);
    av_opt_set_int(swr, "out_channel_layout", AV_CH_LAYOUT_STEREO,  0);
    av_opt_set_int(swr, "in_sample_rate",     audio_dec_ctx->sample_rate, 0);
    av_opt_set_int(swr, "out_sample_rate",    audio_dec_ctx->sample_rate, 0);
    av_opt_set_int(swr, "in_sample_fmt", audio_dec_ctx->sample_fmt, 0);
    av_opt_set_int(swr, "out_sample_fmt", AV_SAMPLE_FMT_S16,  0);
    avresample_open(swr);

    fprintf(stderr, "Num channels for resmapling %d\n",
	   av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO));

    int buffer_size;
    int num_ret;

    /* read frames from the file */
    AVFrame *frame = avcodec_alloc_frame(); // av_frame_alloc
    while (av_read_frame(pFormatCtx, &pkt) >= 0) {
	// printf("Read pkt %d\n", pkt.size);

	AVPacket orig_pkt = pkt;
	if (pkt.stream_index == audio_stream_idx) {
	    // printf("  read audio pkt %d\n", pkt.size);
	    //fwrite(pkt.data, 1, pkt.size, out);

	    AVPacket avpkt;
	    int got_frame;
	    av_init_packet(&avpkt);
	    avpkt.data = pkt.data;
	    avpkt.size = pkt.size;


	    uint8_t *buffer;
	    if (((err = avcodec_decode_audio4(codec_context,
					      frame,
					      &got_frame,
					      &avpkt)) < 0)  || !got_frame) {
		fprintf(stderr, "Error decoding %d\n", err);
		continue;
	    }
	    int required_decoded_size = 0;

	    if (audio_dec_ctx->sample_fmt == AV_SAMPLE_FMT_S16) {
		buffer = frame->data;
	    } else {
		read_audio_into_buffer_and_empty(frame,
					 renderComponent,
					 required_decoded_size
					 );
	    }
	    fwrite(buffer, 1, frame->nb_samples*4, out);
	}

	av_free_packet(&orig_pkt);
    }

    exit(0);
}
