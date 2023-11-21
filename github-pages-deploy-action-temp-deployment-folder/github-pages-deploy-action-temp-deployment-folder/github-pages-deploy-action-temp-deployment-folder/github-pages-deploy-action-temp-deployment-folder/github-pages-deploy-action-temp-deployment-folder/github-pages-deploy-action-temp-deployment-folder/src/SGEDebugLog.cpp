/** @file SGEDebugLog.cpp
 *
 *  Provides debug output to a log, for target device SW development.
 *
 *  Copyright (c) 2023, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include <e32std.h>
#include <f32file.h>
#include "SGEDebugLog.h"

EXPORT_C RSGEDebugLog::RSGEDebugLog()
{
}

EXPORT_C RSGEDebugLog::~RSGEDebugLog()
{
}

EXPORT_C TInt Open()
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C void RSGEDebugLog::Close()
{
}

EXPORT_C TInt RSGEDebugLog::WriteLine(const char* aText, TLogLevel aLevel = EAll)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt RSGEDebugLog::WriteLine(const char* aText, TInt aInt, TLogLevel aLevel = EAll)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt RSGEDebugLog::WriteLine(const char* aText, TUint32 aHex, TLogLevel aLevel = EAll)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt RSGEDebugLog::Write(const char* aText, TLogLevel aLevel = EAll)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt RSGEDebugLog::Write(const TInt aInt, TLogLevel aLevel = EAll)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt RSGEDebugLog::Write(const TUint32 aHex, TLogLevel aLevel = EAll)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt RSGEDebugLog::Write(const TUint8 aHex, TLogLevel aLevel = EAll)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt RSGEDebugLog::Write(const char aChar, TLogLevel aLevel = EAll)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt RSGEDebugLog::Write(const TDesC& aDesC, TLogLevel aLevel = EAll)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt RSGEDebugLog::NewLine(TLogLevel aLevel = EAll)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C void RSGEDebugLog::SetLoggingLevel(TLogLevel aLevel)
{
}
