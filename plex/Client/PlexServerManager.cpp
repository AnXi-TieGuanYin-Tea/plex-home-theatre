#include "PlexServerManager.h"

#include <boost/foreach.hpp>
#include <vector>
#include "utils/log.h"
#include "GUIMessage.h"
#include "GUIWindowManager.h"
#include "plex/PlexTypes.h"
#include "Client/PlexConnection.h"
#include "PlexServerDataLoader.h"
#include "File.h"

#include "Stopwatch.h"

using namespace std;

void
CPlexServerReachabilityThread::Process()
{
  if (m_force == true ||
      !m_server->GetActiveConnection())
  {
    if (m_server->UpdateReachability())
      g_plexServerManager.ServerReachabilityDone(m_server, true);
    else
      g_plexServerManager.ServerReachabilityDone(m_server, false);
  }

  
}

CPlexServerManager::CPlexServerManager()
{
  _myPlexServer = CPlexServerPtr(new CPlexServer("myplex", "myPlex", true));
  _myPlexServer->AddConnection(CPlexConnectionPtr(new CMyPlexConnection(CPlexConnection::CONNECTION_MYPLEX, "my.plexapp.com", 443)));

  _localServer = CPlexServerPtr(new CPlexServer("local", PlexUtils::GetHostName(), true));
  _localServer->AddConnection(CPlexConnectionPtr(new CPlexConnection(CPlexConnection::CONNECTION_MANUAL, "127.0.0.1", 32400)));
}

CPlexServerPtr
CPlexServerManager::FindByHostAndPort(const CStdString &host, int port)
{
  CSingleLock lk(m_serverManagerLock);

  BOOST_FOREACH(PlexServerPair p, m_serverMap)
  {
    vector<CPlexConnectionPtr> connections;
    p.second->GetConnections(connections);
    BOOST_FOREACH(CPlexConnectionPtr conn, connections)
    {
      if (conn->GetAddress().GetHostName().Equals(host) &&
          conn->GetAddress().GetPort() == port)
        return p.second;
    }
  }
  return CPlexServerPtr();
}

CPlexServerPtr
CPlexServerManager::FindByUUID(const CStdString &uuid)
{
  CSingleLock lk(m_serverManagerLock);

  if (uuid.Equals("myplex"))
    return _myPlexServer;

  if (uuid.Equals("local"))
    return _localServer;

  if (m_serverMap.find(uuid) != m_serverMap.end())
  {
    return m_serverMap.find(uuid)->second;
  }
  return CPlexServerPtr();
}

PlexServerList
CPlexServerManager::GetAllServers(CPlexServerOwnedModifier modifier) const
{
  CSingleLock lk(m_serverManagerLock);

  PlexServerList ret;

  BOOST_FOREACH(PlexServerPair p, m_serverMap)
  {
    if (modifier == SERVER_OWNED && p.second->GetOwned())
      ret.push_back(p.second);
    else if (modifier == SERVER_SHARED && !p.second->GetOwned())
      ret.push_back(p.second);
    else if (modifier == SERVER_ALL)
      ret.push_back(p.second);
  }

  return ret;
}

void
CPlexServerManager::MarkServersAsRefreshing()
{
  BOOST_FOREACH(PlexServerPair p, m_serverMap)
    p.second->MarkAsRefreshing();
}

void
CPlexServerManager::UpdateFromConnectionType(PlexServerList servers, int connectionType)
{
  CSingleLock lk(m_serverManagerLock);

  MarkServersAsRefreshing();
  BOOST_FOREACH(CPlexServerPtr p, servers)
    MergeServer(p);

  ServerRefreshComplete(connectionType);
  UpdateReachability();
  save();
}

void
CPlexServerManager::UpdateFromDiscovery(CPlexServerPtr server)
{
  CSingleLock lk(m_serverManagerLock);

  MergeServer(server);
  NotifyAboutServer(server);
  SetBestServer(server, false);
}

void
CPlexServerManager::MergeServer(CPlexServerPtr server)
{
  CSingleLock lk(m_serverManagerLock);

  if (m_serverMap.find(server->GetUUID()) != m_serverMap.end())
  {
    CPlexServerPtr existingServer = m_serverMap.find(server->GetUUID())->second;
    existingServer->Merge(server);
    CLog::Log(LOGDEBUG, "CPlexServerManager::MergeServer Merged %s with %d connection, now we have %d total connections.",
              server->GetName().c_str(), server->GetNumConnections(),
              existingServer->GetNumConnections());
  }
  else
  {
    m_serverMap[server->GetUUID()] = server;
    CLog::Log(LOGDEBUG, "CPlexServerManager::MergeServer Added a new server %s with %d connections", server->GetName().c_str(), server->GetNumConnections());
  }
}

