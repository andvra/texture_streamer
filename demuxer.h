#pragma once

struct AVFormatContext;
struct AVPacket;
struct AVBitStreamFilter;
struct AVBSFContext;

class Packet_data {
public:
	unsigned char* data;
	int size;
};

class Demuxer {
public:
	Demuxer();
	~Demuxer();
	bool init(const char* input_file);
	bool demux(Packet_data* packet_data);
private:
	int idx_video_stream = 0;
	AVFormatContext* format_context = nullptr;
	AVPacket* packet_original = nullptr;
	AVPacket* packet_filtered = nullptr;
	AVBitStreamFilter* bitstream_filter = nullptr;
	AVBSFContext* bitstream_filter_context = nullptr;
};