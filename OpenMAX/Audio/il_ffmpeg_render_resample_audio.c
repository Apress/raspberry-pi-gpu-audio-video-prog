#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <OMX_Core.h>
#include <OMX_Component.h>

#include <bcm_host.h>
#include <ilclient.h>


#include "libavcodec/avcodec.h"
#include <libavformat/avformat.h>
#include "libavutil/mathematics.h"
#include "libavutil/samplefmt.h"
#include "libavutil/opt.h"
#include "libavresample/avresample.h"

#define INBUF_SIZE 4096
#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096

#define AUDIO  "BST.mp3"

AVCodecContext *audio_dec_ctx;
int audio_stream_idx;

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

    sPCMMode.nSamplingRate = 44100;
    sPCMMode.nChannels = 2; // assumed for now - should be checked

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

void printState(OMX_HANDLETYPE handle) {
    // elided
}

char *err2str(int err) {
    return "error elided";
}

void eos_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data) {
    fprintf(stderr, "Got eos event\n");
}

void error_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data) {
    fprintf(stderr, "OMX error %s\n", err2str(data));
}

int get_file_size(char *fname) {
    struct stat st;

    if (stat(fname, &st) == -1) {
	perror("Stat'ing img file");
	return -1;
    }
    return(st.st_size);
}

AVPacket avpkt;

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

int num_streams = 0;
int sample_size = 0;

OMX_ERRORTYPE read_into_buffer_and_empty(AVFrame *decoded_frame,
					 COMPONENT_T *component,
					 // OMX_BUFFERHEADERTYPE *buff_header,
					 int total_len) {
    OMX_ERRORTYPE r;
    OMX_BUFFERHEADERTYPE *buff_header = NULL;

    // do this once only
    AVAudioResampleContext *swr = avresample_alloc_context();
    av_opt_set_int(swr, "in_channel_layout",  audio_dec_ctx->channel_layout, 0);
    av_opt_set_int(swr, "out_channel_layout", audio_dec_ctx->channel_layout,  0);
    av_opt_set_int(swr, "in_sample_rate",     audio_dec_ctx->sample_rate, 0);
    av_opt_set_int(swr, "out_sample_rate",    audio_dec_ctx->sample_rate, 0);
    av_opt_set_int(swr, "in_sample_fmt",  audio_dec_ctx->sample_fmt, 0);
    av_opt_set_int(swr, "out_sample_fmt", AV_SAMPLE_FMT_S16,  0);
    avresample_open(swr);

    int required_decoded_size = 0;
    
    int out_linesize;
    required_decoded_size = 
	av_samples_get_buffer_size(&out_linesize, 2, 
				   decoded_frame->nb_samples,
				   AV_SAMPLE_FMT_S16, 0);
    uint8_t *buffer;
    av_samples_alloc(&buffer, &out_linesize, 2, decoded_frame->nb_samples,
		     AV_SAMPLE_FMT_S16, 0);
    avresample_convert(swr, &buffer, 
		       decoded_frame->linesize[0], 
		       decoded_frame->nb_samples, 
		       // decoded_frame->extended_data, 
		       decoded_frame->data, 
		       decoded_frame->linesize[0], 
		       decoded_frame->nb_samples);

    while (required_decoded_size >= 0) {
	buff_header = 
	    ilclient_get_input_buffer(component,
				      100,
				      1 /* block */);
	if (required_decoded_size > 4096) {
	    memcpy(buff_header->pBuffer,
		   buffer, 4096);
	    buff_header->nFilledLen = 4096;
	    buffer += 4096;
	} else {
	     memcpy(buff_header->pBuffer,
		   buffer, required_decoded_size);
	    buff_header->nFilledLen = required_decoded_size;
	}
	required_decoded_size -= 4096;
	
	r = OMX_EmptyThisBuffer(ilclient_get_handle(component),
				buff_header);
	if (r != OMX_ErrorNone) {
	    fprintf(stderr, "Empty buffer error %s\n",
		    err2str(r));
	    return r;
	}
    }
    return r;
}

FILE *favpkt = NULL;

