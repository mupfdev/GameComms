/** @file PhantomLinkView.h
 *
 *  Container control with one selection list; the application swaps the
 *  list content between the games and the bridge settings.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef PHANTOMLINKVIEW_H
#define PHANTOMLINKVIEW_H

#include <aknlists.h>
#include <badesca.h>
#include <coecntrl.h>

class CPhantomLinkView : public CCoeControl
{
  public:
    static CPhantomLinkView *NewL(const TRect &aRect);
    ~CPhantomLinkView();

    /** Replaces the list. Takes ownership of aItems (also if it leaves). */
    void SetListL(CDesCArray *aItems, TInt aCurrent);
    /** Replaces the text of one line (the line count stays the same). */
    void ReplaceItemL(TInt aIndex, const TDesC &aText);
    TInt CurrentIndex() const;

  public: // from CCoeControl
    TKeyResponse OfferKeyEventL(const TKeyEvent &aKeyEvent, TEventCode aType);
    TInt CountComponentControls() const;
    CCoeControl *ComponentControl(TInt aIndex) const;

  private:
    void ConstructL(const TRect &aRect);
    void SizeChanged();

    CAknSingleStyleListBox *iListBox;
};

#endif /* PHANTOMLINKVIEW_H */
