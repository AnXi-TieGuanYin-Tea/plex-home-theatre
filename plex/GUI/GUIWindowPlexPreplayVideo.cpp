
#include "GUIDialogPlexAudioSubtitlePicker.h"

#include "GUIWindowPlexPreplayVideo.h"
#include "plex/PlexTypes.h"
#include "guilib/GUIWindow.h"
#include "filesystem/Directory.h"

#include "FileItem.h"
#include "guilib/GUILabelControl.h"
#include "ApplicationMessenger.h"

#define AUDIO_BUTTON_ID 19101
#define SUBTITLE_BUTTON_ID 19102
#define AUDIO_LABEL_ID 19111
#define SUBTITLE_LABEL_ID 19112

using namespace XFILE;

CGUIWindowPlexPreplayVideo::CGUIWindowPlexPreplayVideo(void)
 : CGUIMediaWindow(WINDOW_PLEX_PREPLAY_VIDEO, "PlexPreplayVideo.xml")
{
}

CGUIWindowPlexPreplayVideo::~CGUIWindowPlexPreplayVideo()
{
}

void
CGUIWindowPlexPreplayVideo::UpdateAudioSubButtons()
{
  CFileItemPtr fileItem = m_vecItems->Get(0);
  if (fileItem && fileItem->m_mediaItems.size() > 0)
  {
    CGUILabelControl *audioLabel = (CGUILabelControl*)GetControl(AUDIO_LABEL_ID);
    if (audioLabel)
      audioLabel->SetLabel(CGUIDialogPlexAudioSubtitlePicker::GetPresentationString(fileItem->m_mediaItems[0]));
    
    CGUILabelControl *subtitleLabel = (CGUILabelControl*)GetControl(SUBTITLE_LABEL_ID);
    if (subtitleLabel)
      subtitleLabel->SetLabel(CGUIDialogPlexAudioSubtitlePicker::GetPresentationString(fileItem->m_mediaItems[0], false));
  }  
}

bool
CGUIWindowPlexPreplayVideo::OnMessage(CGUIMessage &message)
{
  
  if (message.GetMessage() == GUI_MSG_CLICKED)
  {
    if (message.GetSenderId() == AUDIO_BUTTON_ID || message.GetSenderId() == SUBTITLE_BUTTON_ID)
    {
      CFileItemPtr fileItem = m_vecItems->Get(0);
      if (fileItem && fileItem->m_mediaItems.size() > 0)
      {
        CGUIDialogPlexAudioSubtitlePicker::ShowAndGetInput(fileItem->m_mediaItems[0], message.GetSenderId() == AUDIO_BUTTON_ID);
        UpdateAudioSubButtons();
        return true;
      }
    }
  }

  bool ret = CGUIMediaWindow::OnMessage(message);

  if (message.GetMessage() == GUI_MSG_WINDOW_INIT)
  {
    if (m_vecItems->GetContent() == "movies")
      m_vecItems->SetContent("movie");
    if (m_vecItems->GetContent() == "episodes")
      m_vecItems->SetContent("episode");
    
    UpdateAudioSubButtons();
  }
  
  
  return ret;
}

bool
CGUIWindowPlexPreplayVideo::OnAction(const CAction &action)
{
  if (action.GetID() == ACTION_PLAYER_PLAY)
  {
    CApplicationMessenger::Get().PlayFile(*m_vecItems->Get(0));
    return true;
  }
  
  return CGUIMediaWindow::OnAction(action);
}

CFileItemPtr
CGUIWindowPlexPreplayVideo::GetCurrentListItem(int offset)
{
  if (offset == 0 && m_vecItems->Size() > 0)
    return m_vecItems->Get(0);
  
  return CFileItemPtr();
}
