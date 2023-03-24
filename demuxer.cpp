#include <iostream>
#include <functional>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/codec_par.h>
}

#include "demuxer.h"

Demuxer::Demuxer() {}

Demuxer::~Demuxer() {
	av_packet_free(&packet_original);
	av_packet_free(&packet_filtered);
	av_bsf_free(&bitstream_filter_context);
	avformat_close_input(&format_context);
}

bool Demuxer::init(const char* input_file) {
	format_context = avformat_alloc_context();

	if (avformat_open_input(&format_context, input_file, nullptr, nullptr) < 0) {
		std::cout << "Could not open input file " << input_file << std::endl;
		return false;
	}

	if (avformat_find_stream_info(format_context, nullptr) < 0) {
		std::cout << "Could not find stream info" << std::endl;
		return false;
	}

	// TODO: Instead, find out what stream to use
	idx_video_stream = 0;

	// TODO: Is 0=video, 1=audio here?
	av_dump_format(format_context, 0, input_file, 0);

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

	packet_data->size = packet_filtered->buf->size;
	packet_data->data = packet_filtered->buf->data;

	return true;
}