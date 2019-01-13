#include <stdexcept>

#include "JpgEncoder.hpp"
#include "util/debug.h"



/*
  从Surface获取到得图片右侧带有黑边，nexus5机器上测试得到的原始RGBA图片大小为1152x1920
  原始RGBA图像的宽度计算不能直接取Minicap::Frame中的width，必须通过frame->bpp * frame->stride计算得到
  http://www.cnblogs.com/welhzh/p/4939613.html

  不同的芯片，在codec功能上存在差异，Nexus5只能编码YUV420SP编码的图片
  由于libturbo-jpge只支持RGBA到YUV420格式的转换。
  因此需要借助libyuv对libturbo-jpge编码后的YUV420进行缩放以及转换
  http://blog.csdn.net/zxccxzzxz/article/details/545644161111
  http://www.cnblogs.com/raomengyang/p/5582270.html

  以下yuv格式转换以及缩放的代码均来自yasea
  https://github.com/begeekmyfriend/yasea

  所以YUVEncoder中的代码我也只是知其然，不知其所以然 ＝ ＝！


*/
YUVEncoder::YUVEncoder(uint32 fourcc) :
	handle(tjInitCompress()),
	fourcc(fourcc),
	count(0)
{
	MCINFO("YUVEncoder created with corlor format %d", fourcc);
}

YUVEncoder::~YUVEncoder() {
	tjFree(rawFrame.data);
	tjFree(scaledFrame.data);
	tjFree(nvFrame.data);
}

/*
  RGB与YUV格式的文件，均有标准的计算公式
  RGB（长 * 宽 * 每个像素点的空间）
	RGBA_8888（长 * 宽 * 4）

  YUV（长 * 宽 * 3 / 2）

  因此从RGBA_8888 转换到YUV之后，体积已经减小了很多，比例为37.5%
  Nexus5上默认的RGBA_8888格式文件大小在8M左右，转换为YUV之后，体积缩小为3M

*/
bool
YUVEncoder::reserveData(uint32_t width, uint32_t height, float scale) {

	//rawFrame
	int resolution = width * height;
	rawFrame.height = height;
	rawFrame.width = width;
	rawFrame.size = tjBufSizeYUV2(width, 1/*1或4均可，但不能是0  */, height, TJSAMP_420);
	rawFrame.data = (unsigned char *)tjAlloc(rawFrame.size);
	if (rawFrame.data != NULL) {
		MCINFO("Alloc %d bytes buffer for yuv encoding raw buffer", rawFrame.size);
		rawFrame.y = rawFrame.data;
		rawFrame.u = rawFrame.y + resolution;
		rawFrame.v = rawFrame.u + resolution / 4;
	}

	int dest_width = width * scale;
	int dest_height = height * scale;
	int dest_resolution = dest_width * dest_height;
	YuvFrame *frames[] = {
	  &scaledFrame,
	  &nvFrame
	};
	MCINFO("Reserving scaled & vn12 buffer for resolution %dx%d ", dest_width, dest_height);
	for (int i = 0; i < 2; i++)
	{
		frames[i]->width = dest_width;
		frames[i]->height = dest_height;
		frames[i]->size = tjBufSizeYUV2(dest_width, 1/*1或4均可，但不能是0  */, dest_height, TJSAMP_420);
		frames[i]->data = (unsigned char *)tjAlloc(frames[i]->size);
		if (frames[i]->data != NULL) {
			MCINFO("Alloc %d bytes buffer for yuv encoding buffer[%d]", frames[i]->size, i);
			frames[i]->y = frames[i]->data;
			frames[i]->u = frames[i]->y + dest_width * dest_height;
			frames[i]->v = frames[i]->u + dest_width * dest_height / 4;
		}
	}

	return true;
}

