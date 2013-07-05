/*
 *  PlexApplication.cpp
 *  XBMC
 *
 *  Created by Jamie Kirkpatrick on 20/01/2011.
 *  Copyright 2011 Plex Inc. All rights reserved.
 *
 */

#include "Client/PlexNetworkServiceBrowser.h"
#include "PlexApplication.h"
#include "BackgroundMusicPlayer.h"
#include "GUIUserMessages.h"
#include "MediaSource.h"
#include "plex/Helper/PlexHTHelper.h"
#include "Client/MyPlex/MyPlexManager.h"
#include "AdvancedSettings.h"

////////////////////////////////////////////////////////////////////////////////
void
PlexApplication::Start()
{

  m_autoUpdater = new CPlexAutoUpdate("http://plexapp.com/appcast/plexht/appcast.xml");

  if (g_advancedSettings.m_bEnableGDM)
    m_serviceListener = CPlexServiceListenerPtr(new CPlexServiceListener);
  
  // Add the manual server if it exists and is enabled.
  if (g_guiSettings.GetBool("plexmediaserver.manualaddress"))
  {
    string address = g_guiSettings.GetString("plexmediaserver.address");
    if (PlexUtils::IsValidIP(address))
    {
      PlexServerList list;
      CPlexServerPtr server = CPlexServerPtr(new CPlexServer("", address, 32400));
      list.push_back(server);
      g_plexServerManager.UpdateFromConnectionType(list, CPlexConnection::CONNECTION_MANUAL);
    }
  }

  g_myplexManager.Create();
}

////////////////////////////////////////////////////////////////////////////////
PlexApplication::~PlexApplication()
{
  delete m_autoUpdater;
}

////////////////////////////////////////////////////////////////////////////////
bool PlexApplication::OnMessage(CGUIMessage& message)
{
  switch (message.GetMessage())
  {
    case GUI_MSG_APP_ACTIVATED:
    case GUI_MSG_APP_DEACTIVATED:
    {
      CLog::Log(LOGDEBUG,"Plex Application: Handling message %d", message.GetMessage());
      return true;
    }
    case GUI_MSG_BG_MUSIC_SETTINGS_UPDATED:
    {
      return true;
    }
    case GUI_MSG_BG_MUSIC_THEME_UPDATED:
    {
      g_backgroundMusicPlayer.SetTheme(message.GetStringParam());
      return true;
    }
  }
  
  return false;
}

#ifdef TARGET_DARWIN_OSX
// Hack
class CRemoteRestartThread : public CThread
{
  public:
    CRemoteRestartThread() : CThread("RemoteRestart") {}
    void Process()
    {
      // This blocks until the helper is restarted
      PlexHTHelper::GetInstance().Restart();
    }
};
#endif

////////////////////////////////////////////////////////////////////////////////
void PlexApplication::OnWakeUp()
{
  /* Scan servers */
  m_serviceListener->ScanNow();
  //MyPlexManager::Get().scanAsync();

#ifdef TARGET_DARWIN_OSX
  CRemoteRestartThread* hack = new CRemoteRestartThread;
  hack->Create(true);
#endif

}
