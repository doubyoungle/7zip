// SmartExtract.cpp

#include "StdAfx.h"

#include "../../../Common/MyCom.h"
#include "../../../Common/StringConvert.h"

#include "../../../Windows/FileDir.h"
#include "../../../Windows/FileName.h"
#include "../../../Windows/PropVariant.h"

#include "../../Archive/IArchive.h"
#include "../../IPassword.h"
#include "../../PropID.h"

#include "ExtractingFilePath.h"
#include "SmartExtract.h"

using namespace NWindows;
using namespace NWindows::NCOM;

namespace NSmartExtract {

/* This callback never provides a password. So archives with encrypted
   headers cannot be opened, and the listing fails for them.
   That failure is treated as the reason to use the fallback mode. */

class CNoPasswordOpenCallback Z7_final:
  public IArchiveOpenCallback,
  public ICryptoGetTextPassword,
  public CMyUnknownImp
{
  Z7_COM_QI_BEGIN2(IArchiveOpenCallback)
  Z7_COM_QI_ENTRY(ICryptoGetTextPassword)
  Z7_COM_QI_END
  Z7_COM_ADDREF_RELEASE

  Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
  Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)
};

Z7_COM7F_IMF(CNoPasswordOpenCallback::SetTotal(const UInt64 * /* totalFiles */, const UInt64 * /* totalBytes */))
  { return S_OK; }

Z7_COM7F_IMF(CNoPasswordOpenCallback::SetCompleted(const UInt64 * /* completeFiles */, const UInt64 * /* completeBytes */))
  { return S_OK; }

Z7_COM7F_IMF(CNoPasswordOpenCallback::CryptoGetTextPassword(BSTR * /* password */))
  { return E_NOTIMPL; }


/* This callback writes the single extracted item to a file.
   It is used to unpack the wrapped archive from compound archives
   like foo.tar.gz to a temporary file. */

class CSingleItemDumpCallback Z7_final:
  public IArchiveExtractCallback,
  public CMyUnknownImp
{
  Z7_COM_QI_BEGIN2(IArchiveExtractCallback)
  Z7_COM_QI_END
  Z7_COM_ADDREF_RELEASE

  FString _filePath;
  CMyComPtr<ISequentialOutStream> _outStream;
  HRESULT _result;
public:
  CSingleItemDumpCallback(const FString &filePath): _filePath(filePath), _result(S_OK) {}

  Z7_IFACE_COM7_IMP(IProgress)
  Z7_IFACE_COM7_IMP(IArchiveExtractCallback)
};

Z7_COM7F_IMF(CSingleItemDumpCallback::SetTotal(UInt64 /* total */))
  { return S_OK; }

Z7_COM7F_IMF(CSingleItemDumpCallback::SetCompleted(const UInt64 * /* completeValue */))
  { return S_OK; }

Z7_COM7F_IMF(CSingleItemDumpCallback::GetStream(UInt32 /* index */, ISequentialOutStream **outStream, Int32 askExtractMode))
{
  COM_TRY_BEGIN
  *outStream = NULL;
  if (askExtractMode != NArchive::NExtract::NAskMode::kExtract)
    return S_OK;
  COutFileStream *outStreamSpec = new COutFileStream;
  _outStream = outStreamSpec;
  if (!outStreamSpec->Create_ALWAYS_or_Open_ALWAYS(_filePath, true))
    _result = GetLastError_noZero_HRESULT();
  *outStream = _outStream.Detach();
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(CSingleItemDumpCallback::PrepareOperation(Int32 /* askExtractMode */))
  { return S_OK; }

Z7_COM7F_IMF(CSingleItemDumpCallback::SetOperationResult(Int32 opRes))
{
  if (opRes != NArchive::NExtract::NOperationResult::kOK)
    _result = E_FAIL;
  return S_OK;
}


static bool IsKnownArcExt(const UString &ext)
{
  static const char * const kArcExts[] =
  {
      "7z"
    , "bz2"
    , "bzip2"
    , "cab"
    , "gz"
    , "gzip"
    , "jar"
    , "lz"
    , "lz4"
    , "lzma"
    , "rar"
    , "tgz"
    , "tar"
    , "wim"
    , "xz"
    , "zip"
    , "zst"
  };
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kArcExts); i++)
    if (ext.IsEqualTo_Ascii_NoCase(kArcExts[i]))
      return true;
  return false;
}


static bool IsCompoundWrapperExt(const UString &ext)
{
  static const char * const kWrapperExts[] =
  {
      "bz2"
    , "gz"
    , "gzip"
    , "lz"
    , "lzma"
    , "tgz"
    , "xz"
    , "zst"
  };
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kWrapperExts); i++)
    if (ext.IsEqualTo_Ascii_NoCase(kWrapperExts[i]))
      return true;
  return false;
}


/* IsCompoundWrapper() checks that the archive is a compressed wrapper
   that contains a single archive item:
     foo.tar.gz  contains  foo.tar
     foo.tgz     contains  foo.tar
     backup.cpio.xz  contains backup.cpio ... etc.
   The wrapped item must be extracted first, and then it can be
   processed as an archive. */

