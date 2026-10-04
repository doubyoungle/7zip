// SmartExtract.h

#ifndef ZIP7_INC_SMART_EXTRACT_H
#define ZIP7_INC_SMART_EXTRACT_H

#include "../../../Common/MyString.h"

#include "../../../Windows/FileIO.h"

#include "LoadCodecs.h"
#include "OpenArchive.h"

namespace NSmartExtract {

enum ESmartMode
{
  kSmart_ExtractHere,     // a single top-level item:
                          // extract to the directory that stores the archive
  kSmart_ExtractToFolder  // several top-level items, an empty archive or a
                          // listing failure: extract to a subfolder named
                          // after the archive (the fallback mode)
};

struct CPlan
{
  ESmartMode Mode;
  bool Listed;        // false, if the archive could not be opened or listed
                      // (for example, encrypted headers without a password)
  bool IsCompound;    // true, if the archive is a compressed wrapper that
                      // contains a single archive item (foo.tar.gz -> foo.tar).
                      // The wrapped item was extracted to TempPath, and
                      // TempPath must be extracted by this plan.
  UString InnerName;  // the name of the wrapped item (for IsCompound)
  FString TempPath;   // the path of the extracted wrapped item (for IsCompound)
  UString FolderName; // the name of the subfolder for kSmart_ExtractToFolder

  CPlan(): Mode(kSmart_ExtractToFolder), Listed(false), IsCompound(false) {}
};

/* MakePlan() opens the archive (without password requests), calculates the
   smart extraction plan for it, and (for compound archives) extracts the
   wrapped item to tempDir.
   tempDir must exist. It returns S_OK also when the archive could not be
   listed; in that case plan.Listed = false (the fallback mode). */

HRESULT MakePlan(CCodecs *codecs, const FString &arcPath,
    const FString &tempDir, CPlan &plan);

/* MakePlanFromArc() is similar, but it uses the already opened archive. */

HRESULT MakePlanFromArc(CCodecs *codecs, const CArchiveLink &arcLink,
    const UString &arcName, const FString &tempDir, CPlan &plan);

/* OpenArchiveFileNoPassword() opens the archive without password requests
   (the archive with encrypted headers cannot be opened with it). */

HRESULT OpenArchiveFileNoPassword(CCodecs *codecs, const FString &arcPath,
    CArchiveLink &arcLink);

/* GetExtractFolderName() calculates the name of the subfolder for
   "extract to folder" mode:
     foo.zip    -> foo
     foo.7z     -> foo
     foo.tar.gz -> foo
     foo.7z.001 -> foo
   It doesn't include the path of the archive. */

UString GetExtractFolderName(const UString &arcName);

}

#endif
