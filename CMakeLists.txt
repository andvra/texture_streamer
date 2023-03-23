cmake_minimum_required (VERSION 3.8)

project ("test-nvidia-codec")

find_package(CUDAToolkit REQUIRED)
find_package(FFmpeg REQUIRED)

if(NOT DEFINED ENV{NV12_SDK_ROOT})
    message(FATAL_ERROR "No NV12 SDK found. Set the environment variable NV12_SDK_ROOT, eg. C:/dev/Video_Codec_SDK_12.0.16")
endif()

# Make sure to convert path to using front slashes, so CMAKE can handle it
file(TO_CMAKE_PATH $ENV{NV12_SDK_ROOT} NV12_SDK_ROOT)
set(NV12_INCLUDE_PATH ${NV12_SDK_ROOT}/Interface)
set(NV12_LIB_PATH ${NV12_SDK_ROOT}/lib/x64/)
set(NV12_LIBS ${NV12_LIB_PATH}/nvcuvid.lib)

message(STATUS "Found NV12 SDK at: ${NV12_SDK_ROOT}")

# Add source to this project's executable.
add_executable (test-nvidia-codec)

target_sources(test-nvidia-codec PRIVATE "main.cpp" "decoder.cpp" "demuxer.cpp")

target_include_directories(test-nvidia-codec 
	PRIVATE
		${NV12_INCLUDE_PATH}
		${CUDAToolkit_INCLUDE_DIRS}
		${FFMPEG_INCLUDE_DIRS}
)

MESSAGE(STATUS "FFMPEG_LIBRARIES: ${FFMPEG_LIBRARIES}")

target_link_libraries(test-nvidia-codec 
	PRIVATE
		${NV12_LIBS}
		CUDA::cuda_driver
		${FFMPEG_LIBRARIES}
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET test-nvidia-codec PROPERTY CXX_STANDARD 20)
endif()
