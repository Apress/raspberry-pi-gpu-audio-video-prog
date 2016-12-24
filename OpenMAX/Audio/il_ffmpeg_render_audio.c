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

#define INBUF_SIZE 4096
#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096

#define AUDIO  "BST.mp3"

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
AVCodecContext *c = NULL;

/*
 * Audio decoding.
 */
static void audio_decode_example(const char *filename)
{
    AVCodec *codec;


    av_init_packet(&avpkt);

    printf("Audio decoding\n");

    /* find the mpeg audio decoder */
    codec = avcodec_find_decoder(CODEC_ID_MP3);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        exit(1);
    }

    c = avcodec_alloc_context3(codec);;

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "could not open codec\n");
        exit(1);
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

int num_streams = 0;
int sample_size = 0;

OMX_ERRORTYPE read_into_buffer_and_empty(AVFrame *decoded_frame,
					 COMPONENT_T *component,
					 // OMX_BUFFERHEADERTYPE *buff_header,
					 int total_len) {
    OMX_ERRORTYPE r;
    OMX_BUFFERHEADERTYPE *buff_header = NULL;
    int k, m, n;

    if (total_len <= 4096) { //buff_header->nAllocLen) {
	// all decoded frame fits into one OpenMAX buffer
	buff_header = 
	    ilclient_get_input_buffer(component,
				      100,
				      1 /* block */);
	for (k = 0, n = 0; n < decoded_frame->nb_samples; n++) {
	    for (m = 0; m < num_streams; m++) {
		memcpy(&buff_header->pBuffer[k], 
		       &decoded_frame->data[m][n*sample_size], 
		       sample_size);
		k += sample_size;
	    }
	}

	buff_header->nFilledLen = k;
	r = OMX_EmptyThisBuffer(ilclient_get_handle(component),
				buff_header);
	if (r != OMX_ErrorNone) {
	    fprintf(stderr, "Empty buffer error %s\n",
		    err2str(r));
	}
	return r;
    }

    // more than one OpenMAX buffer required
    for (k = 0, n = 0; n < decoded_frame->nb_samples; n++) {

	if (k == 0) {
	     buff_header = 
		ilclient_get_input_buffer(component,
					  100,
					  1 /* block */);
	}

	// interleave the samples from the planar streams
	for (m = 0; m < num_streams; m++) {
	    memcpy(&buff_header->pBuffer[k], 
		   &decoded_frame->data[m][n*sample_size], 
		   sample_size);
	    k += sample_size;
	}

	if (k >= buff_header->nAllocLen) {
	    // this buffer is full
	    buff_header->nFilledLen = k;
	    r = OMX_EmptyThisBuffer(ilclient_get_handle(component),
				    buff_header);
	    if (r != OMX_ErrorNone) {
		fprintf(stderr, "Empty buffer error %s\n",
			err2str(r));
	    }
	    k = 0;
	    buff_header = NULL;
	}
    }
    if (buff_header != NULL) {
	    buff_header->nFilledLen = k;
	    r = OMX_EmptyThisBuffer(ilclient_get_handle(component),
				    buff_header);
	    if (r != OMX_ErrorNone) {
		fprintf(stderr, "Empty buffer error %s\n",
			err2str(r));
	    }
    }
    return r;
}


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

    FILE *fp = fopen(audio_file, "r");
    int toread = get_file_size(audio_file);

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
    
    audio_decode_example(audio_file);


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
    FILE *f;
    uint8_t inbuf[AUDIO_INBUF_SIZE + FF_INPUT_BUFFER_PADDING_SIZE];

    AVFrame *decoded_frame = NULL;


    f = fopen(audio_file, "rb");
    if (!f) {
        fprintf(stderr, "could not open %s\n", audio_file);
        exit(1);
    }

    /* decode until eof */
    avpkt.data = inbuf;
    avpkt.size = fread(inbuf, 1, AUDIO_INBUF_SIZE, f);

    while (avpkt.size > 0) {
        int got_frame = 0;

        if (!decoded_frame) {
            if (!(decoded_frame = avcodec_alloc_frame())) {
                fprintf(stderr, "out of memory\n");
                exit(1);
            }
        } else
            avcodec_get_frame_defaults(decoded_frame);

        len = avcodec_decode_audio4(c, decoded_frame, &got_frame, &avpkt);
        if (len < 0) {
            fprintf(stderr, "Error while decoding\n");
            exit(1);
        }
        if (got_frame) {
            /* if a frame has been decoded, we want to send it to OpenMAX */
            int data_size = av_samples_get_buffer_size(NULL, c->channels,
                                                       decoded_frame->nb_samples,
                                                       c->sample_fmt, 1);
	    // first time: count the number of  planar streams
	    if (num_streams == 0) {
		while (num_streams < AV_NUM_DATA_POINTERS &&
		       decoded_frame->data[num_streams] != NULL) 
		    num_streams++; 
	    }

	    // first time: set sample_size from 0 to e.g 2 for 16-bit data
	    if (sample_size == 0) {
		sample_size = 
		    data_size / (num_streams * decoded_frame->nb_samples);
	    }

	    // Empty into render_audio input buffers
	    read_into_buffer_and_empty(decoded_frame,
				       component,
				       data_size
				       );
	}

	avpkt.size -= len;
        avpkt.data += len;
        if (avpkt.size < AUDIO_REFILL_THRESH) {
            /* Refill the input buffer, to avoid trying to decode
             * incomplete frames. Instead of this, one could also use
             * a parser, or use a proper container format through
             * libavformat. */
            memmove(inbuf, avpkt.data, avpkt.size);
            avpkt.data = inbuf;
            len = fread(avpkt.data + avpkt.size, 1,
                        AUDIO_INBUF_SIZE - avpkt.size, f);
            if (len > 0)
                avpkt.size += len;
        }
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

    fclose(f);

    avcodec_close(c);
    av_free(c);
    av_free(decoded_frame);

    sleep(10);
    exit(0);
}
