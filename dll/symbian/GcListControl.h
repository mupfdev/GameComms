/** @file GcListControl.h
 *
 *  Self-contained full screen list (no resource file needed) used for
 *  host selection and for confirming joining players.  It replaces the
 *  Bluetooth device selection dialog / device list of the original.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef GCLISTCONTROL_H
#define GCLISTCONTROL_H

#include <badesca.h>
#include <coecntrl.h>

class MGcListObserver;

class CGcListControl : public CCoeControl
{
  public:
    static CGcListControl *NewL(MGcListObserver &aObserver, const TDesC &aTitle, const TRect &aRect);
    ~CGcListControl();

    void SetItemsL(const MDesCArray &aItems);
    /** hide and stop taking keys (deleted later by the owner) */
    void Dismiss();
    TKeyResponse OfferKeyEventL(const TKeyEvent &aKeyEvent, TEventCode aType);

  private:
    CGcListControl(MGcListObserver &aObserver);
    void ConstructL(const TDesC &aTitle, const TRect &aRect);
    void Draw(const TRect &aRect) const;

    MGcListObserver &iObserver;
    CDesCArrayFlat *iItems;
    TBuf<40> iTitle;
    TInt iCurrent;
    TBool iOnStack;
};

#endif /* GCLISTCONTROL_H */