void
CPlexServerManager::ServerRefreshComplete(int connectionType)
{
  vector<CStdString> serversToRemove;

  BOOST_FOREACH(PlexServerPair p, m_serverMap)
  {
    if (!p.second->MarkUpdateFinished(connectionType))
      serversToRemove.push_back(p.first);
  }

  BOOST_FOREACH(CStdString uuid, serversToRemove)
  {
    CLog::Log(LOGDEBUG, "CPlexServerManager::ServerRefreshComplete removing server %s", uuid.c_str());
    NotifyAboutServer(m_serverMap.find(uuid)->second, false);
    m_serverMap.erase(uuid);
  }
}

void
CPlexServerManager::UpdateReachability(bool force)
{
  CSingleLock lk(m_serverManagerLock);

  CLog::Log(LOGDEBUG, "CPlexServerManager::UpdateReachability Updating reachability (force=%s)", force ? "YES" : "NO");

  BOOST_FOREACH(PlexServerPair p, m_serverMap)
    new CPlexServerReachabilityThread(p.second, force);
}

void
CPlexServerManager::SetBestServer(CPlexServerPtr server, bool force)
{
  CSingleLock lk(m_serverManagerLock);
  if (!m_bestServer || force || m_bestServer == server)
  {
    m_bestServer = server;

    CGUIMessage msg(GUI_MSG_PLEX_BEST_SERVER_UPDATED, 0, 0);
    msg.SetStringParam(server->GetUUID());
    g_windowManager.SendThreadMessage(msg);
  }
}

void
CPlexServerManager::ClearBestServer()
{
  m_bestServer.reset();
}

void CPlexServerManager::ServerReachabilityDone(CPlexServerPtr server, bool success)
{
  if (success)
  {
    if (server->GetOwned() &&
        (server->GetServerClass().empty() || !server->GetServerClass().Equals(PLEX_SERVER_CLASS_SECONDARY)))
      SetBestServer(server, false);
    NotifyAboutServer(server);
  }
  else
  {
    if (m_bestServer==server)
      ClearBestServer();

    NotifyAboutServer(server, false);
  }
}

void
CPlexServerManager::NotifyAboutServer(CPlexServerPtr server, bool added)
{
  CGUIMessage msg(GUI_MSG_PLEX_SERVER_NOTIFICATION, 0, 0, added ? 1 : 0);
  msg.SetStringParam(server->GetUUID());
  g_windowManager.SendThreadMessage(msg);

  if (added)
    g_plexServerDataLoader.LoadDataFromServer(server);
  else
    g_plexServerDataLoader.RemoveServer(server);
}

void CPlexServerManager::save()
{
  CXBMCTinyXML xml;
  TiXmlElement srvmgr("serverManager");
  srvmgr.SetAttribute("version", PLEX_SERVER_MANAGER_XML_FORMAT_VERSION);

  if (m_bestServer)
    srvmgr.SetAttribute("bestServer", m_bestServer->GetUUID().c_str());

  TiXmlNode *root = xml.InsertEndChild(srvmgr);

  CSingleLock lk(m_serverManagerLock);

  BOOST_FOREACH(PlexServerPair p, m_serverMap)
  {
    p.second->save(root);
  }

  xml.SaveFile(PLEX_SERVER_MANAGER_XML_FILE);
}

void CPlexServerManager::load()
{
  if (XFILE::CFile::Exists(PLEX_SERVER_MANAGER_XML_FILE))
  {
    CXBMCTinyXML doc;
    if (doc.LoadFile(PLEX_SERVER_MANAGER_XML_FILE))
    {
      TiXmlElement* element = doc.FirstChildElement();
      if (!element)
        return;

      std::string bestServer;
      element->QueryStringAttribute("bestServer", &bestServer);

      element = element->FirstChildElement();

      while (element)
      {
        CPlexServerPtr server = CPlexServer::load(element);
        if (server)
        {
          CLog::Log(LOGDEBUG, "CPlexServerManager::load got server %s from xml file", server->GetName().c_str());
          m_serverMap[server->GetUUID()] = server;
          NotifyAboutServer(server);
        }

        element = element->NextSiblingElement();
      }

      if (!bestServer.empty())
        SetBestServer(FindByUUID(bestServer), true);
    }
    else
    {
      CLog::Log(LOGWARNING, "CPlexServerManager::load failed to open %s: %s", PLEX_SERVER_MANAGER_XML_FILE, doc.ErrorDesc());
    }

    CLog::Log(LOGDEBUG, "CPlexServerManager::load Got %ld servers from plexservermanager.xml", m_serverMap.size());
  }
}