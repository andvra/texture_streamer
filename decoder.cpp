#include <functional>
#include <vector>
#include <map>
#include <iostream>

#include <cuviddec.h>
#include <nvcuvid.h>

#include "decoder.h"
#include "stream_info.h"

// TODO: These should be member variables instead
CUvideoparser video_parser;
CUvideodecoder video_decoder = {};
CUVIDDECODECAPS decode_capabilities = {};
CUcontext cuda_context;
CUdevice cuda_device;

/**
 * @brief Used for convenience instead of Stream_info. This struct holds
 * all the data types needed for the decoder, so it's enough to convert once,
 * ie. when this struct is filled
*/
struct Video_format {
	cudaVideoCodec video_codec;
	cudaVideoChromaFormat chroma_format;
	/**
	 * @brief Eg. 0 for 8-bit or 2 for 10-bit
	*/
	unsigned int bit_depth_minus_8;
	unsigned int width;
	unsigned int height;
};

/**
 * @brief A collection of error codes from cuda.h, with a label so we can print
 * them in our debug messages
*/
std::map<int, std::string> cuda_errors = {
	{CUDA_ERROR_INVALID_VALUE, "CUDA_ERROR_INVALID_VALUE"},
	{CUDA_ERROR_INVALID_CONTEXT, "CUDA_ERROR_INVALID_CONTEXT"}
};

/**
 * @brief Print info about the decoding device
*/
void print_device_info();

/**
 * @brief Creates a decoder
 * @param video_format Video format
 * @result Cuvid result code
*/
CUresult create_decoder(const Video_format& video_format);

/**
 * @brief Gets the decode capabilities. Useful if you look for specific decode features
 * @param video_format Video format
 * @param decode_capabilities Reference to decode capabilities result object
 * @return Cuvid result code
*/
CUresult get_decode_cababilities(const Video_format& video_format, CUVIDDECODECAPS& decode_capabilities);

/**
 * @brief Initialize a video parser object
 * @param decoder The decoder calling this function
 * @param video_format Video format
 * @param video_parser Vide parser
 * @return Cuvid result code
*/
CUresult create_video_parser(Decoder* decoder, const Video_format& video_format, void*& video_parser);

/**
 * @brief See definition at 433 in nvcuvid.h. There, it's called PFNVIDSEQUENCECALLBACK
 * @param user_data Should be a pointer to an instance of the Decoder class
 * @param format Format structure, set by Nvidia
 * @return 
*/
int sequence_callback_proc(void* user_data, CUVIDEOFORMAT* format) {
	Decoder* decoder = static_cast<Decoder*>(user_data);

	return format->min_num_decode_surfaces;
}

/**
 * @brief 
 * @param user_data Should be a pointer to an instance of the Decoder class
 * @param params Parameter structure, set by Nvidia
 * @return 
*/
int decode_callback_proc(void* user_data, CUVIDPICPARAMS* params) {
	return 1;
}

/**
 * @brief 
 * @param user_data Should be a pointer to an instance of the Decoder class
 * @param display_info Display info structure, set by Nvidia
 * @return 
*/
int display_callback_proc(void* user_data, CUVIDPARSERDISPINFO* display_info) {
	return 1;
}

/**
 * @brief More info on Supplemental Enhancement Information: https://www.magewell.com/blog/82/detail
 * @param user_data Should be a pointer to an instance of the Decoder class
 * @param message_info Message info structure, set by Nvidia
 * @return 
*/
int get_sei_callback_proc(void* user_data, CUVIDSEIMESSAGEINFO* message_info) {
	return 1;
}

Decoder::~Decoder() {
	cuvidDestroyVideoParser(video_parser);
}

bool Decoder::init(const Stream_info& stream_info) {
	// More conversions can be found in function FFmpeg2NvCodecId here: https://github.com/NVIDIA/video-sdk-samples/blob/master/Samples/Utils/FFmpegDemuxer.h
	std::map<Codec_id, cudaVideoCodec> codec_map = {
		{Codec_id::h264, cudaVideoCodec::cudaVideoCodec_H264},
		{Codec_id::hevc, cudaVideoCodec::cudaVideoCodec_HEVC},
		{Codec_id::av1, cudaVideoCodec::cudaVideoCodec_AV1}
	};
	std::map<Pixel_format, cudaVideoChromaFormat> pixel_format_map = {
		{Pixel_format::yuv420, cudaVideoChromaFormat::cudaVideoChromaFormat_420}
	};

	if (codec_map.count(stream_info.codec_id) == 0) {
		std::cout << "Don't have a codec corresponding to codec_id " << static_cast<int>(stream_info.codec_id) << " (yet)" << std::endl;
		return false;
	}

	if (pixel_format_map.count(stream_info.pixel_format) == 0) {
		std::cout << "Don't have a chroma format corresponding to pixel_format " << static_cast<int>(stream_info.pixel_format) << " (yet)" << std::endl;
		return false;
	}

	Video_format video_format = {
		codec_map[stream_info.codec_id],
		pixel_format_map[stream_info.pixel_format],
		stream_info.bits_per_raw_pixel - 8,
		stream_info.width,
		stream_info.height
	};

	typedef std::function<int()> decoder_fn;
	typedef std::pair<std::string, decoder_fn> fn_with_label;

	// Use cuDevicePrimaryCtxRetain over cuCtxCreate()! See here https://docs.nvidia.com/cuda/cuda-driver-api/group__CUDA__CTX.html#group__CUDA__CTX_1g65dc0012348bc84810e2103a40d8e2cf
	// We can set flags using cuDevicePrimaryCtxSetFlags
	unsigned int api_version;

	// TODO: Found this flag is used in the AppDecGL sample, so I copied it
	unsigned int context_flags = CU_CTX_SCHED_BLOCKING_SYNC;

	// TODO: Add cuDeviceGetCount?
	std::vector<fn_with_label> fns = {
		{"Initializing CUDA",				[]() { return cuInit(0); }},
		{"Printing device info",			[this]() { print_device_info(); return CUDA_SUCCESS; }},
		{"Getting device",					[]() { return cuDeviceGet(&cuda_device, 0); }},
		//{"Getting device context",			[&cuda_context, &cuda_device]() { return cuDevicePrimaryCtxRetain(&cuda_context, cuda_device); }},
		{"Getting device context",			[&context_flags]() { return cuCtxCreate_v2(&cuda_context, context_flags, cuda_device); }},
		{"Getting API version",				[&api_version]() { return cuCtxGetApiVersion(cuda_context, &api_version); }},
		{"Printing API version",			[&api_version]() { std::cout << "API version: " << api_version << std::endl; return CUDA_SUCCESS; }},
		{"Creating video parser",			[this, &video_format]() { return create_video_parser(this, video_format, video_parser); }},
		{"Getting decoder capabilities",	[this, &video_format]() { return get_decode_cababilities(video_format, decode_capabilities); }},
		{"Creating decoder",				[this, &video_format]() { return create_decoder(video_format); }}
	};

	for (auto& [the_label, the_function] : fns) {
		auto ret = the_function();
		if (ret != CUDA_SUCCESS) {
			std::cout << the_label << " failed. Error code was " << ret << " (" << (cuda_errors.count(ret) > 0 ? cuda_errors[ret] : "see cudaError_enum") << ")" << std::endl;
			return false;
		}
		else {
			// TODO: Not the best code; this will be removed soon but needed to find out how to get setup to work!
			std::cout << the_label << ": SUCCESS" << std::endl;
		}
	}

	return true;
}

