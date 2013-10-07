//
//  PlexHTTPRemoteHandler.cpp
//  Plex Home Theater
//
//  Created by Tobias Hieta on 2013-08-16.
//
//

#include "PlexHTTPRemoteHandler.h"
#include "network/WebServer.h"
#include <boost/algorithm/string.hpp>

#include "Client/PlexServerManager.h"
#include "FileSystem/PlexDirectory.h"
#include "ApplicationMessenger.h"
#include "Application.h"
#include "GUIUserMessages.h"
#include "guilib/GUIWindowManager.h"
#include "dialogs/GUIDialogKeyboardGeneric.h"

#include "utils/log.h"
#include "interfaces/Builtins.h"
#include "PlexRemoteSubscriberManager.h"
#include "Client/PlexTimelineManager.h"

#include <boost/asio/detail/socket_ops.hpp>
#include "PlexApplication.h"

#include "settings/GUISettings.h"

#include "PlayList.h"

#define LEGACY 1

////////////////////////////////////////////////////////////////////////////////////////
bool CPlexHTTPRemoteHandler::CheckHTTPRequest(const HTTPRequest &request)
{
  return boost::starts_with(request.url, "/player") || boost::starts_with(request.url, "/device");;
}

////////////////////////////////////////////////////////////////////////////////////////
int CPlexHTTPRemoteHandler::HandleHTTPRequest(const HTTPRequest &request)
{
  /* defaults */
  m_responseType = HTTPMemoryDownloadNoFreeCopy;
  m_responseCode = MHD_HTTP_OK;
  m_contentType = "text/xml";

  ArgMap argumentMap;
  ArgMap headerMap;
  CStdString path(request.url);
  
  CWebServer::GetRequestHeaderValues(request.connection, MHD_GET_ARGUMENT_KIND, argumentMap);
  CWebServer::GetRequestHeaderValues(request.connection, MHD_HEADER_KIND, headerMap);

  /* first see if we need to handle CORS requests - Access-Control-Allow-Origin needs to be
   * available for all requests, the other headers is only needed on a OPTIONS call */
  m_responseHeaderFields.insert(std::pair<std::string, std::string>("Access-Control-Allow-Origin", "*"));
  if (request.method == OPTIONS &&
      headerMap.find("Access-Control-Request-Method") != headerMap.end())
  {
    m_responseHeaderFields.insert(std::pair<std::string, std::string>("Content-Type", "text/plain"));
    m_responseHeaderFields.insert(std::pair<std::string, std::string>("Access-Control-Allow-Methods", "POST, GET, OPTIONS, DELETE, PUT, HEAD"));
    m_responseHeaderFields.insert(std::pair<std::string, std::string>("Access-Control-Max-Age", "1209600"));
    m_responseHeaderFields.insert(std::pair<std::string, std::string>("Connection", "close"));

    if (headerMap.find("Access-Control-Request-Headers") != headerMap.end())
      m_responseHeaderFields.insert(std::pair<std::string, std::string>("Access-Control-Allow-Headers", headerMap["Access-Control-Request-Headers"]));

    return MHD_YES;
  }

  m_data = "<Response code=\"200\" status=\"OK\" />";

  CLog::Log(LOGDEBUG, "CPlexHTTPRemoteHandler::HandleHTTPRequest handling %s", request.url.c_str());
  
  if (path.Equals("/player/playback/playMedia") ||
      /* LEGACY */ path.Equals("/player/application/playMedia"))
    playMedia(argumentMap);
  else if (path.Equals("/player/playback/stepForward") ||
           path.Equals("/player/playback/stepBack"))
    stepFunction(request.url, argumentMap);
  else if (path.Equals("/player/playback/skipNext"))
    skipNext(argumentMap);
  else if (path.Equals("/player/playback/skipPrevious"))
    skipPrevious(argumentMap);
  else if (path.Equals("/player/playback/stop"))
    stop(argumentMap);
  else if (path.Equals("/player/playback/seekTo"))
    seekTo(argumentMap);
  else if (path.Equals("/player/playback/skipTo"))
    skipTo(argumentMap);
  else if (path.Equals("/player/playback/setParameters"))
    set(argumentMap);
  else if (boost::starts_with(path, "/player/navigation"))
    navigation(path, argumentMap);
  else if (path.Equals("/player/timeline/subscribe"))
    subscribe(request, argumentMap);
  else if (path.Equals("/player/timeline/unsubscribe"))
    unsubscribe(request, argumentMap);
  else if (path.Equals("/player/timeline/poll"))
    poll(request, argumentMap);
  else if (path.Equals("/player/setStreams"))
    setStreams(argumentMap);
  else if (boost::starts_with(path, "/device"))
    device();
  else if (path.Equals("/player/playback/togglePlayPause"))
    pausePlay(argumentMap);

#ifdef LEGACY
  else if (path.Equals("/player/application/sendString"))
    sendString(argumentMap);
  else if (path.Equals("/player/application/sendVirtualKey") ||
           path.Equals("/player/application/sendKey"))
    sendVKey(argumentMap);
  else if (path.Equals("/player/playback/bigStepForward") ||
           path.Equals("/player/playback/bigStepBack"))
    stepFunction(request.url, argumentMap);
  else if (path.Equals("/player/playback/pause"))
    pausePlay(argumentMap);
  else if (path.Equals("/player/playback/play"))
    pausePlay(argumentMap);
#endif

  else
  {
    m_responseCode = MHD_HTTP_INTERNAL_SERVER_ERROR;
    m_data.Format("<Response code=\"500\" status=\"not implemented\" />");
  }

  m_responseHeaderFields.insert(std::pair<std::string, std::string>("Content-Type", m_contentType));
  
  return MHD_YES;
}

