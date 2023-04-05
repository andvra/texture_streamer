cmake_minimum_required (VERSION 3.10)

project ("test-nvidia-codec")

find_package(CUDAToolkit REQUIRED)
if(WIN32)
	find_package(FFMPEG REQUIRED)
else()
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
		libavdevice
		libavfilter
		libavformat
		libavcodec
		libswresample
		libswscale
		libavutil
	)
endif()

if(NOT DEFINED ENV{NV12_SDK_ROOT})
    message(FATAL_ERROR "Environment variable NV12_SDK_ROOT not set. Should be the path to the Video codec SDK, eg. C:/dev/Video_Codec_SDK_12.0.16")
endif()

# Make sure to convert path to using front slashes, so CMAKE can handle it
file(TO_CMAKE_PATH $ENV{NV12_SDK_ROOT} NV12_SDK_ROOT)

if(NOT EXISTS ${NV12_SDK_ROOT})
	message(FATAL_ERROR "No NV12 SDK found at ${NV12_SDK_ROOT}")
endif()

if(WIN32)
	set(NV12_INCLUDE_PATH ${NV12_SDK_ROOT}/Interface)
	set(NV12_LIB_PATH ${NV12_SDK_ROOT}/lib/x64/)
	set(NV12_LIBS ${NV12_LIB_PATH}/nvcuvid.lib)
else()
	set(NV12_INCLUDE_PATH ${NV12_SDK_ROOT}/Interface)
	set(NV12_LIB_PATH ${NV12_SDK_ROOT}/Lib/linux/stubs/x86_64)
	set(NV12_LIBS ${NV12_LIB_PATH}/libnvcuvid.so)
endif()

message(STATUS "Found NV12 SDK at: ${NV12_SDK_ROOT}")

# Add source to this project's executable.
add_executable (test-nvidia-codec)

target_sources(test-nvidia-codec PRIVATE "main.cpp" "decoder.cpp" "demuxer.cpp" "utils.cpp")

target_include_directories(test-nvidia-codec 
	PRIVATE
		${NV12_INCLUDE_PATH}
		${CUDAToolkit_INCLUDE_DIRS}
		${FFMPEG_INCLUDE_DIRS}
)

if(WIN32)
	set(ALL_LIBS ${NV12_LIBS} CUDA::cuda_driver ${FFMPEG_LIBRARIES})
else()
	set(ALL_LIBS ${NV12_LIBS} CUDA::cuda_driver PkgConfig::LIBAV)
endif()

target_link_libraries(test-nvidia-codec 
	PRIVATE
		${ALL_LIBS}
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET test-nvidia-codec PROPERTY CXX_STANDARD 20)
endif()
