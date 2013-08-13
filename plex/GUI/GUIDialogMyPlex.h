//
//  GUIDialogMyPlexPin.h
//  Plex
//
//  Created by Tobias Hieta <tobias@plexapp.com> on 2012-12-14.
//  Copyright 2012 Plex Inc. All rights reserved.
//

#ifndef GUIDIALOGMYPLEXPIN_H
#define GUIDIALOGMYPLEXPIN_H

#include <dialogs/GUIDialogBoxBase.h>
#include "PlexTypes.h"
#include "Client/MyPlex/MyPlexManager.h"

class CGUIDialogMyPlex : public CGUIDialog
{
  public:
    CGUIDialogMyPlex() : CGUIDialog(WINDOW_DIALOG_MYPLEX_PIN, "DialogMyPlexLogin.xml") {};

    virtual bool OnMessage(CGUIMessage &message);
    static void ShowAndGetInput();

    void Setup();
    void ShowManualInput();
    void ShowPinInput();

    void ShowSuccess();
    void ShowFailure(int reason);

    void ToggleInput()
    {
      if (m_manual)
        ShowPinInput();
      else
        ShowManualInput();
    }

    void HandleMyPlexState(int state, int errorCode);

  private:
    bool m_manual;
};

#endif // GUIDIALOGMYPLEXPIN_H