////////////////////////////////////////////////////////////////////////////////////////
void* CPlexHTTPRemoteHandler::GetHTTPResponseData() const
{
  return (void*)m_data.c_str();
}

////////////////////////////////////////////////////////////////////////////////////////
size_t CPlexHTTPRemoteHandler::GetHTTPResonseDataLength() const
{
  return m_data.size();
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexHTTPRemoteHandler::playMedia(const ArgMap &arguments)
{
  CPlexServerPtr server;
  std::string containerPath;
  
  /* Protocol v2 allows for sending machineIndentifier. */
  if (arguments.find("machineIdentifier") != arguments.end())
  {
    std::string uuid = arguments.find("machineIdentifier")->second;
    server = g_plexApplication.serverManager->FindByUUID(uuid);
    if (arguments.find("containerKey") != arguments.end())
      containerPath = arguments.find("containerKey")->second;
  }
  else if (arguments.find("path") != arguments.end())
  {
    CURL serverURL(arguments.find("path")->second);
    if (!PlexUtils::IsValidIP(serverURL.GetHostName()))
    {
      CLog::Log(LOGWARNING, "CPlexHTTPRemoteHandler::playMedia got a path, but it's not a valid IP.");
      return;
    }
    /* look up via host and port, this is not super reliable, but it will probably work
     * most of the time */
    server = g_plexApplication.serverManager->FindByHostAndPort(serverURL.GetHostName(), serverURL.GetPort());

    if (!server)
    {
      server = CPlexServerPtr(new CPlexServer());
      CStdString token("");

      if (serverURL.HasOption("X-Plex-Token"))
        token = serverURL.GetOption("X-Plex-Token");
      server->AddConnection(CPlexConnectionPtr(new CPlexConnection(CPlexConnection::CONNECTION_DISCOVERED, serverURL.GetHostName(), serverURL.GetPort(), token)));
    }

    containerPath = serverURL.GetFileName();
  }
  else
  {
    CLog::Log(LOGWARNING, "CPlexHTTPRemoteHandler::playMedia Didn't find machineIdentifier nor path in the request. Can't know what to play?");
    return;
  }
  
  if (!server || containerPath.empty())
  {
    CLog::Log(LOGWARNING, "CPlexHTTPRemoteHandler::playMedia can't find server to play from");
    return;
  }
  
  CURL itemURL = server->BuildPlexURL(containerPath);
  
  std::string key;
  
  if (arguments.find("key") != arguments.end())
  {
    key = arguments.find("key")->second;
  }
  else
  {
    CLog::Log(LOGWARNING, "CPlexHTTPRemoteHandler::playMedia no key attribute, can't progress...");
    return;
  }

  if (arguments.find("protocol") != arguments.end())
  {
    if (arguments.find("protocol")->second == "https")
      itemURL.SetProtocolOption("ssl", "1");
  }
  
  /* fetch the container */
  CFileItemList list;
  XFILE::CPlexDirectory dir;
  if (!dir.GetDirectory(itemURL.Get(), list))
  {
    CLog::Log(LOGWARNING, "CPlexHTTPRemoteHandler::playMedia can't fetch container %s", itemURL.Get().c_str());
    return;
  }
  
  CFileItemPtr item;
  int idx;
  for (int i = 0; i < list.Size(); i ++)
  {
    CFileItemPtr it = list.Get(i);
    if (it->HasProperty("unprocessed_key") &&
        it->GetProperty("unprocessed_key") == key)
    {
      item = it;
      idx = i;
      break;
    }
  }
  
  if (!item)
  {
    CLog::Log(LOGWARNING, "CPlexHTTPRemoteHandler::playMedia couldn't find %s in %s", key.c_str(), itemURL.Get().c_str());
    return;
  }

  if (arguments.find("viewOffset") != arguments.end())
  {
    item->SetProperty("viewOffset", arguments.find("viewOffset")->second);
    item->m_lStartOffset = STARTOFFSET_RESUME;
  }
  
  
  if (item->GetPlexDirectoryType() == PLEX_DIR_TYPE_TRACK)
  {
    if (g_application.IsPlaying())
      CApplicationMessenger::Get().MediaStop();
    
    if (g_playlistPlayer.IsShuffled(PLAYLIST_MUSIC))
      CApplicationMessenger::Get().PlayListPlayerShuffle(PLAYLIST_MUSIC, false);
    
    CApplicationMessenger::Get().PlayListPlayerClear(PLAYLIST_MUSIC);
    CApplicationMessenger::Get().PlayListPlayerAdd(PLAYLIST_MUSIC, list);
    CApplicationMessenger::Get().MediaPlay(PLAYLIST_MUSIC, idx);
  }
  else if (item->GetPlexDirectoryType() == PLEX_DIR_TYPE_PHOTO)
  {
    /* if we are playing music, we don't need to stop */
    if (g_application.IsPlayingVideo())
      CApplicationMessenger::Get().MediaStop();
    
    CApplicationMessenger::Get().PictureSlideShow(itemURL.Get(), false, list.Get(idx)->GetPath());
  }
  else
  {
    if (g_application.IsPlaying())
      CApplicationMessenger::Get().MediaStop();
    
    CApplicationMessenger::Get().PlayFile(*item);
  }
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexHTTPRemoteHandler::stepFunction(const CStdString &url, const ArgMap &arguments)
{
  
  if (!g_application.IsPlaying())
    return;
  
  if (url.Equals("/player/playback/bigStepForward"))
    CApplicationMessenger::Get().ExecBuiltIn("playercontrol(bigskipforward)");
  else if (url.Equals("/player/playback/bigStepBack"))
    CApplicationMessenger::Get().ExecBuiltIn("playercontrol(bigskipbackward)");
  else if (url.Equals("/player/playback/stepForward"))
    CApplicationMessenger::Get().ExecBuiltIn("playercontrol(smallskipforward)");
  else if (url.Equals("/player/playback/stepBack"))
    CApplicationMessenger::Get().ExecBuiltIn("playercontrol(smallskipbackward)");
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexHTTPRemoteHandler::skipNext(const ArgMap &arguments)
{
  if (g_application.IsPlaying())
    CApplicationMessenger::Get().ExecBuiltIn("playercontrol(next)");
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexHTTPRemoteHandler::skipPrevious(const ArgMap &arguments)
{
  if (g_application.IsPlaying())
    CApplicationMessenger::Get().ExecBuiltIn("playercontrol(previous)");
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexHTTPRemoteHandler::pausePlay(const ArgMap &arguments)
{
  CApplicationMessenger::Get().MediaPause();
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexHTTPRemoteHandler::stop(const ArgMap &arguments)
{
  CApplicationMessenger::Get().MediaStop();
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexHTTPRemoteHandler::seekTo(const ArgMap &arguments)
{
  int64_t seekTo;
  
  if (arguments.find("offset") != arguments.end())
  {
    try {
      seekTo = boost::lexical_cast<int64_t>(arguments.find("offset")->second);
    } catch (...) {
      CLog::Log(LOGWARNING, "CPlexHTTPRemoteHandler::seekTo failed to convert offset into a int64_t");
      return;
    }
  }
  else
    return;
  
  if (g_application.IsPlaying() && g_application.m_pPlayer)
    g_application.m_pPlayer->SeekTime(seekTo);
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexHTTPRemoteHandler::navigation(const CStdString &url, const ArgMap &arguments)
{
  int action = ACTION_NONE;
  
  CStdString navigation = url.Mid(19, url.length() - 19);
  
  if (navigation.Equals("moveRight"))
    action = ACTION_MOVE_RIGHT;
  else if (navigation.Equals("moveLeft"))
    action = ACTION_MOVE_LEFT;
  else if (navigation.Equals("moveDown"))
    action = ACTION_MOVE_DOWN;
  else if (navigation.Equals("moveUp"))
    action = ACTION_MOVE_UP;
  else if (navigation.Equals("select"))
    action = ACTION_SELECT_ITEM;
  else if (navigation.Equals("back"))
    action = ACTION_NAV_BACK;
  else if (navigation.Equals("home"))
    action = ACTION_FIRST_PAGE;
  else if (navigation.Equals("music"))
  {
    if (g_application.IsPlayingAudio() && g_windowManager.GetActiveWindow() != WINDOW_VISUALISATION)
      action = ACTION_SHOW_GUI;
  }

#ifdef LEGACY
  else if (navigation.Equals("contextMenu"))
    action = ACTION_CONTEXT_MENU;
  else if (navigation.Equals("toggleOSD"))
    action = ACTION_SHOW_OSD;
  else if (navigation.Equals("pageUp"))
    action = ACTION_PAGE_UP;
  else if (navigation.Equals("pageDown"))
    action = ACTION_PAGE_DOWN;
  else if (navigation.Equals("nextLetter"))
    action = ACTION_NEXT_LETTER;
  else if (navigation.Equals("previousLetter"))
    action = ACTION_PREV_LETTER;
#endif
  
  if (action != ACTION_NONE)
  {
    g_application.WakeUpScreenSaverAndDPMS();
    CApplicationMessenger::Get().SendAction(CAction(action), g_windowManager.GetFocusedWindow(), false);
  }
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexHTTPRemoteHandler::set(const ArgMap &arguments)
{
  if (arguments.find("volume") != arguments.end())
  {
    ArgMap vmap;
    vmap["level"] = arguments.find("volume")->second;
    setVolume(vmap);
  }
  
  if (arguments.find("shuffle") != arguments.end())
  {
    int shuffle = boost::lexical_cast<int>(arguments.find("shuffle")->second);
    int playlistType = g_playlistPlayer.GetCurrentPlaylist();
    CApplicationMessenger::Get().PlayListPlayerShuffle(playlistType, shuffle == 1);
  }
  
  if (arguments.find("repeat") != arguments.end())
  {
    int repeat = boost::lexical_cast<int>(arguments.find("repeat")->second);
    int playlistType = g_playlistPlayer.GetCurrentPlaylist();
    
    int xbmcRepeat = PLAYLIST::REPEAT_NONE;
    
    if (repeat==1)
      xbmcRepeat = PLAYLIST::REPEAT_ONE;
    else if (repeat==2)
      xbmcRepeat = PLAYLIST::REPEAT_ALL;
    
    CApplicationMessenger::Get().PlayListPlayerRepeat(playlistType, xbmcRepeat);
  }
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexHTTPRemoteHandler::setVolume(const ArgMap &arguments)
{
  int level;
  
  if (arguments.find("level") == arguments.end())
    return;
  
  level = boost::lexical_cast<int>(arguments.find("level")->second);
  
  int oldVolume = g_application.GetVolume();
  g_application.SetVolume((float)level, true);
  CApplicationMessenger::Get().ShowVolumeBar(oldVolume < level);
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexHTTPRemoteHandler::sendString(const ArgMap &arguments)
{
  if (arguments.find("text") == arguments.end())
    return;
  
  /* this is a bit kludgy, but I couldn't figure out how to get the current text any other way
   * TODO: how does this work with a numeric input? */
  CGUIDialogKeyboardGeneric *keyb = dynamic_cast<CGUIDialogKeyboardGeneric*>(g_windowManager.GetWindow(g_windowManager.GetFocusedWindow()));
  
  if (!keyb)
    return;

  g_application.WakeUpScreenSaverAndDPMS();
  
  CStdString newString = keyb->GetText() + arguments.find("text")->second.c_str();
  
  /* instead of calling keyb->SetText() we want to send this as a message
   * to avoid any thread locking and contention */
  CGUIMessage msg(GUI_MSG_SET_TEXT, 0, 0);
  msg.SetLabel(newString);
  msg.SetParam1(0);
  
  CApplicationMessenger::Get().SendGUIMessage(msg, g_windowManager.GetFocusedWindow());
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexHTTPRemoteHandler::sendVKey(const ArgMap &arguments)
{
  if (arguments.find("code") == arguments.end())
    return;
  
  int code = boost::lexical_cast<int>(arguments.find("code")->second);
  CLog::Log(LOGDEBUG, "CPlexHTTPRemoteHandler::sendVKey got code %d", code);
  int action;
  
  switch (code)
  {
    case 8:
      action = ACTION_BACKSPACE;
      break;
    case 13:
      action = ACTION_SELECT_ITEM;
      break;
    default:
      action = ACTION_NONE;
  }
  
  if (action != ACTION_NONE)
  {
    g_application.WakeUpScreenSaverAndDPMS();
    CApplicationMessenger::Get().SendAction(CAction(action), g_windowManager.GetFocusedWindow(), false);
  }
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexHTTPRemoteHandler::subscribe(const HTTPRequest &request, const ArgMap &arguments)
{
  CPlexRemoteSubscriberPtr sub = getSubFromRequest(request, arguments);
  if (sub)
  {
    g_plexApplication.remoteSubscriberManager->addSubscriber(sub);
    g_plexApplication.timelineManager->SendTimelineToSubscriber(sub);
    m_data = g_plexApplication.timelineManager->GetCurrentTimeLinesXML(sub->getCommandID());
  }
  else
  {
    m_data = "<Response code=\"500\" status=\"Failed to specify all data to subscribe call, try again\" />";
  }
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexHTTPRemoteHandler::unsubscribe(const HTTPRequest &request, const ArgMap &arguments)
{
  g_plexApplication.remoteSubscriberManager->removeSubscriber(getSubFromRequest(request, arguments));
}

////////////////////////////////////////////////////////////////////////////////////////
CPlexRemoteSubscriberPtr CPlexHTTPRemoteHandler::getSubFromRequest(const HTTPRequest &request, const ArgMap &arguments)
{
  std::string uuid, ipaddress;
  
  uuid = CWebServer::GetRequestHeaderValue(request.connection, MHD_HEADER_KIND, "X-Plex-Client-Identifier");
  if (uuid.empty())
  {
    CLog::Log(LOGWARNING, "CPlexHTTPRemoteHandler::subscribe subscriber didn't set X-Plex-Client-Identifier");
    return CPlexRemoteSubscriberPtr();
  }
  
  char ipstr[INET_ADDRSTRLEN];
#if MHD_VERSION > 0x00090600
  sockaddr *so = MHD_get_connection_info(request.connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS)->client_addr;
  strncpy(ipstr, so->sa_data, INET_ADDRSTRLEN);
#else
  struct sockaddr_in *so = MHD_get_connection_info(request.connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS)->client_addr;

  boost::system::error_code ec;
  boost::asio::detail::socket_ops::inet_ntop(AF_INET, &(so->sin_addr), ipstr, INET_ADDRSTRLEN, 0, ec);
#endif
  if (!so)
    return CPlexRemoteSubscriberPtr();
  
  int port = 32400;
  int commandID = -1;
  CStdString protocol = "http";

  if (arguments.find("port") != arguments.end())
    port = boost::lexical_cast<int>(arguments.find("port")->second);

  if (arguments.find("commandID") != arguments.end())
    commandID = boost::lexical_cast<int>(arguments.find("commandID")->second);

  if (arguments.find("protocol") != arguments.end())
    protocol = arguments.find("protocol")->second;
  
  CPlexRemoteSubscriberPtr sub = CPlexRemoteSubscriber::NewSubscriber(uuid, ipstr, port, commandID);
  
  return sub;
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexHTTPRemoteHandler::setStreams(const ArgMap &arguments)
{
  if (!g_application.IsPlayingVideo())
    return;
  
  if (arguments.find("audioStreamID") != arguments.end())
  {
    int audioStreamID = boost::lexical_cast<int>(arguments.find("audioStreamID")->second);
    g_application.m_pPlayer->SetAudioStreamPlexID(audioStreamID);
  }
  
  if (arguments.find("subtitleStreamID") != arguments.end())
  {
    int subStreamID = boost::lexical_cast<int>(arguments.find("subtitleStreamID")->second);
    if (subStreamID == -1 && g_application.m_pPlayer->GetSubtitleVisible())
      g_application.m_pPlayer->SetSubtitleVisible(false);
    else
    {
      g_application.m_pPlayer->SetSubtitleStreamPlexID(subStreamID);
      g_application.m_pPlayer->SetSubtitleVisible(true);
    }
  }
}

void CPlexHTTPRemoteHandler::poll(const HTTPRequest &request, const ArgMap &arguments)
{
  bool wait = false;
  if (arguments.find("wait") != arguments.end())
  {
    if (arguments.find("wait")->second == "1" || arguments.find("wait")->second == "true")
      wait = true;
  }

  int commandID = -1;
  if (arguments.find("commandID") != arguments.end())
    commandID = boost::lexical_cast<int>(arguments.find("commandID")->second);

  CStdString lines;
  if (wait)
  {
    CLog::Log(LOGDEBUG, "CPlexHTTPRemoteHandler::poll waiting for timeline event.");
    lines = g_plexApplication.timelineManager->WaitForTimeline(commandID);
  }
  else
  {
    lines = g_plexApplication.timelineManager->GetCurrentTimeLinesXML(commandID);
  }

  m_data = lines;
}

void CPlexHTTPRemoteHandler::skipTo(const ArgMap &arguments)
{
  PLAYLIST::CPlayList playlist;
  int playlistType = g_playlistPlayer.GetCurrentPlaylist();

  if (arguments.find("type") != arguments.end())
  {
    std::string type = arguments.find("type")->second;
    if (type == "music")
      playlistType = PLAYLIST_MUSIC;
    else if (type == "video")
      playlistType = PLAYLIST_VIDEO;
  }

  playlist = g_playlistPlayer.GetPlaylist(playlistType);

  if (arguments.find("key") == arguments.end())
  {
    CLog::Log(LOGWARNING, "CPlexHTTPRemoteHandler::skipTo missing 'key' argument.");
    return;
  }

  std::string key = arguments.find("key")->second;

  int idx = -1;
  for (int i = 0; i < playlist.size(); i++)
  {
    CFileItemPtr item = playlist[i];
    if (item->GetProperty("unprocessed_key").asString() == key)
    {
      idx = i;
      break;
    }
  }

  if (idx != -1)
    CApplicationMessenger::Get().MediaPlay(playlistType, idx);
}


void CPlexHTTPRemoteHandler::device()
{
  m_data.Format("<MediaContainer machineIdentifier=\"%s\" platform=\"%s\" platformVersion=\"%s\" friendlyName=\"%s\">",
                g_guiSettings.GetString("system.uuid"),
                PlexUtils::GetMachinePlatform(),
                PlexUtils::GetMachinePlatformVersion(),
                g_guiSettings.GetString("services.devicename"));
  m_data += "\n  <Capability type=\"player\" protocolVersion=\"1\" protocolCapabilites=\"navigation\" />\n</MediaContainer>";
}