bool YUVEncoder::encode(Minicap::Frame *frame) {
	MCINFO("Frame Format: %d\r\n", JpgEncoder::convertFormat(frame->format));

	//int ret = tjEncodeYUV3(handle, (unsigned char *)frame->data, frame->width, 
	//  frame->bpp * frame->stride, /* 设置为0等价于width * tjPixelSize[pixelFormat] */
	//  frame->height, TJPF_RGBA, rawFrame.data, 1, TJSAMP_420, TJFLAG_FASTDCT | TJFLAG_NOREALLOC);  
	int ret = ABGRToI420((uint8 *)frame->data, frame->bpp * frame->stride,
		rawFrame.y, rawFrame.width,
		rawFrame.u, rawFrame.width / 2,
		rawFrame.v, rawFrame.width / 2,
		frame->width, frame->height);
	if (ret < 0)
	{
		MCINFO("encode to yuv failed: %s\n", tjGetErrorStr());
	}


	ret = I420Scale(rawFrame.y, rawFrame.width,
		rawFrame.u, rawFrame.width / 2,
		rawFrame.v, rawFrame.width / 2,
		false ? -rawFrame.width : rawFrame.width, rawFrame.height,
		scaledFrame.y, scaledFrame.width,
		scaledFrame.u, scaledFrame.width / 2,
		scaledFrame.v, scaledFrame.width / 2,
		scaledFrame.width, scaledFrame.height,
		kFilterNone);

	//uint32 fourcc = FOURCC_NV12;

	ret = ConvertFromI420(scaledFrame.y, scaledFrame.width,
		scaledFrame.u, scaledFrame.width / 2,
		scaledFrame.v, scaledFrame.width / 2,
		nvFrame.data, nvFrame.width,
		nvFrame.width, nvFrame.height,
		fourcc);

	MCINFO("ret = [%d],[%d]Raw data encode into %dK yuv data!", ret, count++, nvFrame.size / 1024);
	return ret == 0;
}
int
YUVEncoder::getEncodedSize() {
	return nvFrame.size;
}
unsigned char*
YUVEncoder::getEncodedData() {
	return nvFrame.data;
}

