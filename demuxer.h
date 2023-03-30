#pragma once

struct AVFormatContext;
struct AVPacket;
struct AVBitStreamFilter;
struct AVBSFContext;
struct Stream_info;

/**
 * @brief Size of demuxed data and a pointer to the data buffer
*/
class Packet_data {
public:
	unsigned char* data;
	int size;
};

/**
 * @brief Used for demuxing video
*/
class Demuxer {
public:
	Demuxer();
	~Demuxer();

	/**
	 * @brief Initializes the demuxer
	 * @param input_file Video file to demux
	 * @param stream_info Optional out parameter, will be updated with stream info if provided
	 * @return True on success, false otherwise
	*/
	bool init(const char* input_file, Stream_info* stream_info = nullptr);

	/**
	 * @brief Demux the next packet
	 * @param packet_data This structure will be filled by this function
	 * @return True on success, false otherwise or if the video is at the end
	*/
	bool demux(Packet_data* packet_data);
private:
	Stream_info make_stream_info();
	int idx_video_stream = 0;
	AVFormatContext* format_context = nullptr;
	AVPacket* packet_original = nullptr;
	AVPacket* packet_filtered = nullptr;
	AVBitStreamFilter* bitstream_filter = nullptr;
	AVBSFContext* bitstream_filter_context = nullptr;
};