void print_device_info() {
	const unsigned int max_name_length = 100;
	int device_count;
	CUdevice cuda_device;
	char device_name[max_name_length];

	auto res = cuDeviceGetCount(&device_count);

	for (int idx_device = 0; idx_device < device_count; idx_device++) {
		res = cuDeviceGet(&cuda_device, idx_device);
		cuDeviceGetName(device_name, max_name_length, cuda_device);
		std::cout << "Device name: " << device_name << std::endl;
	}
}

CUresult create_video_parser(Decoder* decoder, const Video_format& video_format, void*& video_parser) {
	CUVIDPARSERPARAMS params = {};

	params.CodecType = video_format.video_codec;
	params.ulMaxNumDecodeSurfaces = 1; // This is a dummy value. The actual value is received in pfnSequenceCallback
	params.ulMaxDisplayDelay = 0;
	params.pUserData = decoder;
	params.pfnSequenceCallback = sequence_callback_proc;
	params.pfnDecodePicture = decode_callback_proc;
	params.pfnDisplayPicture = display_callback_proc;
	params.pfnGetSEIMsg = get_sei_callback_proc;

	return cuvidCreateVideoParser(&video_parser, &params);
}

CUresult get_decode_cababilities(const Video_format& video_format, CUVIDDECODECAPS& decode_capabilities) {
	decode_capabilities.eCodecType = video_format.video_codec;
	decode_capabilities.eChromaFormat = video_format.chroma_format;
	decode_capabilities.nBitDepthMinus8 = video_format.bit_depth_minus_8;

	auto ret = cuvidGetDecoderCaps(&decode_capabilities);

	if (ret == CUDA_SUCCESS) {
		if (!decode_capabilities.bIsSupported) {
			std::cout << "Codec not supported" << std::endl;
		}
		if ((video_format.width > decode_capabilities.nMaxWidth)
			|| (video_format.height > decode_capabilities.nMaxHeight)) {
			std::cout << "Resolution not supported" << std::endl;
		}
		if ((video_format.width >> 4) * (video_format.height >> 4) > decode_capabilities.nMaxMBCount) {
			std::cout << "MBCount not supported" << std::endl;
		}
	}

	return ret;
}

CUresult create_decoder(const Video_format& video_format) {
	CUVIDDECODECREATEINFO create_info = {};

	create_info.bitDepthMinus8 = video_format.bit_depth_minus_8;
	create_info.ChromaFormat = video_format.chroma_format;
	create_info.CodecType = video_format.video_codec;
	create_info.ulWidth = video_format.width;
	create_info.ulHeight = video_format.height;
	// Set the max size to another value if we want to support video that change size
	create_info.ulMaxWidth = video_format.width;
	create_info.ulMaxHeight = video_format.height;
	create_info.ulTargetWidth = video_format.width;
	create_info.ulTargetHeight = video_format.height;
	// TODO: This value  can be set manually, but should atleast be CUVIDEOFORMAT::min_num_decode_surfaces. See documentation
	create_info.ulNumDecodeSurfaces = 10;
	create_info.ulNumOutputSurfaces = 1;
	// TODO: Not sure if this is correct, but it will render CUDA_ERROR_NOT_SUPPORTED if it's wrong
	//	so fine to use it for now!
	create_info.OutputFormat = cudaVideoSurfaceFormat::cudaVideoSurfaceFormat_NV12;
	create_info.DeinterlaceMode = cudaVideoDeinterlaceMode_enum::cudaVideoDeinterlaceMode_Adaptive;

	return cuvidCreateDecoder(&video_decoder, &create_info);
}

bool Decoder::decode(unsigned char* data, int data_size) {
	CUVIDSOURCEDATAPACKET data_packet = {};

	data_packet.payload = data;
	data_packet.payload_size = data_size;

	auto ret = cuvidParseVideoData(video_parser, &data_packet);

	if (ret != CUDA_SUCCESS) {
		std::cout << "Could not parse packet" << std::endl;
		return false;
	}

	return true;
}
