#pragma once

struct Video_format;
struct Stream_info;

/**
 * @brief See guide for NV12 decoding here: https://docs.nvidia.com/video-codec-sdk/nvdec-video-decoder-api-prog-guide/index.html
*/
class Decoder {
public:
	Decoder() {}
	~Decoder();

	/**
	 * @brief Initializes cuvid decoding for a file with the given format
	 * @param stream_info Information about the stream
	 * @return True on success, false otherwise
	*/
	bool init(const Stream_info& stream_info);

	/**
	 * @brief Tries to decode the given data
	 * @param data Data pointer
	 * @param data_size Size of data
	 * @return True on success, false otherwise
	*/
	bool decode(unsigned char* data, int data_size);
private:
};