int YUVEncoder::trgb2yuv(Minicap::Frame *frame, YuvFrame *yuvFrame, YuvFrame *scaledFrame, YuvFrame *nvFrame)
{
	int subsample = TJSAMP_420;
	unsigned char *yuv_buffer = NULL;
	unsigned long yuv_size = 0;

	int flags = 0;
	int padding = 1; // 
	int pixelfmt = TJPF_RGBA;
	int ret = 0;

	flags |= 0;
	int resolution = frame->height * frame->width;
	yuvFrame->height = frame->height;
	yuvFrame->width = frame->width;
	yuvFrame->size = tjBufSizeYUV2(frame->width, padding, frame->height, subsample);

	//printf("Alloc %d bytes buffer for yuv file", yuvFrame->size);
	yuvFrame->data = (unsigned char *)tjAlloc(yuvFrame->size);
	yuvFrame->y = yuvFrame->data;
	yuvFrame->u = yuvFrame->y + resolution;
	yuvFrame->v = yuvFrame->u + resolution / 4;
	if (yuvFrame->data == NULL)
	{
		MCINFO("malloc buffer for rgb failed.\n");
		return -1;
	}
	ret = tjEncodeYUV3(handle, (unsigned char *)frame->data, frame->width,
		frame->bpp * frame->stride, /* 设置为0等价于width * tjPixelSize[pixelFormat] */
		frame->height, pixelfmt, yuvFrame->data, padding, subsample, flags);
	if (ret < 0)
	{
		MCINFO("encode to yuv failed: %s\n", tjGetErrorStr());
	}
	/*
	nvFrame->size = yuvFrame->size;
	nvFrame->data = yuvFrame->data;
	*/

	int dest_width = 432;
	int dest_height = 768;
	scaledFrame->size = dest_width * dest_height * 3 / 2;//tjBufSizeYUV2(frame->width, padding, frame->height, subsample);  
	scaledFrame->width = dest_width;
	scaledFrame->height = dest_height;
	//1080x1920x3/2 = 3110400 与libtrubojpge转出来的格式大小相等
	scaledFrame->data = (unsigned char *)tjAlloc(scaledFrame->size);

	scaledFrame->y = scaledFrame->data;
	scaledFrame->u = scaledFrame->y + dest_width * dest_height;
	scaledFrame->v = scaledFrame->u + dest_width * dest_height / 4;


	ret = I420Scale(yuvFrame->y, yuvFrame->width,
		yuvFrame->u, yuvFrame->width / 2,
		yuvFrame->v, yuvFrame->width / 2,
		false ? -yuvFrame->width : yuvFrame->width, yuvFrame->height,
		scaledFrame->y, scaledFrame->width,
		scaledFrame->u, scaledFrame->width / 2,
		scaledFrame->v, scaledFrame->width / 2,
		scaledFrame->width, scaledFrame->height,
		kFilterNone);

	nvFrame->size = dest_width * dest_height * 3 / 2;//tjBufSizeYUV2(frame->width, padding, frame->height, subsample);  
	nvFrame->width = dest_width;
	nvFrame->height = dest_height;
	//1080x1920x3/2 = 3110400 与libtrubojpge转出来的格式大小相等
	nvFrame->data = (unsigned char *)tjAlloc(nvFrame->size);

	nvFrame->y = nvFrame->data;
	nvFrame->u = nvFrame->y + dest_width * dest_height;
	nvFrame->v = nvFrame->u + dest_width * dest_height / 4;


	ret = ConvertFromI420(scaledFrame->y, scaledFrame->width,
		scaledFrame->u, scaledFrame->width / 2,
		scaledFrame->v, scaledFrame->width / 2,
		nvFrame->data, nvFrame->width,
		nvFrame->width, nvFrame->height,
		FOURCC('N', 'V', '1', '2'));

	tjDestroy(handle);
	return ret;
}



ScalingFactor::ScalingFactor(const tjscalingfactor *pFactor) :
	_pFactor(pFactor) {

}
ScalingFactor::~ScalingFactor() {

}
//缩放比例
float ScalingFactor::scalingPercentage() {
	return (float)_pFactor->num / (float)_pFactor->denom;
}

//分子 
int ScalingFactor::num() {
	return _pFactor->num;
}

//分母
int ScalingFactor::denom() {
	return _pFactor->denom;
}


int ScalingFactor::loadScalingFactors() {
	return 0;
}


Resizer::Resizer(int sampleType) :
	mSubsampling(sampleType)
{

}



bool Resizer::resize(Minicap::Frame *pFrame, unsigned char **ppBuffer, unsigned long *pBufferSize) {

	mCompressedDataSize = tjBufSize(
		pFrame->width,
		pFrame->height,
		mSubsampling
	);

	MCINFO("Allocating %ld bytes for JPG resizer", mCompressedDataSize);

	tjhandle _tjCompressHandler = tjInitCompress();
	mCompressedData = tjAlloc(mCompressedDataSize);
	int ret = tjCompress2(
		_tjCompressHandler,
		(unsigned char*)pFrame->data,
		pFrame->width,
		pFrame->stride * pFrame->bpp,
		pFrame->height,
		JpgEncoder::convertFormat(pFrame->format),
		&mCompressedData,
		&mCompressedDataSize,
		mSubsampling,
		100,
		TJFLAG_FASTDCT | TJFLAG_NOREALLOC
	);

	printf("%d from tjCompress2\r\n", ret);

	int width = 540;
	int height = 960;
	int pixelType = TJPF_GRAY;
	int pixelSize = tjPixelSize[pixelType];
	mDecompressDataSize = width * pixelSize * height;
	mDecompressData = tjAlloc(mDecompressDataSize);
	tjhandle tjDecompressHandler = tjInitDecompress();

	ret = tjDecompress2(
		tjDecompressHandler,
		mCompressedData, mCompressedDataSize,
		mDecompressData,
		width, pixelSize, height,
		pixelType,
		TJFLAG_FASTDCT);

	printf("%d from tjDecompress2\r\n", ret);
	ret = tjCompress2(
		_tjCompressHandler,
		mDecompressData,
		width,
		pixelSize * width,
		height,
		TJPF_RGBA,
		ppBuffer,
		pBufferSize,
		mSubsampling,
		10,
		TJFLAG_FASTDCT | TJFLAG_NOREALLOC
	);

	printf("%d from tjCompress2: %s\r\n", ret, tjGetErrorStr());
	return ret == 0;
}