int main(int argc, char** argv) {

    int i;
    char *componentName;
    int err;
    ILCLIENT_T  *handle;
    COMPONENT_T *component;

    AVFormatContext *pFormatCtx = NULL;

    char *audio_file = AUDIO;
    if (argc == 2) {
	audio_file = argv[1];
    }

    OMX_BUFFERHEADERTYPE *buff_header;

    componentName = "audio_render";

    bcm_host_init();

    handle = ilclient_init();
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


    err = ilclient_create_component(handle,
				    &component,
				    componentName,
				    ILCLIENT_DISABLE_ALL_PORTS
				    |
				    ILCLIENT_ENABLE_INPUT_BUFFERS
				    );
    if (err == -1) {
	fprintf(stderr, "Component create failed\n");
	exit(1);
    }
    printState(ilclient_get_handle(component));

    err = ilclient_change_component_state(component,
					  OMX_StateIdle);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Idle\n");
	exit(1);
    }
    printState(ilclient_get_handle(component));


    // FFmpeg init
    av_register_all();
    if(avformat_open_input(&pFormatCtx, audio_file, NULL, NULL)!=0) {
	fprintf(stderr, "Can't get format\n");
        return -1; // Couldn't open file
    }
    // Retrieve stream information
    if(avformat_find_stream_info(pFormatCtx, NULL)<0)
	return -1; // Couldn't find stream information
    av_dump_format(pFormatCtx, 0, audio_file, 0);

    int ret;
    if ((ret = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0)) >= 0) {
	//AVCodecContext* codec_context;
	AVStream *audio_stream;
	int sample_rate;

	audio_stream_idx = ret;
	fprintf(stderr, "Audio stream index is %d\n", ret);

	audio_stream = pFormatCtx->streams[audio_stream_idx];
	audio_dec_ctx = audio_stream->codec;

	sample_rate = audio_dec_ctx->sample_rate;
	printf("Sample rate is %d\n", sample_rate);
	printf("Sample format is %d\n", audio_dec_ctx->sample_fmt);
	printf("Num channels %d\n", audio_dec_ctx->channels);

	if (audio_dec_ctx->channel_layout == 0) {
	    audio_dec_ctx->channel_layout = 
		av_get_default_channel_layout(audio_dec_ctx->channels);
	}

	AVCodec *codec = avcodec_find_decoder(audio_stream->codec->codec_id);
	if (avcodec_open2(audio_dec_ctx, codec, NULL) < 0) {
	    fprintf(stderr, "could not open codec\n");
	    exit(1);
	}

	if (codec) {
	    printf("Codec name %s\n", codec->name);
	}
    }

    av_init_packet(&avpkt);

    // must be before we enable buffers
    set_audio_render_input_format(component);

    setOutputDevice(ilclient_get_handle(component), "local");

    // input port
    ilclient_enable_port_buffers(component, 100, 
				 NULL, NULL, NULL);
    ilclient_enable_port(component, 100);

    err = ilclient_change_component_state(component,
					  OMX_StateExecuting);
    if (err < 0) {
	fprintf(stderr, "Couldn't change state to Executing\n");
	exit(1);
    }
    printState(ilclient_get_handle(component));


    // now work through the file

    int len;
    uint8_t inbuf[AUDIO_INBUF_SIZE + FF_INPUT_BUFFER_PADDING_SIZE];

    AVFrame *decoded_frame = NULL;

    /* decode until eof */
    avpkt.data = inbuf;
    av_read_frame(pFormatCtx, &avpkt);

    while (avpkt.size > 0) {
	printf("Packet size %d\n", avpkt.size);
	printf("Stream idx is %d\n", avpkt.stream_index);
	printf("Codec type %d\n", pFormatCtx->streams[1]->codec->codec_type);
	
	if (avpkt.stream_index != audio_stream_idx) {
	    // it's an image, subtitle, etc
	    av_read_frame(pFormatCtx, &avpkt);
	    continue;
	}
	
        int got_frame = 0;

	if (favpkt == NULL) {
	    favpkt = fopen("tmp.mp3", "wb");
	}
	fwrite(avpkt.data, 1, avpkt.size, favpkt);

        if (!decoded_frame) {
            if (!(decoded_frame = avcodec_alloc_frame())) {
                fprintf(stderr, "out of memory\n");
                exit(1);
            }
	}

        len = avcodec_decode_audio4(audio_dec_ctx, 
				     decoded_frame, &got_frame, &avpkt);
        if (len < 0) {
            fprintf(stderr, "Error while decoding\n");
            exit(1);
        }
        if (got_frame) {
            /* if a frame has been decoded, we want to send it to OpenMAX */
            int data_size =
		av_samples_get_buffer_size(NULL, audio_dec_ctx->channels,
					   decoded_frame->nb_samples,
					   audio_dec_ctx->sample_fmt, 1);

	    // Empty into render_audio input buffers
	    read_into_buffer_and_empty(decoded_frame,
				       component,
				       data_size
				       );
	}
	av_read_frame(pFormatCtx, &avpkt);
	continue;
    }

    printf("Finished decoding MP3\n");
    // clean up last empty buffer with EOS
    buff_header = 
	ilclient_get_input_buffer(component,
				  100,
				  1 /* block */);
    buff_header->nFilledLen = 0;
    int r;
    buff_header->nFlags |= OMX_BUFFERFLAG_EOS;
    r = OMX_EmptyThisBuffer(ilclient_get_handle(component),
			    buff_header);
    if (r != OMX_ErrorNone) {
	fprintf(stderr, "Empty buffer error %s\n",
		err2str(r));
    } else {
	printf("EOS sent\n");
    }

    avcodec_close(audio_dec_ctx);
    av_free(audio_dec_ctx);
    av_free(decoded_frame);

    sleep(10);
    exit(0);
}
