/** @file GameInstaller.cpp
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "GameInstaller.h"
#include <bautils.h>
#include <f32file.h>

_LIT(KDefaultTargetDir, "E:\\System\\Libs\\");
_LIT(KTargetName, "gamecomms.dll");

static TInt DoInstallL(RFs &aFs, TInt aVariant, const TDesC &aAppFullName, const char *aTargetDir)
{
    TFileName KTargetDir;
    if (aTargetDir) {
        KTargetDir.Copy(TPtrC8((const TUint8 *)aTargetDir).Left(KMaxFileName - 16));
    } else {
        KTargetDir.Copy(KDefaultTargetDir);
    }
    TFileName KTargetFile(KTargetDir);
    KTargetFile.Append(KTargetName);

    TParsePtrC app(aAppFullName);
    TFileName src;
    src.Copy(app.DriveAndPath());
    src.Append(_L("dlls\\gcv"));
    src.AppendNum(aVariant);
    src.Append(_L(".dll"));

    TEntry srcEntry;
    TInt err = aFs.Entry(src, srcEntry);
    if (err != KErrNone) {
        return err;
    }

    BaflUtils::EnsurePathExistsL(aFs, KTargetDir);

    CFileMan *fileMan = CFileMan::NewL(aFs);
    CleanupStack::PushL(fileMan);
    err = fileMan->Copy(src, KTargetFile, CFileMan::EOverWrite);
    CleanupStack::PopAndDestroy(); /* fileMan */
    if (err != KErrNone) {
        return err;
    }

    /* the copy must be complete, a half written DLL would crash the game */
    TEntry dstEntry;
    err = aFs.Entry(KTargetFile, dstEntry);
    if (err == KErrNone && dstEntry.iSize != srcEntry.iSize) {
        err = KErrCorrupt;
    }
    return err;
}

TInt GameInstaller::Install(TInt aVariant, const TDesC &aAppFullName, const char *aTargetDir)
{
    if (aVariant < 1 || aVariant > 11) {
        return KErrArgument;
    }
    RFs fs;
    TInt err = fs.Connect();
    if (err != KErrNone) {
        return err;
    }
    TInt result = KErrNone;
    DoInstallL(fs, aVariant, aAppFullName, aTargetDir));
    fs.Close();
    return err != KErrNone ? err : result;
}
