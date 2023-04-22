#include <functional>
#include <vector>
#include <map>
#include <iostream>
#include <cstring>
#include <filesystem>

#include <cuviddec.h>
#include <nvcuvid.h>

#include "decoder.h"
#include "stream_info.h"

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

CUvideoparser video_parser;
CUvideodecoder video_decoder = {};
CUVIDDECODECAPS decode_capabilities = {};
CUcontext cuda_context;
CUdevice cuda_device;
CUstream cuvid_stream;

Video_format video_format;

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
CUresult create_decoder();

/**
 * @brief Gets the decode capabilities. Useful if you look for specific decode features
 * @param video_format Video format
 * @param decode_capabilities Reference to decode capabilities result object
 * @return Cuvid result code
*/
CUresult get_decode_cababilities(CUVIDDECODECAPS& decode_capabilities);

/**
 * @brief Initialize a video parser object
 * @param decoder The decoder calling this function
 * @param video_format Video format
 * @param video_parser Vide parser
 * @return Cuvid result code
*/
CUresult create_video_parser(Decoder* decoder);

std::string get_video_codec_name(cudaVideoCodec codec_id) {
	std::map<cudaVideoCodec, std::string> codecs = {
		{ cudaVideoCodec_MPEG1,     "MPEG-1"       },
		{ cudaVideoCodec_MPEG2,     "MPEG-2"       },
		{ cudaVideoCodec_MPEG4,     "MPEG-4 (ASP)" },
		{ cudaVideoCodec_VC1,       "VC-1/WMV"     },
		{ cudaVideoCodec_H264,      "AVC/H.264"    },
		{ cudaVideoCodec_JPEG,      "M-JPEG"       },
		{ cudaVideoCodec_H264_SVC,  "H.264/SVC"    },
		{ cudaVideoCodec_H264_MVC,  "H.264/MVC"    },
		{ cudaVideoCodec_HEVC,      "H.265/HEVC"   },
		{ cudaVideoCodec_VP8,       "VP8"          },
		{ cudaVideoCodec_VP9,       "VP9"          },
		{ cudaVideoCodec_AV1,       "AV1"          },
		{ cudaVideoCodec_NumCodecs, "Invalid"      },
		{ cudaVideoCodec_YUV420,    "YUV  4:2:0"   },
		{ cudaVideoCodec_YV12,      "YV12 4:2:0"   },
		{ cudaVideoCodec_NV12,      "NV12 4:2:0"   },
		{ cudaVideoCodec_YUYV,      "YUYV 4:2:2"   },
		{ cudaVideoCodec_UYVY,      "UYVY 4:2:2"   }
	};

	if (codecs.count(codec_id) > 0) {
		return codecs[codec_id];
	}

	return "Unknown";
}

std::string get_chroma_format_name(cudaVideoChromaFormat chroma_format_id) {
	std::map<cudaVideoChromaFormat, std::string> chroma_formats = {
		{ cudaVideoChromaFormat_Monochrome, "YUV 400 (Monochrome)" },
		{ cudaVideoChromaFormat_420,        "YUV 420"              },
		{ cudaVideoChromaFormat_422,        "YUV 422"              },
		{ cudaVideoChromaFormat_444,        "YUV 444"              }
	};

	if (chroma_formats.count(chroma_format_id) > 0) {
		return chroma_formats[chroma_format_id];
	}

	return "Unknown";
}

/**
 * @brief See definition at 433 in nvcuvid.h. There, it's called PFNVIDSEQUENCECALLBACK
 * @param user_data Should be a pointer to an instance of the Decoder class
 * @param format Format structure, set by Nvidia
 * @return 
*/
int sequence_callback_proc(void* user_data, CUVIDEOFORMAT* format) {
	Decoder* decoder = static_cast<Decoder*>(user_data);
	std::stringstream ss;

	ss << "Video Input Information" << std::endl
		<< "\tCodec        : " << get_video_codec_name(format->codec) << std::endl
		<< "\tFrame rate   : " << format->frame_rate.numerator << "/" << format->frame_rate.denominator
		<< " = " << 1.0 * format->frame_rate.numerator / format->frame_rate.denominator << " fps" << std::endl
		<< "\tSequence     : " << (format->progressive_sequence ? "Progressive" : "Interlaced") << std::endl
		<< "\tCoded size   : [" << format->coded_width << ", " << format->coded_height << "]" << std::endl
		<< "\tDisplay area : [" << format->display_area.left << ", " << format->display_area.top << ", "
		<< format->display_area.right << ", " << format->display_area.bottom << "]" << std::endl
		<< "\tChroma       : " << get_chroma_format_name(format->chroma_format) << std::endl
		<< "\tBit depth    : " << format->bit_depth_luma_minus8 + 8;

	std::cout << ss.str() << std::endl;

	CUVIDDECODECAPS decode_caps;
	decode_caps.eCodecType = format->codec;
	decode_caps.eChromaFormat = format->chroma_format;
	decode_caps.nBitDepthMinus8 = format->bit_depth_luma_minus8;

	cuCtxPushCurrent(cuda_context);
	cuvidGetDecoderCaps(&decode_caps);
	cuCtxPopCurrent(nullptr);


	std::map<std::string, bool> error_conditions = {
		{ "Codec not supported on this GPU", !decode_caps.bIsSupported },
		{ "Resolution not supported", (format->coded_width > decode_caps.nMaxWidth) || (format->coded_height > decode_caps.nMaxHeight) },
		{ "Macroblock (MBCount) not supported", (format->coded_width >> 4) * (format->coded_height >> 4) > decode_caps.nMaxMBCount}
	};

	for (auto [msg, c] : error_conditions) {
		if (c) {
			std::cout << msg << std::endl;
			return format->min_num_decode_surfaces;
		}
	}


	return format->min_num_decode_surfaces;
}

