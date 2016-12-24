#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

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

static int audio_stream_idx = -1;

AVCodec *codec;


int get_file_size(char *fname) {
    struct stat st;

    if (stat(fname, &st) == -1) {
	perror("Stat'ing img file");
	return -1;
    }
    return(st.st_size);
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

	audio_stream = pFormatCtx->streams[audio_stream_idx];
	audio_dec_ctx = audio_stream->codec;

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

int main(int argc, char** argv) {

    int err;

    if (argc > 1) {
	IMG = argv[1];
    }

    setup_demuxer(IMG);

    FILE *out = fopen("tmp.s16", "w");

    // for converting from AV_SAMPLE_FMT_FLTP to AV_SAMPLE_FMT_S16
    // Set up SWR context once you've got codec information
    AVAudioResampleContext *swr = avresample_alloc_context();
    //av_opt_set_int(swr, "in_channel_layout",  audio_dec_ctx->channel_layout, 0);
    //av_opt_set_int(swr, "out_channel_layout", audio_dec_ctx->channel_layout,  0);
    av_opt_set_int(swr, "in_channel_layout", 
    		   av_get_default_channel_layout(audio_dec_ctx->channels) , 0);
    av_opt_set_int(swr, "out_channel_layout", AV_CH_LAYOUT_STEREO,  0);
    av_opt_set_int(swr, "in_sample_rate",     audio_dec_ctx->sample_rate, 0);
    av_opt_set_int(swr, "out_sample_rate",    audio_dec_ctx->sample_rate, 0);
    //av_opt_set_int(swr, "in_sample_fmt",  AV_SAMPLE_FMT_FLTP, 0);
    av_opt_set_int(swr, "in_sample_fmt",   audio_dec_ctx->sample_fmt, 0);
    av_opt_set_int(swr, "out_sample_fmt", AV_SAMPLE_FMT_S16,  0);
    avresample_open(swr);

    int buffer_size;
    int num_ret;

    /* read frames from the file */
    AVFrame *frame = avcodec_alloc_frame(); // av_frame_alloc
    while (av_read_frame(pFormatCtx, &pkt) >= 0) {
	// printf("Read pkt %d\n", pkt.size);

	AVPacket orig_pkt = pkt;
	if (pkt.stream_index == audio_stream_idx) {
	    // printf("  read audio pkt %d\n", pkt.size);
	    // fwrite(pkt.data, 1, pkt.size, out);

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

	    int out_linesize;
	    /*
	    av_samples_get_buffer_size(&out_linesize, 2, frame->nb_samples,
					   AV_SAMPLE_FMT_S16, 0);
	    */
	    av_samples_alloc(&buffer, &out_linesize, 2, frame->nb_samples,
			     AV_SAMPLE_FMT_S16, 0);

	    avresample_convert(swr, &buffer, 
			       frame->linesize[0], 
			       frame->nb_samples, 
			       frame->data, 
			       // frame->extended_data, 
			       frame->linesize[0],
			       frame->nb_samples);
	    /*
	    printf("Pkt size %d, decoded to %d line size %d\n",
		   pkt.size, err, out_linesize);
	    printf("Samples: pkt size %d nb_samples %d\n",
		  pkt.size, frame->nb_samples);
	    printf("Buffer is (decoded size %d)  %d %d %d %d %d\n", 
		   required_decoded_size,
		   buffer[0], buffer[1], buffer[2], buffer[3], err);
	    */
	    fwrite(buffer, 1, frame->nb_samples*4, out);
	}
	av_free_packet(&orig_pkt);
    }

    printf("Finished\n");
    exit(0);
}
