#include <iostream>
#include <functional>
#include <map>

#include <cuviddec.h>
#include <nvcuvid.h>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/codec_par.h>
}

struct VideoFormat {
	cudaVideoCodec video_codec;
	cudaVideoChromaFormat chroma_format;
	/**
	 * @brief Eg. 0 for 8-bit or 2 for 10-bit
	*/
	int bit_depth_minus_8;
	int width;
	int height;
};

CUvideodecoder video_decoder = {};

// TODO: Can we have these as members instead?
CUVIDDECODECAPS decode_capabilities = {};
CUcontext cuda_context;
CUdevice cuda_device;
CUvideoparser video_parser;

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

CUresult create_video_parser(const VideoFormat& video_format, CUvideoparser& video_parser) {
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

CUresult parse_packet(CUVIDSOURCEDATAPACKET* packet) {
	return cuvidParseVideoData(video_parser, packet);
}

CUresult get_decode_cababilities(const VideoFormat& video_format, CUVIDDECODECAPS& decode_capabilities) {
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

void print_device_info() {
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

CUresult create_decoder(const VideoFormat& video_format) {
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

bool decode(unsigned char* data, int data_size) {
	CUVIDSOURCEDATAPACKET data_packet = {};

	data_packet.payload = data;
	data_packet.payload_size = data_size;

	auto ret = parse_packet(&data_packet);

	if (ret != CUDA_SUCCESS) {
		std::cout << "Could not parse packet" << std::endl;
		return false;
	}

	return true;
}

std::map<int, std::string> cuda_errors = {
	{CUDA_ERROR_INVALID_VALUE, "CUDA_ERROR_INVALID_VALUE"},
	{CUDA_ERROR_INVALID_CONTEXT, "CUDA_ERROR_INVALID_CONTEXT"}
};


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



// See guide here: https://docs.nvidia.com/video-codec-sdk/nvdec-video-decoder-api-prog-guide/index.html
bool decode_init() {

	VideoFormat video_format = {
		cudaVideoCodec::cudaVideoCodec_H264,
		cudaVideoChromaFormat::cudaVideoChromaFormat_420,
		0,
		1920*2,
		1080*2
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
		{"Printing device info",			[]() { print_device_info(); return CUDA_SUCCESS; }},
		{"Getting device",					[]() { return cuDeviceGet(&cuda_device, 0); }},
		//{"Getting device context",			[&cuda_context, &cuda_device]() { return cuDevicePrimaryCtxRetain(&cuda_context, cuda_device); }},
		{"Getting device context",			[&context_flags]() { return cuCtxCreate_v2(&cuda_context, context_flags, cuda_device); }},
		{"Getting API version",				[&api_version]() { return cuCtxGetApiVersion(cuda_context, &api_version); }},
		{"Printing API version",			[&api_version]() { std::cout << "API version: " << api_version << std::endl; return CUDA_SUCCESS; }},
		{"Creating video parser",			[&video_format]() { return create_video_parser(video_format, video_parser); }},
		{"Getting decoder capabilities",	[&video_format]() { return get_decode_cababilities(video_format, decode_capabilities); }},
		{"Creating decoder",				[&video_format]() { return create_decoder(video_format); }}
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

void decode_deinit() {
	auto ret = cuvidDestroyVideoParser(video_parser);
}

void demux(std::string input_file, std::function<bool(unsigned char*, int)> callback_decode) {
	AVFormatContext* format_context = avformat_alloc_context();
	int ret{};

	if (avformat_open_input(&format_context, input_file.c_str(), nullptr, nullptr) < 0) {
		std::cout << "Could not open input file " << input_file << std::endl;
		return;
	}

	if (avformat_find_stream_info(format_context, nullptr) < 0) {
		std::cout << "Could not find stream info" << std::endl;
	}

	av_dump_format(format_context, 0, input_file.c_str(), 0);

	//auto frame = av_frame_alloc();
	//if (!frame) {
	//	std::cout << "Can't allocated frame" << std::endl;
	//	return;
	//}

	auto packet = av_packet_alloc();
	if (!packet) {
		std::cout << "Can't allocate packet" << std::endl;
		return;
	}

	// Only used for H264, se FFmpegDemuxer.h in the samples!
	const AVBitStreamFilter* bsf = av_bsf_get_by_name("h264_mp4toannexb");
	AVBSFContext* bsfc = nullptr;
	av_bsf_alloc(bsf, &bsfc);
	avcodec_parameters_copy(bsfc->par_in, format_context->streams[0]->codecpar); // TODO Right now, we hard-code video stream index here,
	av_bsf_init(bsfc);

	AVPacket* packet_filtered = av_packet_alloc();

	int cnt = 0;
	while (av_read_frame(format_context, packet) >= 0) {
		if (packet->stream_index == 0) {
			// TODO: Make sure to use the correct stream index. Just guessing that stream 0 is video
			if (packet_filtered->data) {
				av_packet_unref(packet_filtered);
			}
			int buf_size_original = packet->buf->size;

			// Only used for H264, se FFmpegDemuxer.h in the samples!
			av_bsf_send_packet(bsfc, packet);
			av_bsf_receive_packet(bsfc, packet_filtered);

			std::cout << "Buffer size: " << buf_size_original << ". Filtered: " << packet_filtered->buf->size << std::endl;

			//if (!callback_decode(packet->buf->data, packet->buf->size)) {
			if (!callback_decode(packet_filtered->buf->data, packet_filtered->buf->size)) {
				return;
			}
		}
		std::cout << "Stream index: " << packet->stream_index << std::endl;
		av_packet_unref(packet);
		if (cnt++ > 1500) {
			// TODO: Here, we stop after a while! This is just for debugging
			std::cout << "cnt: " << cnt << std::endl;
			break;
		}
	}

	av_packet_free(&packet);
	//av_frame_free(&frame);
	avformat_close_input(&format_context);
}

int main()
{
	std::string input_file = R"(C:\Users\andre\Downloads\Tutorial1.mp4)";
	if (!decode_init()) {
		return 1;
	}

	demux(input_file, decode);
	decode_deinit();
	std::cout << "Done" << std::endl;

	return 0;
}