/**
 * @brief 
 * @param user_data Should be a pointer to an instance of the Decoder class
 * @param params Parameter structure, set by Nvidia
 * @return 1 on success, 0 otherwise.
*/
int decode_callback_proc(void* user_data, CUVIDPICPARAMS* params) {
	cuCtxPushCurrent(cuda_context);
	cuvidDecodePicture(video_decoder, params);
	cuCtxPopCurrent(nullptr);

	// NB If we want zero-latency, we could call display_callback_proc directly.
	//	Question: What are the disadvantages? Maybe frames are not displayed in the right order?

	return 1;
}

/**
 * @brief 
 * @param user_data Should be a pointer to an instance of the Decoder class
 * @param display_info Display info structure, set by Nvidia
 * @return 
*/
int display_callback_proc(void* user_data, CUVIDPARSERDISPINFO* display_info) {
	CUdeviceptr source_frame_ptr = 0;
	unsigned int source_pitch = 0;
	CUVIDPROCPARAMS videoProcessingParameters = {};
	unsigned char* host_pointer = nullptr;

	videoProcessingParameters.progressive_frame = display_info->progressive_frame;
	videoProcessingParameters.second_field = display_info->repeat_first_field + 1;
	videoProcessingParameters.top_field_first = display_info->top_field_first;
	videoProcessingParameters.unpaired_field = display_info->repeat_first_field < 0;
	videoProcessingParameters.output_stream = cuvid_stream;

	auto res = cuCtxPushCurrent(cuda_context);
	
	res = cuvidMapVideoFrame(video_decoder, display_info->picture_index, &source_frame_ptr,
		&source_pitch, &videoProcessingParameters);

	CUVIDGETDECODESTATUS DecodeStatus;
	memset(&DecodeStatus, 0, sizeof(DecodeStatus));
	res = cuvidGetDecodeStatus(video_decoder, display_info->picture_index, &DecodeStatus);

	if (res == CUDA_SUCCESS && (DecodeStatus.decodeStatus == cuvidDecodeStatus_Error || DecodeStatus.decodeStatus == cuvidDecodeStatus_Error_Concealed))
	{
		//printf("Decode Error occurred for picture %d\n", m_nPicNumInDecodeOrder[display_info->picture_index]);
		std::cout << "Decode error occured for picture with picture index (not in order) " << display_info->picture_index << std::endl;
		cuvidUnmapVideoFrame(video_decoder, source_frame_ptr);
		return 0;
	}

	uint8_t* decoded_frame = nullptr;

	auto frame_size = (video_format.chroma_format == cudaVideoChromaFormat_444) ? source_pitch * (3 * video_format.height) :
		source_pitch * (video_format.height + (video_format.height + 1) / 2);
	// use CUDA based Device to Host memcpy
	res = cuMemAllocHost((void**)&host_pointer, frame_size);

	if (host_pointer)
	{
		res = cuMemcpyDtoH(host_pointer, source_frame_ptr, frame_size);
		std::filesystem::path full_path("c:\\temp\\yuv.yuv");
		auto xx = full_path.parent_path();
		auto yy = std::filesystem::is_directory(xx);
		if (!yy) {
			std::filesystem::create_directories(xx);
		}

		FILE* fp = fopen(full_path.string().c_str(), "wb");
		auto num_written = fwrite(host_pointer, 1, frame_size, fp);
		fclose(fp);

		res = cuvidUnmapVideoFrame(video_decoder, source_frame_ptr);
		cuMemFreeHost(host_pointer);
	}

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

	video_format = {
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
		{"Creating video parser",			[this]() { return create_video_parser(this); }},
		{"Getting decoder capabilities",	[this]() { return get_decode_cababilities(decode_capabilities); }},
		{"Creating decoder",				[this]() { return create_decoder(); }}
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

	cuStreamCreate(&cuvid_stream, CU_STREAM_DEFAULT);

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

CUresult create_video_parser(Decoder* decoder) {
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

CUresult get_decode_cababilities(CUVIDDECODECAPS& decode_capabilities) {
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

CUresult create_decoder() {
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