static bool IsCompoundWrapper(const UString &arcName, const UString &itemName)
{
  const int dotPos = arcName.ReverseFind_Dot();
  if (dotPos <= 0)
    return false;
  const UString ext = arcName.Ptr((unsigned)dotPos + 1);
  if (!IsCompoundWrapperExt(ext))
    return false;
  UString base = arcName.Left((unsigned)dotPos);
  base.TrimRight();
  if (base.IsEmpty())
    return false;
  UString expected = base;
  if (ext.IsEqualTo_Ascii_NoCase("tgz"))
    expected += ".tar";
  if (!itemName.IsEqualTo_NoCase(expected))
    return false;
  const int dotPos2 = itemName.ReverseFind_Dot();
  if (dotPos2 <= 0)
    return false;
  const UString innerExt = itemName.Ptr((unsigned)dotPos2 + 1);
  return IsKnownArcExt(innerExt);
}


static HRESULT GetTopLevelItems_I(IInArchive *archive, UStringVector &topItems)
{
  topItems.Clear();
  UInt32 numItems = 0;
  RINOK(archive->GetNumberOfItems(&numItems))
  for (UInt32 i = 0; i < numItems; i++)
  {
    CPropVariant prop;
    RINOK(archive->GetProperty(i, kpidPath, &prop))
    if (prop.vt != VT_BSTR || !prop.bstrVal)
      continue;
    const UString path = prop.bstrVal;
    // we skip items with empty path (they have no top-level item)
    if (path.IsEmpty())
      continue;
    int sepPos = path.Find(L'/');
    const int sepPos2 = path.Find(L'\\');
    if (sepPos < 0 || (sepPos2 >= 0 && sepPos2 < sepPos))
      sepPos = sepPos2;
    const UString top = (sepPos < 0) ? path : path.Left((unsigned)sepPos);
    // we skip items with path like "/foo" (empty top-level item)
    if (top.IsEmpty())
      continue;
    // exact string deduplication is used, so we keep the logic predictable
    bool wasAdded = false;
    const unsigned num = topItems.Size();
    for (unsigned k = 0; k < num; k++)
      if (topItems[k] == top)
      {
        wasAdded = true;
        break;
      }
    if (!wasAdded)
      topItems.Add(top);
  }
  return S_OK;
}


/* Dumps one item of the archive to a file (used for compound archives) */

static HRESULT DumpItem(IInArchive *archive, UInt32 index, const FString &filePath)
{
  CSingleItemDumpCallback *dumpCallbackSpec = new CSingleItemDumpCallback(filePath);
  CMyComPtr<IArchiveExtractCallback> dumpCallback = dumpCallbackSpec;
  const UInt32 indices[1] = { index };
  return archive->Extract(indices, 1, false, dumpCallback);
}


HRESULT OpenArchiveFileNoPassword(CCodecs *codecs, const FString &arcPath, CArchiveLink &arcLink)
{
  CInFileStream *inStreamSpec = new CInFileStream;
  CMyComPtr<IInStream> inStream = inStreamSpec;
  if (!inStreamSpec->Open(arcPath))
    return GetLastError_noZero_HRESULT();

  // note: CArchiveLink::Open() dereferences (op.types) and
  // (op.excludedFormats) without NULL checks, so we provide empty lists
  CObjectVector<COpenType> types;
  CIntVector excludedFormats;

  COpenOptions op;
  op.codecs = codecs;
  op.openType.FormatIndex = -1; // open by extension of the file name
  op.types = &types;
  op.excludedFormats = &excludedFormats;
  op.props = NULL;
  op.stream = inStream;
  op.filePath = fs2us(arcPath);

  // the callback object must be allocated in the heap, because the
  // archive handlers can store the callback pointer and release it later
  CNoPasswordOpenCallback *openCallbackSpec = new CNoPasswordOpenCallback;
  CMyComPtr<IArchiveOpenCallback> openCallback = openCallbackSpec;
  op.callback = openCallbackSpec;

  return arcLink.Open(op);
}


/* Calculates the plan by the list of top-level items of the archive.
   If the archive is a compound wrapper with a single archive item,
   and the item can be extracted to tempDir, then the plan is calculated
   for the wrapped archive (recursively, but without nested compound
   handling). */

