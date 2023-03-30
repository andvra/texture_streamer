#pragma once

/**
 * @brief Here, unsupported means that we haven't added a conversion yet. Add when needed
*/
enum class Codec_id {
	unsupported,
	h264,
	hevc,
	av1
};

/**
 * @brief @brief Here, unsupported means that we haven't added a conversion yet. Add when needed
*/
enum class Pixel_format {
	unsupported,
	yuv420
};

struct Stream_info {
	Codec_id codec_id;
	Pixel_format pixel_format;
	unsigned int width;
	unsigned int height;
	unsigned int bits_per_raw_pixel;
};