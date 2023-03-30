#include <iostream>

#include "decoder.h"
#include "demuxer.h"
#include "stream_info.h"

int main()
{
	const char* input_file = R"(d:\downloads\Tutorial1.mp4)";

	Stream_info stream_info;
	Demuxer demuxer;
	Packet_data packet_data;
	Decoder decoder;

	if (!demuxer.init(input_file, &stream_info)) {
		return -1;
	}

	if (!decoder.init(stream_info)) {
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
