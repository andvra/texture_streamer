#include <iostream>

#include "decoder.h"
#include "demuxer.h"

// TODO: Found this in the example code. Put as a map and use it?
// 
//inline cudaVideoCodec FFmpeg2NvCodecId(AVCodecID id) {
//	switch (id) {
//	case AV_CODEC_ID_MPEG1VIDEO: return cudaVideoCodec_MPEG1;
//	case AV_CODEC_ID_MPEG2VIDEO: return cudaVideoCodec_MPEG2;
//	case AV_CODEC_ID_MPEG4: return cudaVideoCodec_MPEG4;
//	case AV_CODEC_ID_WMV3:
//	case AV_CODEC_ID_VC1: return cudaVideoCodec_VC1;
//	case AV_CODEC_ID_H264: return cudaVideoCodec_H264;
//	case AV_CODEC_ID_HEVC: return cudaVideoCodec_HEVC;
//	case AV_CODEC_ID_VP8: return cudaVideoCodec_VP8;
//	case AV_CODEC_ID_VP9: return cudaVideoCodec_VP9;
//	case AV_CODEC_ID_MJPEG: return cudaVideoCodec_JPEG;
//	case AV_CODEC_ID_AV1: return cudaVideoCodec_AV1;
//	default: return cudaVideoCodec_NumCodecs;
//	}
//}

int main()
{
	const char* input_file = R"(d:\downloads\Tutorial1.mp4)";

	Demuxer demuxer;
	Packet_data packet_data;
	Decoder decoder;

	if (!demuxer.init(input_file)) {
		return -1;
	}

	if (!decoder.init()) {
		return -1;
	}

	for (int i = 0; i < 100; i++) {
		if (demuxer.demux(&packet_data)) {
			std::cout << "Demuxing packet of size " << packet_data.size << std::endl;
			if (!decoder.decode(packet_data.data, packet_data.size)) {
				std::cout << "Could not decode :(" << std::endl;
			}
		}
	}

	std::cout << "Done" << std::endl;

	return 0;
}
