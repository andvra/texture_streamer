#pragma once

enum cudaError_enum;
typedef enum cudaError_enum CUresult;
struct _CUVIDDECODECAPS;
typedef struct _CUVIDDECODECAPS CUVIDDECODECAPS;
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
	/**
	 * @brief Print info about the decoding device
	*/
	void print_device_info();

	/**
	 * @brief Initialize a video parser object
	 * @param video_format Video format
	 * @param video_parser Vide parser
	 * @return Cuvid result code
	*/
	CUresult create_video_parser(const Video_format& video_format, void*& video_parser);

	/**
	 * @brief Gets the decode capabilities. Useful if you look for specific decode features
	 * @param video_format Video format
	 * @param decode_capabilities Reference to decode capabilities result object
	 * @return Cuvid result code
	*/
	CUresult get_decode_cababilities(const Video_format& video_format, CUVIDDECODECAPS& decode_capabilities);

	/**
	 * @brief Creates a decoder
	 * @param video_format Video format
	 * @result Cuvid result code
	*/
	CUresult create_decoder(const Video_format& video_format);
};