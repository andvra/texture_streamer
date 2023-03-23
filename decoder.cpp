#include <functional>
#include <vector>
#include <map>
#include <iostream>

#include <cuviddec.h>
#include <nvcuvid.h>

#include "decoder.h"

// TODO: These should be member variables instead
CUvideoparser video_parser;
CUvideodecoder video_decoder = {};
CUVIDDECODECAPS decode_capabilities = {};
CUcontext cuda_context;
CUdevice cuda_device;

struct Video_format {
	cudaVideoCodec video_codec;
	cudaVideoChromaFormat chroma_format;
	/**
	 * @brief Eg. 0 for 8-bit or 2 for 10-bit
	*/
	int bit_depth_minus_8;
	int width;
	int height;
};

std::map<int, std::string> cuda_errors = {
	{CUDA_ERROR_INVALID_VALUE, "CUDA_ERROR_INVALID_VALUE"},
	{CUDA_ERROR_INVALID_CONTEXT, "CUDA_ERROR_INVALID_CONTEXT"}
};

// See definition at 433 in nvcuvid.h. There, it's called PFNVIDSEQUENCECALLBACK
int sequence_callback(void* user_data, CUVIDEOFORMAT* format) {
	return  format->min_num_decode_surfaces;
}

int decode_callback(void* user_data, CUVIDPICPARAMS* params) {
	return 1;
}

int display_callback(void* user_data, CUVIDPARSERDISPINFO* display_info) {
	return 1;
}

int get_sei_callback(void* user_data, CUVIDSEIMESSAGEINFO* display_info) {
	return 1;
}

Decoder::~Decoder() {
	cuvidDestroyVideoParser(video_parser);
}

bool Decoder::init() {
	// TODO: Initialize these parameters based on video metadata
	Video_format video_format = {
		cudaVideoCodec::cudaVideoCodec_H264,
		cudaVideoChromaFormat::cudaVideoChromaFormat_420,
		0,
		1920 * 2,
		1080 * 2
	};

	typedef std::function<int()> decoder_fn;
	typedef std::pair<std::string, decoder_fn> fn_with_label;

	// Use cuDevicePrimaryCtxRetain over cuCtxCreate()! See here https://docs.nvidia.com/cuda/cuda-driver-api/group__CUDA__CTX.html#group__CUDA__CTX_1g65dc0012348bc84810e2103a40d8e2cf
	// We can set flags using cuDevicePrimaryCtxSetFlags
	unsigned int api_version;

	// TODO: Found this flag is used in the AppDecGL sample, so I copied it
	unsigned int context_flags = CU_CTX_SCHED_BLOCKING_SYNC;

	// TODO: Add cuDeviceGetCount
	std::vector<fn_with_label> fns = {
		{"Initializing CUDA",				[]() { return cuInit(0); }},
		{"Printing device info",			[this]() { print_device_info(); return CUDA_SUCCESS; }},
		{"Getting device",					[]() { return cuDeviceGet(&cuda_device, 0); }},
		//{"Getting device context",			[&cuda_context, &cuda_device]() { return cuDevicePrimaryCtxRetain(&cuda_context, cuda_device); }},
		{"Getting device context",			[&context_flags]() { return cuCtxCreate_v2(&cuda_context, context_flags, cuda_device); }},
		{"Getting API version",				[&api_version]() { return cuCtxGetApiVersion(cuda_context, &api_version); }},
		{"Printing API version",			[&api_version]() { std::cout << "API version: " << api_version << std::endl; return CUDA_SUCCESS; }},
		{"Creating video parser",			[this, &video_format]() { return create_video_parser(video_format, video_parser); }},
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

void Decoder::print_device_info() {
	int device_count;
	CUdevice cuda_device;
	char device_name[100];

	auto res = cuDeviceGetCount(&device_count);

	for (int idx_device = 0; idx_device < device_count; idx_device++) {
		res = cuDeviceGet(&cuda_device, idx_device);
		cuDeviceGetName(device_name, 100, cuda_device);
		std::cout << "Device name: " << device_name << std::endl;
	}
}

CUresult Decoder::create_video_parser(const Video_format& video_format, void*& video_parser) {
	CUVIDPARSERPARAMS params = {};

	params.CodecType = video_format.video_codec;
	params.ulMaxNumDecodeSurfaces = 1; // This is a dummy value. The actual value is received in pfnSequenceCallback
	params.ulMaxDisplayDelay = 0;
	params.pfnSequenceCallback = sequence_callback;
	params.pfnDecodePicture = decode_callback;
	params.pfnDisplayPicture = display_callback;
	params.pfnGetSEIMsg = get_sei_callback;

	return cuvidCreateVideoParser(&video_parser, &params);
}

CUresult Decoder::get_decode_cababilities(const Video_format& video_format, CUVIDDECODECAPS& decode_capabilities) {
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

CUresult Decoder::create_decoder(const Video_format& video_format) {
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
