// wv_remux.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include <stdlib.h>

extern "C"
{
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h" 
#include "libavutil/timestamp.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavformat/avio.h"
}

int main(int argc, char **argv)
{
	AVOutputFormat *ofmt = NULL;
	AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
	AVPacket pkt;
	const char *in_filename, *out_filename;
	int ret, i;
	int stream_index = 0;
	int *stream_mapping = NULL;
	int stream_mapping_size = 0;

	int64_t lastVideoDts = 0;
	int64_t offsetVideoDts = 0;
	int64_t lastAudioDts = 0;
	int64_t offsetAudioDts = 0;
	int64_t offsetModify = 0;
	bool audioFirst = true;
	bool videoFirst = true;


	if (argc < 3) {
		printf("usage: %s input output\n"
			"API example program to remux a media file with libavformat and libavcodec.\n"
			"The output format is guessed according to the file extension.\n"
			"\n", argv[0]);
		return 1;
	}

	in_filename = argv[1];
	out_filename = argv[2];

	AVDictionary *inputdic = NULL;
	av_dict_set(&inputdic, "buffer_size", "1024000", 0);

	if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, &inputdic)) < 0) {
		fprintf(stderr, "Could not open input file '%s'", in_filename);
		goto end;
	}

	if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
		fprintf(stderr, "Failed to retrieve input stream information");
		goto end;
	}

	av_dump_format(ifmt_ctx, 0, in_filename, 0);

	avformat_alloc_output_context2(&ofmt_ctx, NULL, "mpegts", out_filename);
	if (!ofmt_ctx) {
		fprintf(stderr, "Could not create output context\n");
		ret = AVERROR_UNKNOWN;
		goto end;
	}

	stream_mapping_size = ifmt_ctx->nb_streams;
	stream_mapping = (int *)av_mallocz_array(stream_mapping_size, sizeof(*stream_mapping));
	if (!stream_mapping) {
		ret = AVERROR(ENOMEM);
		goto end;
	}

	ofmt = ofmt_ctx->oformat;
	ofmt->flags |= AVFMT_TS_NONSTRICT;

	for (i = 0; i < ifmt_ctx->nb_streams; i++) {
		AVStream *out_stream;
		AVStream *in_stream = ifmt_ctx->streams[i];
		AVCodecParameters *in_codecpar = in_stream->codecpar;

		if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
			in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
			in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
			stream_mapping[i] = -1;
			continue;
		}

		stream_mapping[i] = stream_index++;

		out_stream = avformat_new_stream(ofmt_ctx, NULL);
		if (!out_stream) {
			fprintf(stderr, "Failed allocating output stream\n");
			ret = AVERROR_UNKNOWN;
			goto end;
		}

		ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
		if (ret < 0) {
			fprintf(stderr, "Failed to copy codec parameters\n");
			goto end;
		}
		out_stream->codecpar->codec_tag = 0;
	}
	av_dump_format(ofmt_ctx, 0, out_filename, 1);

	if (!(ofmt->flags & AVFMT_NOFILE)) {
		AVDictionary*dic = NULL;
		av_dict_set(&dic, "pkt_size", "1316", 0);
		//av_dict_set(&dic, "fifo_size", "18800", 0);
		av_dict_set(&dic, "buffer_size", "1000000", 0);
		av_dict_set(&dic, "bitrate", "11000000", 0);
		av_dict_set(&dic, "reuse", "1", 0);
		ret = avio_open2(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE, NULL, &dic);
		if (ret < 0) {
			fprintf(stderr, "Could not open output file '%s'", out_filename);
			goto end;
		}
	}
	
	av_opt_set(ofmt_ctx->priv_data, "muxrate", "11000000", 0);
	av_opt_set(ofmt_ctx->priv_data, "MpegTSWrite", "1", 0);
	av_opt_set(ofmt_ctx->priv_data, "pes_payload_size", "300", 0);

	AVDictionary*dicCtx = NULL;
	av_dict_set(&dicCtx, "max_delay", "1000000", 0);
	ret = avformat_write_header(ofmt_ctx, &dicCtx);
	if (ret < 0) {
		fprintf(stderr, "Error occurred when opening output file\n");
		goto end;
	}

	while (1) {
		AVStream *in_stream, *out_stream;

		ret = av_read_frame(ifmt_ctx, &pkt);
		if (ret < 0)
			break;

		in_stream = ifmt_ctx->streams[pkt.stream_index];
		if (pkt.stream_index >= stream_mapping_size ||
			stream_mapping[pkt.stream_index] < 0) {
			av_packet_unref(&pkt);
			continue;
		}

		pkt.stream_index = stream_mapping[pkt.stream_index];
		out_stream = ofmt_ctx->streams[pkt.stream_index];

		if (AVMEDIA_TYPE_VIDEO == ifmt_ctx->streams[pkt.stream_index]->codecpar->codec_type)
		{
			if (true == videoFirst)
			{
				offsetVideoDts = (90000 - pkt.dts);
				videoFirst = false;
			}

			pkt.dts += offsetVideoDts;
			pkt.pts += offsetVideoDts;
			if (pkt.dts < lastVideoDts)
			{
				offsetModify = (lastVideoDts - pkt.dts) + 1;
				pkt.dts += offsetModify;
				pkt.pts += offsetModify;
				offsetVideoDts += offsetModify;			
			}		
			lastVideoDts = pkt.dts;			
		}

		if (AVMEDIA_TYPE_AUDIO == ifmt_ctx->streams[pkt.stream_index]->codecpar->codec_type)
		{
			if (true == audioFirst)
			{
				offsetAudioDts = (0 - pkt.dts);
				printf("frist %lld (dts %lld)\n", offsetAudioDts, pkt.dts);
				audioFirst = false;
			}

			pkt.dts += offsetAudioDts;
			pkt.pts += offsetAudioDts;
			if (pkt.dts < lastAudioDts)
			{				
				offsetModify = (lastAudioDts - pkt.dts) + 1;
				printf("offsetAudioDts %lld offsetModify %lld\n", offsetAudioDts, offsetModify);
				pkt.dts += offsetModify;
				pkt.pts += offsetModify;
				offsetAudioDts += offsetModify;
				printf("audio offset %lld\n", offsetAudioDts);
			}	
			lastAudioDts = pkt.dts;			
		}

		/* copy packet */
		pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
		pkt.pos = -1;

		ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
		if (ret < 0) {
			fprintf(stderr, "Error muxing packet\n");
			break;
		}
		av_packet_unref(&pkt);
	}

	av_write_trailer(ofmt_ctx);
end:

	avformat_close_input(&ifmt_ctx);

	/* close output */
	if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
		avio_closep(&ofmt_ctx->pb);
	avformat_free_context(ofmt_ctx);

	av_freep(&stream_mapping);

	if (ret < 0 && ret != AVERROR_EOF) {
		//fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
		return 1;
	}

	system("pause");

	return 0;
}
