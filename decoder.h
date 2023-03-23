#pragma once

enum cudaError_enum;
typedef enum cudaError_enum CUresult;
struct _CUVIDDECODECAPS;
typedef struct _CUVIDDECODECAPS CUVIDDECODECAPS;
struct Video_format;

/**
 * @brief See guide for NV12 decoding here: https://docs.nvidia.com/video-codec-sdk/nvdec-video-decoder-api-prog-guide/index.html
*/
class Decoder {
public:
	Decoder() {}
	~Decoder();
	bool init();
	bool decode(unsigned char* data, int data_size);
private:
	void print_device_info();
	CUresult create_video_parser(const Video_format& video_format, void*& video_parser);
	CUresult get_decode_cababilities(const Video_format& video_format, CUVIDDECODECAPS& decode_capabilities);
	CUresult create_decoder(const Video_format& video_format);
};