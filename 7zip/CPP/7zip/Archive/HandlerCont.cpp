// HandlerCont.cpp

#include "StdAfx.h"

#include "../../Common/ComTry.h"

#include "../Common/LimitedStreams.h"
#include "../Common/ProgressUtils.h"
#include "../Common/StreamUtils.h"

#include "../Compress/CopyCoder.h"

#include "HandlerCont.h"

namespace NArchive {

STDMETHODIMP CHandlerCont::Extract(const UInt32 *indices, UInt32 numItems,
    Int32 testMode, IArchiveExtractCallback *extractCallback)
{
  COM_TRY_BEGIN
  bool allFilesMode = (numItems == (UInt32)(Int32)-1);
  if (allFilesMode)
  {
    RINOK(GetNumberOfItems(&numItems));
  }
  if (numItems == 0)
    return S_OK;
  UInt64 totalSize = 0;
  UInt32 i;
  for (i = 0; i < numItems; i++)
    totalSize += GetItemSize(allFilesMode ? i : indices[i]);
  extractCallback->SetTotal(totalSize);

  totalSize = 0;
  
  NCompress::CCopyCoder *copyCoderSpec = new NCompress::CCopyCoder();
  CMyComPtr<ICompressCoder> copyCoder = copyCoderSpec;

  CLocalProgress *lps = new CLocalProgress;
  CMyComPtr<ICompressProgressInfo> progress = lps;
  lps->Init(extractCallback, false);

  CLimitedSequentialInStream *streamSpec = new CLimitedSequentialInStream;
  CMyComPtr<ISequentialInStream> inStream(streamSpec);
  streamSpec->SetStream(_stream);

  for (i = 0; i < numItems; i++)
  {
    lps->InSize = totalSize;
    lps->OutSize = totalSize;
    RINOK(lps->SetCur());
    CMyComPtr<ISequentialOutStream> outStream;
    Int32 askMode = testMode ?
        NExtract::NAskMode::kTest :
        NExtract::NAskMode::kExtract;
    Int32 index = allFilesMode ? i : indices[i];
    
    RINOK(extractCallback->GetStream(index, &outStream, askMode));
    UInt64 size = GetItemSize(index);
    totalSize += size;
    if (!testMode && !outStream)
      continue;
    RINOK(extractCallback->PrepareOperation(askMode));

    RINOK(_stream->Seek(GetItemPos(index), STREAM_SEEK_SET, NULL));
    streamSpec->Init(size);
    RINOK(copyCoder->Code(inStream, outStream, NULL, NULL, progress));
    outStream.Release();
    int opRes = NExtract::NOperationResult::kDataError;
    if (copyCoderSpec->TotalSize == size)
      opRes = NExtract::NOperationResult::kOK;
    else if (copyCoderSpec->TotalSize < size)
      opRes = NExtract::NOperationResult::kUnexpectedEnd;
    RINOK(extractCallback->SetOperationResult(opRes));
  }
  
  return S_OK;
  COM_TRY_END
}

STDMETHODIMP CHandlerCont::GetStream(UInt32 index, ISequentialInStream **stream)
{
  COM_TRY_BEGIN
  // const CPartition &item = _items[index];
  return CreateLimitedInStream(_stream, GetItemPos(index), GetItemSize(index), stream);
  COM_TRY_END
}



STDMETHODIMP CHandlerImg::Seek(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition)
{
  switch (seekOrigin)
  {
    case STREAM_SEEK_SET: break;
    case STREAM_SEEK_CUR: offset += _virtPos; break;
    case STREAM_SEEK_END: offset += _size; break;
    default: return STG_E_INVALIDFUNCTION;
  }
  if (offset < 0)
    return HRESULT_WIN32_ERROR_NEGATIVE_SEEK;
  _virtPos = offset;
  if (newPosition)
    *newPosition = offset;
  return S_OK;
}

static const Byte k_GDP_Signature[] = { 'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T' };

static const char *GetImgExt(ISequentialInStream *stream)
{
  const size_t kHeaderSize = 1 << 10;
  Byte buf[kHeaderSize];
  if (ReadStream_FAIL(stream, buf, kHeaderSize) == S_OK)
  {
    if (buf[0x1FE] == 0x55 && buf[0x1FF] == 0xAA)
    {
      if (memcmp(buf + 512, k_GDP_Signature, sizeof(k_GDP_Signature)) == 0)
        return "gpt";
      return "mbr";
    }
  }
  return NULL;
}

void CHandlerImg::CloseAtError()
{
  Stream.Release();
}

STDMETHODIMP CHandlerImg::Open(IInStream *stream,
    const UInt64 * /* maxCheckStartPosition */,
    IArchiveOpenCallback * openCallback)
{
  COM_TRY_BEGIN
  {
    Close();
    HRESULT res;
    try
    {
      res = Open2(stream, openCallback);
      if (res == S_OK)
      {
        CMyComPtr<ISequentialInStream> inStream;
        HRESULT res2 = GetStream(0, &inStream);
        if (res2 == S_OK && inStream)
          _imgExt = GetImgExt(inStream);
        return S_OK;
      }
    }
    catch(...)
    {
      CloseAtError();
      throw;
    }
    CloseAtError();
    return res;
  }
  COM_TRY_END
}

STDMETHODIMP CHandlerImg::GetNumberOfItems(UInt32 *numItems)
{
  *numItems = 1;
  return S_OK;
}

STDMETHODIMP CHandlerImg::Extract(const UInt32 *indices, UInt32 numItems,
    Int32 testMode, IArchiveExtractCallback *extractCallback)
{
  COM_TRY_BEGIN
  if (numItems == 0)
    return S_OK;
  if (numItems != (UInt32)(Int32)-1 && (numItems != 1 || indices[0] != 0))
    return E_INVALIDARG;

  RINOK(extractCallback->SetTotal(_size));
  CMyComPtr<ISequentialOutStream> outStream;
  Int32 askMode = testMode ?
      NExtract::NAskMode::kTest :
      NExtract::NAskMode::kExtract;
  RINOK(extractCallback->GetStream(0, &outStream, askMode));
  if (!testMode && !outStream)
    return S_OK;
  RINOK(extractCallback->PrepareOperation(askMode));

  CLocalProgress *lps = new CLocalProgress;
  CMyComPtr<ICompressProgressInfo> progress = lps;
  lps->Init(extractCallback, false);

  int opRes = NExtract::NOperationResult::kDataError;
  
  CMyComPtr<ISequentialInStream> inStream;
  HRESULT hres = GetStream(0, &inStream);
  if (hres == S_FALSE)
    hres = E_NOTIMPL;

  if (hres == S_OK && inStream)
  {
    NCompress::CCopyCoder *copyCoderSpec = new NCompress::CCopyCoder();
    CMyComPtr<ICompressCoder> copyCoder = copyCoderSpec;

    hres = copyCoder->Code(inStream, outStream, NULL, &_size, progress);
    if (hres == S_OK)
    {
      if (copyCoderSpec->TotalSize == _size)
        opRes = NExtract::NOperationResult::kOK;
      else if (copyCoderSpec->TotalSize < _size)
        opRes = NExtract::NOperationResult::kUnexpectedEnd;
    }
  }

  inStream.Release();
  outStream.Release();
  
  if (hres != S_OK)
  {
    if (hres == S_FALSE)
      opRes = NExtract::NOperationResult::kDataError;
    else if (hres == E_NOTIMPL)
      opRes = NExtract::NOperationResult::kUnsupportedMethod;
    else
      return hres;
  }
 
  return extractCallback->SetOperationResult(opRes);
  COM_TRY_END
}

}
