/** @file PhantomLinkView.cpp
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "PhantomLinkView.h"
#include "GameNavigation.h"
#include "PhantomLink.hrh"
#include <aknappui.h>
#include <avkon.hrh>
#include <eikclb.h>
#include <eikclbd.h>

CPhantomLinkView *CPhantomLinkView::NewL(const TRect &aRect)
{
    CPhantomLinkView *self = new (ELeave) CPhantomLinkView;
    CleanupStack::PushL(self);
    self->ConstructL(aRect);
    CleanupStack::Pop();
    return self;
}

void CPhantomLinkView::ConstructL(const TRect &aRect)
{
    CreateWindowL();
    SetRect(aRect);
    ActivateL();
}

CPhantomLinkView::~CPhantomLinkView()
{
    delete iListBox;
}

void CPhantomLinkView::SetListL(CDesCArray *aItems, TInt aCurrent, TBool aGameNavigation)
{
    CleanupStack::PushL(aItems);
    CAknSingleStyleListBox *listBox = new (ELeave) CAknSingleStyleListBox;
    CleanupStack::PushL(listBox);
    listBox->SetContainerWindowL(*this);
    listBox->ConstructL(this, EAknListBoxSelectionList);
    CleanupStack::Pop();       /* listBox */
    CleanupStack::Pop(aItems); /* handed to the list box below */

    listBox->Model()->SetItemTextArray(aItems);
    listBox->Model()->SetOwnershipType(ELbmOwnsItemArray);
    TRAPD(err, listBox->CreateScrollBarFrameL(ETrue);
          listBox->ScrollBarFrame()->SetScrollBarVisibilityL(CEikScrollBarFrame::EOff, CEikScrollBarFrame::EAuto););
    if (err != KErrNone) {
        delete listBox;
        User::Leave(err);
    }
    listBox->SetRect(Rect());
    if (aCurrent >= 0 && aCurrent < aItems->MdcaCount()) {
        listBox->SetCurrentItemIndex(aCurrent);
    }
    listBox->ActivateL();

    delete iListBox;
    iListBox = listBox;
    iGameNavigation = aGameNavigation;
    DrawNow();
}

void CPhantomLinkView::ReplaceItemL(TInt aIndex, const TDesC &aText)
{
    if (!iListBox) {
        return;
    }
    CDesCArray *items = static_cast<CDesCArray *>(iListBox->Model()->ItemTextArray());
    if (aIndex < 0 || aIndex >= items->MdcaCount()) {
        return;
    }
    items->Delete(aIndex);
    items->InsertL(aIndex, aText);
    iListBox->DrawNow();
}

TInt CPhantomLinkView::CurrentIndex() const
{
    return iListBox ? iListBox->CurrentItemIndex() : -1;
}

TKeyResponse CPhantomLinkView::OfferKeyEventL(const TKeyEvent &aKeyEvent, TEventCode aType)
{
    if (!iListBox) {
        return EKeyWasNotConsumed;
    }
    if (aType == EEventKey && (aKeyEvent.iCode == EKeyDevice3 || aKeyEvent.iCode == EKeyEnter)) {
        iEikonEnv->EikAppUi()->HandleCommandL(EPlCmdSelect);
        return EKeyWasConsumed;
    }
    if (iGameNavigation && aType == EEventKey) {
        TGameNavigationDirection direction;
        switch (aKeyEvent.iCode) {
        case EKeyUpArrow:
            direction = EGameNavUp;
            break;
        case EKeyDownArrow:
            direction = EGameNavDown;
            break;
        case EKeyLeftArrow:
            direction = EGameNavLeft;
            break;
        case EKeyRightArrow:
            direction = EGameNavRight;
            break;
        default:
            return iListBox->OfferKeyEventL(aKeyEvent, aType);
        }
        TInt current = iListBox->CurrentItemIndex();
        TInt count = iListBox->Model()->ItemTextArray()->MdcaCount();
        TInt next = GameNavigationIndex(current, count, direction);
        if (next != current) {
            iListBox->SetCurrentItemIndex(next);
            iListBox->ScrollToMakeItemVisible(next);
            iListBox->UpdateScrollBarsL();
            iListBox->DrawNow();
        }
        return EKeyWasConsumed;
    }
    return iListBox->OfferKeyEventL(aKeyEvent, aType);
}

TInt CPhantomLinkView::CountComponentControls() const
{
    return iListBox ? 1 : 0;
}

CCoeControl *CPhantomLinkView::ComponentControl(TInt /*aIndex*/) const
{
    return iListBox;
}

void CPhantomLinkView::SizeChanged()
{
    if (iListBox) {
        iListBox->SetRect(Rect());
    }
}
