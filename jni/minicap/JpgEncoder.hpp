#ifndef MINICAP_JPG_ENCODER_HPP
#define MINICAP_JPG_ENCODER_HPP

#include <turbojpeg.h>
#include <libyuv.h>
#include "Minicap.hpp"
using namespace libyuv;
class ScalingFactor {
public:
  ScalingFactor(const tjscalingfactor *pFactor);
  ~ScalingFactor();

  static int loadScalingFactors();

  //缩放比例
  float scalingPercentage();

  //分子 
  int num();

  //分母
  int denom();

private:
  const tjscalingfactor *_pFactor;
};

class Resizer {

public:
  Resizer(int mSubsampling);

  bool 
  resize(Minicap::Frame *pFrame,  unsigned char **ppBuffer, unsigned long *pBufferSize);

  bool
  reserveData(uint32_t width, uint32_t height);

private:
  int mSubsampling;
  unsigned char *mCompressedData;
  unsigned long mCompressedDataSize;
  unsigned char *mDecompressData;
  unsigned long mDecompressDataSize;
};


struct YuvFrame {
    int width;
    int height;
    int size;
    unsigned char *data;
    unsigned char *y;
    unsigned char *u;
    unsigned char *v;
};

class YUVEncoder {
public:
  YUVEncoder(uint32 fourcc);
  ~YUVEncoder();

  bool
  reserveData(uint32_t width, uint32_t height, float scale);

  bool 
  encode(Minicap::Frame *frame);

  int
  getEncodedSize();

  unsigned char*
  getEncodedData();
/*
  struct Frame {
    void const* data;
    Format format;
    uint32_t width;
    uint32_t height;    
    uint32_t stride;//stride指在内存中每行像素所占的空间,因为涉及到内存对其的问题，所以stride／bpp并不能代表图片的宽度
    uint32_t bpp;//Byte per pixel
    size_t size;
  };
  */
  int trgb2yuv(Minicap::Frame *frame, YuvFrame *yuvFrame, YuvFrame *scaledFrame, YuvFrame *nvFrame);

  uint32 fourcc;
  tjhandle handle;
  YuvFrame rawFrame;
  YuvFrame scaledFrame;
  YuvFrame nvFrame;
  unsigned int count;
};


class JpgEncoder {
public:
  JpgEncoder(unsigned int prePadding, unsigned int postPadding, unsigned int sampling = TJSAMP_420, float scaling = 1);

  ~JpgEncoder();

  bool
  encode(Minicap::Frame* frame, unsigned int quality);

  int
  getEncodedSize();

  unsigned char*
  getEncodedData();

  bool
  reserveData(uint32_t width, uint32_t height);

private:
  tjhandle mTjCompressHandle;
  tjhandle mTjDecompressHandle;
  int mSubsampling;
  float mScaling;
  unsigned int mPrePadding;
  unsigned int mPostPadding;
  unsigned int mMaxWidth;
  unsigned int mMaxHeight;
  unsigned char* mEncodedData;
  unsigned long mEncodedSize;

  unsigned char *mCompressBuffer;
  unsigned long mCompressBufferSize;

  unsigned char *mDecompressBuffer;
  unsigned long mDecompressBufferSize;

public:
  static int
  convertFormat(Minicap::Format format);

  static const char *
  convertSampling(int n);
  
};

#endif
