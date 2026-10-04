// CompressCall.h

#ifndef ZIP7_INC_COMPRESS_CALL_H
#define ZIP7_INC_COMPRESS_CALL_H

#include "../../../Common/MyString.h"

UString GetQuotedString(const UString &s);

HRESULT CompressFiles(
    const UString &arcPathPrefix,
    const UString &arcName,
    const UString &arcType,
    bool addExtension,
    const UStringVector &names,
    bool email, bool showDialog, bool waitFinish);

/* smartMode: the output directory for every archive is chosen by
   the top-level items of that archive (see SmartExtract.h);
   the (outFolder) value is ignored in that case. */

void ExtractArchives(const UStringVector &arcPaths, const UString &outFolder,
    bool showDialog, bool elimDup, UInt32 writeZone, bool smartMode = false);
void TestArchives(const UStringVector &arcPaths, bool hashMode = false);

void CalcChecksum(const UStringVector &paths,
    const UString &methodName,
    const UString &arcPathPrefix,
    const UString &arcFileName);

void Benchmark(bool totalMode);

#endif
