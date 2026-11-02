/** @file GcConfig.cpp
 *
 *  Minimal INI reader/writer (no estlib dependency).
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include <f32file.h>
#include "GcConfig.h"

_LIT(KIniCard, "E:\\GameComms.ini");
_LIT(KIniPhone, "C:\\System\\Data\\GameComms.ini");

TGcConfig::TGcConfig()
    : iAutoJoin(EFalse), iLog(EFalse), iHasBridge(EFalse), iChannel(0)
{
}

static TInt HexVal(TUint8 c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static TBool ParseAddr(const TDesC8& aText, TBTDevAddr& aAddr)
{
    TUint8 b[6];
    TInt   n = 0, hi = -1, i;
    for (i = 0; i < aText.Length() && n < 6; i++)
    {
        TInt v = HexVal(aText[i]);
        if (v < 0)
        {
            continue; /* allow ':' and '-' separators */
        }
        if (hi < 0)
        {
            hi = v;
        }
        else
        {
            b[n++] = (TUint8)((hi << 4) | v);
            hi = -1;
        }
    }
    if (n != 6)
    {
        return EFalse;
    }
    aAddr = TBTDevAddr(TPtrC8(b, 6));
    return ETrue;
}

static TInt ToInt(const TDesC8& aText)
{
    TLex8 lex(aText);
    TInt  v = 0;
    lex.Val(v);
    return v;
}

static void Trim(TPtrC8& aText)
{
    TInt s = 0, e = aText.Length();
    while (s < e && (aText[s] == ' ' || aText[s] == '\t' || aText[s] == '"'))
    {
        s++;
    }
    while (e > s && (aText[e - 1] == ' ' || aText[e - 1] == '\t' || aText[e - 1] == '\r' || aText[e - 1] == '"'))
    {
        e--;
    }
    aText.Set(aText.Mid(s, e - s));
}

void TGcConfig::Load()
{
    RFs   fs;
    RFile file;
    if (fs.Connect() != KErrNone)
    {
        return;
    }
    iPath = KIniCard;
    if (file.Open(fs, iPath, EFileRead | EFileShareReadersOnly) != KErrNone)
    {
        iPath = KIniPhone;
        if (file.Open(fs, iPath, EFileRead | EFileShareReadersOnly) != KErrNone)
        {
            iPath.Zero();
            fs.Close();
            return;
        }
    }
    HBufC8* heap = HBufC8::New(2048); /* keep the game's stack small */
    if (!heap)
    {
        file.Close();
        fs.Close();
        return;
    }
    TPtr8 buf = heap->Des();
    file.Read(buf);
    file.Close();
    fs.Close();

    TPtrC8 rest(buf);
    while (rest.Length() > 0)
    {
        TInt   nl = rest.Locate('\n');
        TPtrC8 line = nl < 0 ? rest : rest.Left(nl);
        rest.Set(nl < 0 ? TPtrC8() : rest.Mid(nl + 1));
        Trim(line);
        if (line.Length() == 0 || line[0] == ';' || line[0] == '#' || line[0] == '[')
        {
            continue;
        }
        TInt eq = line.Locate('=');
        if (eq < 0)
        {
            continue;
        }
        TPtrC8 key = line.Left(eq);
        TPtrC8 val = line.Mid(eq + 1);
        Trim(key);
        Trim(val);
        if (key.CompareF(_L8("DeviceName")) == 0)
        {
            iDeviceName = val.Left(32);
        }
        else if (key.CompareF(_L8("AutoJoin")) == 0)
        {
            iAutoJoin = ToInt(val) != 0;
        }
        else if (key.CompareF(_L8("Log")) == 0)
        {
            iLog = ToInt(val) != 0;
        }
        else if (key.CompareF(_L8("Address")) == 0)
        {
            iHasBridge = ParseAddr(val, iBridge);
        }
        else if (key.CompareF(_L8("Channel")) == 0)
        {
            iChannel = ToInt(val);
        }
    }
    delete heap;
}

void TGcConfig::SaveBridgeAddress(const TBTDevAddr& aAddr)
{
    /* Rewrites the file, keeping every line except [Bridge] Address. */
    RFs   fs;
    RFile file;
    if (fs.Connect() != KErrNone)
    {
        return;
    }
    TFileName path = iPath.Length() ? iPath : TFileName(KIniCard);
    HBufC8*   inBuf = HBufC8::New(2048);
    HBufC8*   outBuf = HBufC8::New(2300);
    if (!inBuf || !outBuf)
    {
        delete inBuf;
        delete outBuf;
        fs.Close();
        return;
    }
    TPtr8 in = inBuf->Des();
    TPtr8 out = outBuf->Des();
    if (file.Open(fs, path, EFileRead) == KErrNone)
    {
        file.Read(in);
        file.Close();
    }
    TPtrC8      rest(in);
    TBool       inBridge = EFalse, written = EFalse;
    TBuf8<40>   addrLine;
    TInt        i;
    addrLine.Append(_L8("Address="));
    for (i = 0; i < 6; i++)
    {
        addrLine.AppendNumFixedWidth(aAddr[i], EHex, 2);
    }
    addrLine.Append(_L8("\r\n"));

    while (rest.Length() > 0)
    {
        TInt   nl = rest.Locate('\n');
        TPtrC8 line = nl < 0 ? rest : rest.Left(nl + 1);
        rest.Set(nl < 0 ? TPtrC8() : rest.Mid(nl + 1));
        TPtrC8 t = line;
        Trim(t);
        if (t.Length() && t[0] == '[')
        {
            if (inBridge && !written)
            {
                out.Append(addrLine);
                written = ETrue;
            }
            inBridge = t.CompareF(_L8("[Bridge]")) == 0;
        }
        else if (inBridge && t.Length() >= 7 && t.Left(7).CompareF(_L8("Address")) == 0)
        {
            continue; /* replaced below */
        }
        if (out.Length() + line.Length() < out.MaxLength() - 64)
        {
            out.Append(line);
        }
    }
    if (!written)
    {
        if (!inBridge)
        {
            out.Append(_L8("\r\n[Bridge]\r\n"));
        }
        out.Append(addrLine);
    }
    if (file.Replace(fs, path, EFileWrite) == KErrNone)
    {
        file.Write(out);
        file.Close();
    }
    fs.Close();
    delete inBuf;
    delete outBuf;
    iBridge = aAddr;
    iHasBridge = ETrue;
}
