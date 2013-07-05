#include "PlexServerDataLoader.h"
#include "FileSystem/PlexDirectory.h"
#include "GUIWindowManager.h"
#include "GUIMessage.h"

#include "PlexTypes.h"

#include <boost/foreach.hpp>

#include "utils/log.h"

using namespace XFILE;

CPlexServerDataLoader::CPlexServerDataLoader() : CJobQueue(false, 4, CJob::PRIORITY_NORMAL)
{
}

void
CPlexServerDataLoader::LoadDataFromServer(const CPlexServerPtr &server)
{
  CLog::Log(LOGDEBUG, "CPlexServerDataLoader::LoadDataFromServer loading data for server %s", server->GetName().c_str());
  AddJob(new CPlexServerDataLoaderJob(server));
}

void CPlexServerDataLoader::RemoveServer(const CPlexServerPtr &server)
{
  CSingleLock lk(m_dataLock);
  if (m_sectionMap.find(server->GetUUID()) != m_sectionMap.end())
  {
    CLog::Log(LOG_LEVEL_DEBUG, "CPlexServerDataLoader::RemoveServer from sectionMap %s", server->GetName().c_str());
    m_sectionMap.erase(server->GetUUID());
  }

  if (m_sharedSectionsMap.find(server->GetUUID()) != m_sharedSectionsMap.end())
  {
    CLog::Log(LOG_LEVEL_DEBUG, "CPlexServerDataLoader::RemoveServer from sharedSectionMap %s", server->GetName().c_str());
    m_sharedSectionsMap.equal_range(server->GetUUID());
  }

  if (m_channelMap.find(server->GetUUID()) != m_channelMap.end())
  {
    CLog::Log(LOG_LEVEL_DEBUG, "CPlexServerDataLoader::RemoveServer from channelMap %s", server->GetName().c_str());
    m_channelMap.equal_range(server->GetUUID());
  }

  CGUIMessage msg(GUI_MSG_PLEX_SERVER_DATA_UNLOADED, PLEX_DATA_LOADER, 0);
  msg.SetStringParam(server->GetUUID());
  g_windowManager.SendThreadMessage(msg);
}

void
CPlexServerDataLoader::OnJobComplete(unsigned int jobID, bool success, CJob *job)
{
  CPlexServerDataLoaderJob *j = (CPlexServerDataLoaderJob*)job;
  CLog::Log(LOGDEBUG, "CPlexServerDataLoader::OnJobComplete (%s) %s", j->m_server->GetName().c_str(), success ? "success" : "failed");
  if (success)
  {
    CSingleLock lk(m_dataLock);
    if (j->m_sectionList) {
      CFileItemListPtr sectionList = j->m_sectionList;
      sectionList->SetProperty("serverUUID", j->m_server->GetUUID());
      sectionList->SetProperty("serverName", j->m_server->GetName());

      if (j->m_server->GetOwned())
        m_sectionMap[j->m_server->GetUUID()] = sectionList;
      else
        m_sharedSectionsMap[j->m_server->GetUUID()] = sectionList;
    }
    
    if (j->m_channelList)
      m_channelMap[j->m_server->GetUUID()] = j->m_channelList;

    CGUIMessage msg(GUI_MSG_PLEX_SERVER_DATA_LOADED, PLEX_DATA_LOADER, 0);
    msg.SetStringParam(j->m_server->GetUUID());
    g_windowManager.SendThreadMessage(msg);
  }

  CJobQueue::OnJobComplete(jobID, success, job);
}

CFileItemListPtr
CPlexServerDataLoader::GetSectionsForUUID(const CStdString &uuid)
{
  CSingleLock lk(m_dataLock);

  if (m_sectionMap.find(uuid) != m_sectionMap.end())
    return m_sectionMap[uuid];

  /* not found in our server map, check shared servers */
  if (m_sharedSectionsMap.find(uuid) != m_sectionMap.end())
    return m_sectionMap[uuid];

  return CFileItemListPtr();
}

CFileItemListPtr
CPlexServerDataLoader::GetChannelsForUUID(const CStdString &uuid)
{
  CSingleLock lk(m_dataLock);
  if (m_channelMap.find(uuid) != m_channelMap.end())
    return m_channelMap[uuid];
  return CFileItemListPtr();
}

CFileItemListPtr
CPlexServerDataLoaderJob::FetchList(const CStdString& path)
{
  CPlexDirectory dir;
  CURL url = m_server->BuildPlexURL(path);
  CFileItemList* list = new CFileItemList;

  if (dir.GetDirectory(url.Get(), *list))
    return CFileItemListPtr(list);

  delete list;
  return CFileItemListPtr();
}

bool
CPlexServerDataLoaderJob::DoWork()
{
  m_sectionList = FetchList("/library/sections");

  if (m_server->GetOwned())
    m_channelList = FetchList("/channels/all");

  return true;
}

CFileItemListPtr
CPlexServerDataLoader::GetAllSharedSections() const
{
  CSingleLock lk(m_dataLock);
  CFileItemList* list = new CFileItemList;

  BOOST_FOREACH(ServerDataPair pair, m_sharedSectionsMap)
  {
    for (int i = 0; i < pair.second->Size(); i++)
    {
      CFileItemPtr item = pair.second->Get(i);
      item->SetProperty("serverName", pair.second->GetProperty("serverName"));
      item->SetProperty("serverUUID", pair.second->GetProperty("serverUUID"));
      list->Add(item);
    }
  }

  return CFileItemListPtr(list);
}

CFileItemListPtr
CPlexServerDataLoader::GetAllSections() const
{
  CSingleLock lk(m_dataLock);
  CFileItemList* list = new CFileItemList;

  BOOST_FOREACH(ServerDataPair pair, m_sectionMap)
  {
    for (int i = 0; i < pair.second->Size(); i++)
    {
      CFileItemPtr item = pair.second->Get(i);
      item->SetProperty("serverName", pair.second->GetProperty("serverName"));
      item->SetProperty("serverUUID", pair.second->GetProperty("serverUUID"));
      list->Add(item);
    }
  }

  return CFileItemListPtr(list);
}

CFileItemListPtr CPlexServerDataLoader::GetAllChannels() const
{
  CSingleLock lk(m_dataLock);
  CFileItemList* list = new CFileItemList;

  BOOST_FOREACH(ServerDataPair pair, m_channelMap)
  {
    for (int i = 0; i < pair.second->Size(); i++)
      list->Add(pair.second->Get(i));
  }

  return CFileItemListPtr(list);
}