JpgEncoder::JpgEncoder(unsigned int prePadding, unsigned int postPadding, unsigned int sampling, float scaling)
	: mTjCompressHandle(tjInitCompress()),
	mTjDecompressHandle(tjInitDecompress()),
	mScaling(scaling),
	mSubsampling(sampling),
	mEncodedData(NULL),
	mPrePadding(prePadding),
	mPostPadding(postPadding),
	mMaxWidth(0),
	mMaxHeight(0),
	mCompressBuffer(NULL),
	mDecompressBuffer(NULL)

{
}

JpgEncoder::~JpgEncoder() {

	//printf("%x\r\n", mCompressBuffer);
	tjFree(mCompressBuffer);
	//printf("%x\r\n", mDecompressBuffer);
	tjFree(mDecompressBuffer);
	//printf("%x\r\n", mEncodedData);
	tjFree(mEncodedData);
}

bool
JpgEncoder::encode(Minicap::Frame* frame, unsigned int quality) {
	/*
	  unsigned char *mCompressBuffer;
	  unsigned long mCompressBufferSize;

	  unsigned char *mDecompressBuffer;
	  unsigned long mDecompressBufferSize;
	*/

	unsigned long compressDataSize = 0;
	int ret = tjCompress2(
		mTjCompressHandle,
		(unsigned char*)frame->data,
		frame->width,
		frame->stride * frame->bpp,
		frame->height,
		convertFormat(frame->format),
		&mCompressBuffer,
		&compressDataSize,
		mSubsampling,
		100,
		TJFLAG_FASTDCT | TJFLAG_NOREALLOC
	);

	MCINFO("Encoding raw data with info Width: %d, Heigth: %d, BytePerPixel: %d, RawSize: %d, EncodedSize: %d\r\n",
		frame->width, frame->height, frame->bpp, frame->size / 1024, compressDataSize / 1024);

	int width = frame->width * mScaling;
	int height = frame->height * mScaling;
	int pixelType = convertFormat(frame->format);
	int pixelSize = tjPixelSize[pixelType];

	ret = tjDecompress2(mTjDecompressHandle, mCompressBuffer, compressDataSize, mDecompressBuffer, width, width*pixelSize, height, convertFormat(frame->format), TJFLAG_FASTDCT | TJFLAG_NOREALLOC);

	unsigned char* offset = getEncodedData();

	ret = tjCompress2(
		mTjCompressHandle,
		mDecompressBuffer,
		width,
		width * pixelSize,
		height,
		convertFormat(frame->format),
		&offset,
		&mEncodedSize,
		mSubsampling,
		quality,
		TJFLAG_FASTDCT | TJFLAG_NOREALLOC
	);

	//return false;
	return ret == 0;
}

int
JpgEncoder::getEncodedSize() {
	return mEncodedSize;
}

unsigned char*
JpgEncoder::getEncodedData() {
	return mEncodedData + mPrePadding;
}

