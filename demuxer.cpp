#include <iostream>
#include <functional>
#include <map>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/codec_par.h>
#include <libavcodec/bsf.h>
}

#include "demuxer.h"
#include "stream_info.h"
#include "utils.h"

template Codec_id get_from_map(std::map<AVCodecID, Codec_id>, AVCodecID, Codec_id);
template Pixel_format get_from_map(std::map<AVPixelFormat, Pixel_format>, AVPixelFormat, Pixel_format);

Demuxer::Demuxer() {}

Demuxer::~Demuxer() {
	av_packet_free(&packet_original);
	av_packet_free(&packet_filtered);
	av_bsf_free(&bitstream_filter_context);
	avformat_close_input(&format_context);
}

Stream_info Demuxer::make_stream_info() {
	Stream_info stream_info;

	std::map<AVCodecID, Codec_id> codec_map = {
		{AVCodecID::AV_CODEC_ID_H264, Codec_id::h264},
		{AVCodecID::AV_CODEC_ID_HEVC, Codec_id::hevc},
		{AVCodecID::AV_CODEC_ID_AV1, Codec_id::av1}
	};

	std::map<AVPixelFormat, Pixel_format> pixel_format_map = {
		{AVPixelFormat::AV_PIX_FMT_YUV420P, Pixel_format::yuv420}
	};

	// For video, this variable corresponds to an item in the AVPixelFormat enum. See comment on parameter "format"
	auto pixel_format = static_cast<AVPixelFormat>(format_context->streams[idx_video_stream]->codecpar->format);

	stream_info.codec_id = get_from_map(codec_map, format_context->streams[idx_video_stream]->codecpar->codec_id, Codec_id::unsupported);
	stream_info.pixel_format = get_from_map(pixel_format_map, pixel_format, Pixel_format::unsupported);
	stream_info.height = format_context->streams[idx_video_stream]->codecpar->height;
	stream_info.width = format_context->streams[idx_video_stream]->codecpar->width;
	stream_info.bits_per_raw_pixel = format_context->streams[0]->codecpar->bits_per_raw_sample;

	return stream_info;
}

bool Demuxer::init(const char* input_file, Stream_info* stream_info) {
	format_context = avformat_alloc_context();

	if (avformat_open_input(&format_context, input_file, nullptr, nullptr) < 0) {
		std::cout << "Could not open input file " << input_file << std::endl;
		return false;
	}

	if (avformat_find_stream_info(format_context, nullptr) < 0) {
		std::cout << "Could not find stream info" << std::endl;
		return false;
	}

	// NB we only take the first stream, will miss out if there are multiple
	for (unsigned int i = 0; i < format_context->nb_streams; i++) {
		if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			idx_video_stream = i;
			break;
		}
	} 

	av_dump_format(format_context, idx_video_stream, input_file, 0);

	packet_original = av_packet_alloc();
	packet_filtered = av_packet_alloc();

	if (!packet_original || !packet_filtered) {
		std::cout << "Can't allocate packet" << std::endl;
		return false;
	}

	// TODO: Different filters for different video formats. This is for h264 only. See FFmpegDemuxer.h in the NV12 samples
	bitstream_filter = (AVBitStreamFilter*)av_bsf_get_by_name("h264_mp4toannexb");
	// TODO: Add error handling here
	av_bsf_alloc(bitstream_filter, &bitstream_filter_context);
	avcodec_parameters_copy(bitstream_filter_context->par_in, format_context->streams[0]->codecpar); // TODO Right now, we hard-code video stream index here,
	av_bsf_init(bitstream_filter_context);

	if (stream_info) {
		*stream_info = make_stream_info();
	}

	return true;
}

bool Demuxer::demux(Packet_data* packet_data) {
	if (packet_original->data) {
		av_packet_unref(packet_original);
	}

	int ret;
	
	// Skip unwanted packets
	while (((ret = av_read_frame(format_context, packet_original)) >= 0) && (packet_original->stream_index != idx_video_stream)) {
		av_packet_unref(packet_original);
	}

	if (ret < 0) {
		// End of stream; return
		return false;
	}

	if (packet_filtered->data) {
		av_packet_unref(packet_filtered);
	}

	// TODO: Different bitstream filter for != h264 video formats! See comment when initializing
	av_bsf_send_packet(bitstream_filter_context, packet_original);
	av_bsf_receive_packet(bitstream_filter_context, packet_filtered);

	packet_data->size = static_cast<int>(packet_filtered->buf->size);
	packet_data->data = packet_filtered->buf->data;

	return true;
}