static HRESULT MakePlanForItems(CCodecs *codecs, IInArchive *archive,
    const UString &arcName, const UStringVector &topItems,
    const FString &tempDir, CPlan &plan)
{
  plan.Listed = true;
  plan.Mode = kSmart_ExtractToFolder;
  plan.FolderName = GetExtractFolderName(arcName);

  if (topItems.Size() == 1)
  {
    if (IsCompoundWrapper(arcName, topItems[0]))
    {
      // find the wrapped item and dump it to the temporary folder
      UInt32 numItems = 0;
      RINOK(archive->GetNumberOfItems(&numItems))
      UInt32 dumpIndex = 0;
      bool found = false;
      for (UInt32 i = 0; i < numItems && !found; i++)
      {
        CPropVariant prop;
        RINOK(archive->GetProperty(i, kpidPath, &prop))
        if (prop.vt == VT_BSTR && prop.bstrVal
            && topItems[0].IsEqualTo_NoCase(prop.bstrVal))
        {
          dumpIndex = i;
          found = true;
        }
      }
      if (found)
      {
        FString dir = tempDir;
        NFile::NName::NormalizeDirPathPrefix(dir);
        const FString tempPath = dir + us2fs(topItems[0]);
        if (DumpItem(archive, dumpIndex, tempPath) == S_OK)
        {
          plan.IsCompound = true;
          plan.InnerName = topItems[0];
          plan.TempPath = tempPath;
          plan.FolderName = GetExtractFolderName(topItems[0]);
          // the mode is calculated by the wrapped archive contents.
          // Recursion depth is limited: the wrapped archive is not
          // handled as a compound wrapper again.
          CArchiveLink innerLink;
          if (OpenArchiveFileNoPassword(codecs, tempPath, innerLink) == S_OK)
          {
            UStringVector innerTopItems;
            if (GetTopLevelItems_I(innerLink.GetArchive(), innerTopItems) == S_OK)
              plan.Mode = (innerTopItems.Size() == 1) ? kSmart_ExtractHere : kSmart_ExtractToFolder;
          }
          return S_OK;
        }
      }
      // the wrapped item could not be dumped:
      // use the plain single-item logic for the outer archive
    }
    plan.Mode = kSmart_ExtractHere;
  }
  return S_OK;
}


HRESULT MakePlanFromArc(CCodecs *codecs, const CArchiveLink &arcLink, const UString &arcName,
    const FString &tempDir, CPlan &plan)
{
  plan.Listed = false;
  plan.IsCompound = false;
  plan.Mode = kSmart_ExtractToFolder;
  plan.FolderName = GetExtractFolderName(arcName);
  if (!arcLink.IsOpen || arcLink.Arcs.Size() == 0)
    return S_OK;
  UStringVector topItems;
  if (GetTopLevelItems_I(arcLink.GetArchive(), topItems) != S_OK)
    return S_OK;
  return MakePlanForItems(codecs, arcLink.GetArchive(), arcName,
      topItems, tempDir, plan);
}


HRESULT MakePlan(CCodecs *codecs, const FString &arcPath,
    const FString &tempDir, CPlan &plan)
{
  plan.Listed = false;
  plan.IsCompound = false;
  plan.Mode = kSmart_ExtractToFolder;

  UString arcName = fs2us(arcPath);
  {
    const int sepPos = arcName.ReverseFind_PathSepar();
    if (sepPos >= 0)
      arcName = arcName.Ptr((unsigned)(sepPos + 1));
  }
  plan.FolderName = GetExtractFolderName(arcName);

  CArchiveLink arcLink;
  if (OpenArchiveFileNoPassword(codecs, arcPath, arcLink) != S_OK)
    return S_OK; // plan.Listed = false: the fallback mode

  UStringVector topItems;
  if (GetTopLevelItems_I(arcLink.GetArchive(), topItems) != S_OK)
    return S_OK; // plan.Listed = false: the fallback mode

  return MakePlanForItems(codecs, arcLink.GetArchive(), arcName,
      topItems, tempDir, plan);
}


UString GetExtractFolderName(const UString &arcName)
{
  UString name = arcName;
  for (;;)
  {
    const int dotPos = name.ReverseFind_Dot();
    if (dotPos <= 0)
      break;
    const UString ext = name.Ptr((unsigned)dotPos + 1);
    const UString base = name.Left((unsigned)dotPos);
    if (base.IsEmpty())
      break;
    // volume suffixes: "foo.7z.001" -> "foo.7z", "backup.001" -> "backup"
    if (ext.IsEqualTo_Ascii_NoCase("001"))
    {
      name = base;
      continue;
    }
    // multi-volume rar: "foo.part1.rar" -> "foo.part1" -> "foo"
    if (ext.IsEqualTo_Ascii_NoCase("rar"))
    {
      const int dotPos2 = base.ReverseFind_Dot();
      if (dotPos2 > 0)
      {
        const UString mid = base.Ptr((unsigned)dotPos2 + 1);
        if (mid.IsPrefixedBy_Ascii_NoCase("part")
            && mid.Len() > 4)
        {
          bool isDigits = true;
          for (unsigned k = 4; k < mid.Len(); k++)
            if (mid[k] < '0' || mid[k] > '9')
              { isDigits = false; break; }
          if (isDigits)
          {
            name = base.Left((unsigned)dotPos2);
            continue;
          }
        }
      }
    }
    if (IsKnownArcExt(ext))
    {
      name = base;
      continue;
    }
    break;
  }
  if (name.IsEmpty())
    name = arcName;
  return Get_Correct_FsFile_Name(name);
}

}