//TODO修正缓冲区大小为图片缩放后重新计算得到的缓冲区大小
bool
JpgEncoder::reserveData(uint32_t width, uint32_t height) {
	//printf("reserver data buffer for %dx%d", width, height);
	if (width == mMaxWidth && height == mMaxHeight) {
		return 0;
	}

	tjFree(mCompressBuffer);

	mCompressBufferSize = tjBufSize(
		width,
		height,
		mSubsampling
	);
	MCINFO("Allocating %ld bytes for JPG resizer compression", mCompressBufferSize);
	mCompressBuffer = tjAlloc(mCompressBufferSize);
	//printf("%x\r\n", mCompressBuffer);


	tjFree(mDecompressBuffer);
	mDecompressBufferSize = tjBufSize(
		width,
		height,
		mSubsampling
	);
	mDecompressBufferSize = (width*mScaling) * 4 * (height*mScaling);
	MCINFO("Allocating %ld bytes for JPG resizer decompression", mDecompressBufferSize);
	mDecompressBuffer = tjAlloc(mDecompressBufferSize);
	//printf("%x\r\n", mDecompressBuffer);


	tjFree(mEncodedData);

	unsigned long maxSize = mPrePadding + mPostPadding + tjBufSize(
		width,
		height,
		mSubsampling
	);

	MCINFO("Allocating %ld bytes for JPG encoder", maxSize);

	mEncodedData = tjAlloc(maxSize);
	//printf("%x\r\n", mEncodedData);
	if (mEncodedData == NULL) {
		return false;
	}

	mMaxWidth = width;
	mMaxHeight = height;

	return true;
}

int
JpgEncoder::convertFormat(Minicap::Format format) {
	switch (format) {
	case Minicap::FORMAT_RGBA_8888:
		return TJPF_RGBA;
	case Minicap::FORMAT_RGBX_8888:
		return TJPF_RGBX;
	case Minicap::FORMAT_RGB_888:
		return TJPF_RGB;
	case Minicap::FORMAT_BGRA_8888:
		return TJPF_BGRA;
	default:
		throw std::runtime_error("Unsupported pixel format");
	}
}

const char *
JpgEncoder::convertSampling(int n) {
	switch (n) {
		/**
		 * 4:4:4 chrominance subsampling (no chrominance subsampling).  The JPEG or
		 * YUV image will contain one chrominance component for every pixel in the
		 * source image.
		 */
	case TJSAMP_444:
		return "TJSAMP_444";
		/**
		 * 4:2:2 chrominance subsampling.  The JPEG or YUV image will contain one
		 * chrominance component for every 2x1 block of pixels in the source image.
		 */
	case TJSAMP_422:
		return "TJSAMP_422";
		/**
		 * 4:2:0 chrominance subsampling.  The JPEG or YUV image will contain one
		 * chrominance component for every 2x2 block of pixels in the source image.
		 */
	case TJSAMP_420:
		return "TJSAMP_422";
		/**
		 * Grayscale.  The JPEG or YUV image will contain no chrominance components.
		 */
	case TJSAMP_GRAY:
		return "TJSAMP_422";
		/**
		 * 4:4:0 chrominance subsampling.  The JPEG or YUV image will contain one
		 * chrominance component for every 1x2 block of pixels in the source image.
		 *
		 * @note 4:4:0 subsampling is not fully accelerated in libjpeg-turbo.
		 */
	case TJSAMP_440:
		return "TJSAMP_422";
		/**
		 * 4:1:1 chrominance subsampling.  The JPEG or YUV image will contain one
		 * chrominance component for every 4x1 block of pixels in the source image.
		 * JPEG images compressed with 4:1:1 subsampling will be almost exactly the
		 * same size as those compressed with 4:2:0 subsampling, and in the
		 * aggregate, both subsampling methods produce approximately the same
		 * perceptual quality.  However, 4:1:1 is better able to reproduce sharp
		 * horizontal features.
		 *
		 * @note 4:1:1 subsampling is not fully accelerated in libjpeg-turbo.
		 */
	case TJSAMP_411:
		return "TJSAMP_422";

	default:
		return "UNKNOWN";
	}
}