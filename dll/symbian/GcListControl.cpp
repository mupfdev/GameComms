/** @file GcListControl.cpp
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include <coemain.h>
#include <coeaui.h>
#include <eikenv.h>
#include <gdi.h>
#include "GcListControl.h"
#include "GcEngine.h"

_LIT(KGcSearching, "Searching...");
_LIT(KGcHint, "5/OK: select   C/right: cancel");

CGcListControl* CGcListControl::NewL(MGcListObserver& aObserver, const TDesC& aTitle, const TRect& aRect)
{
    CGcListControl* self = new (ELeave) CGcListControl(aObserver);
    CleanupStack::PushL(self);
    self->ConstructL(aTitle, aRect);
    CleanupStack::Pop();
    return self;
}

CGcListControl::CGcListControl(MGcListObserver& aObserver) : iObserver(aObserver)
{
}

void CGcListControl::ConstructL(const TDesC& aTitle, const TRect& aRect)
{
    iItems = new (ELeave) CDesCArrayFlat(4);
    iTitle = aTitle.Left(iTitle.MaxLength());
    CreateWindowL();
    SetRect(aRect);
    ActivateL();
    static_cast<CCoeAppUi*>(iCoeEnv->AppUi())->AddToStackL(this, ECoeStackPriorityDialog);
    iOnStack = ETrue;
    DrawNow();
}

CGcListControl::~CGcListControl()
{
    if (iOnStack)
    {
        static_cast<CCoeAppUi*>(iCoeEnv->AppUi())->RemoveFromStack(this);
    }
    delete iItems;
}

void CGcListControl::Dismiss()
{
    if (iOnStack)
    {
        static_cast<CCoeAppUi*>(iCoeEnv->AppUi())->RemoveFromStack(this);
        iOnStack = EFalse;
    }
    MakeVisible(EFalse);
}

void CGcListControl::SetItemsL(const MDesCArray& aItems)
{
    TInt i;
    iItems->Reset();
    for (i = 0; i < aItems.MdcaCount(); i++)
    {
        iItems->AppendL(aItems.MdcaPoint(i));
    }
    if (iCurrent >= iItems->Count())
    {
        iCurrent = iItems->Count() ? iItems->Count() - 1 : 0;
    }
    DrawNow();
}

TKeyResponse CGcListControl::OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType)
{
    if (aType != EEventKey)
    {
        return EKeyWasConsumed; /* modal */
    }
    switch (aKeyEvent.iCode)
    {
        case EKeyUpArrow:
            if (iCurrent > 0)
            {
                iCurrent--;
                DrawNow();
            }
            break;
        case EKeyDownArrow:
            if (iCurrent + 1 < iItems->Count())
            {
                iCurrent++;
                DrawNow();
            }
            break;
        case EKeyDevice3:   /* joystick press */
        case EKeyDevice0:   /* left soft key  */
        case EKeyEnter:
        case '5':
            if (iItems->Count() > 0)
            {
                iObserver.ListItemSelected(iCurrent); /* may delete us */
            }
            return EKeyWasConsumed;
        case EKeyDevice1:   /* right soft key */
        case EKeyBackspace: /* C key          */
        case EKeyEscape:
            iObserver.ListCancelled(); /* may delete us */
            return EKeyWasConsumed;
        default:
            break;
    }
    return EKeyWasConsumed;
}

void CGcListControl::Draw(const TRect& /*aRect*/) const
{
    CWindowGc&  gc = SystemGc();
    const CFont* font = iCoeEnv->NormalFont();
    TRect        r = Rect();
    TInt         lh = font->HeightInPixels() + 6;
    TInt         y = r.iTl.iY + lh;
    TInt         i;

    gc.SetPenStyle(CGraphicsContext::ENullPen);
    gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
    gc.SetBrushColor(KRgbWhite);
    gc.DrawRect(r);
    gc.UseFont(font);

    gc.SetBrushColor(KRgbDarkBlue);
    gc.DrawRect(TRect(r.iTl.iX, r.iTl.iY, r.iBr.iX, r.iTl.iY + lh));
    gc.SetPenStyle(CGraphicsContext::ESolidPen);
    gc.SetPenColor(KRgbWhite);
    gc.DrawText(iTitle, TPoint(r.iTl.iX + 4, r.iTl.iY + lh - 4));

    y = r.iTl.iY + 2 * lh;
    if (iItems->Count() == 0)
    {
        gc.SetPenColor(KRgbBlack);
        gc.DrawText(KGcSearching, TPoint(r.iTl.iX + 6, y));
    }
    for (i = 0; i < iItems->Count(); i++)
    {
        TRect line(r.iTl.iX + 2, y - lh + 4, r.iBr.iX - 2, y + 2);
        if (i == iCurrent)
        {
            gc.SetPenStyle(CGraphicsContext::ENullPen);
            gc.SetBrushColor(KRgbBlue);
            gc.DrawRect(line);
            gc.SetPenStyle(CGraphicsContext::ESolidPen);
            gc.SetPenColor(KRgbWhite);
        }
        else
        {
            gc.SetPenColor(KRgbBlack);
        }
        gc.DrawText((*iItems)[i], TPoint(r.iTl.iX + 6, y));
        y += lh;
    }
    gc.SetPenColor(KRgbGray);
    gc.DrawText(KGcHint, TPoint(r.iTl.iX + 4, r.iBr.iY - 4));
    gc.DiscardFont();
}
