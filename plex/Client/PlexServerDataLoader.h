#pragma once

#include "Client/PlexServer.h"
#include "Client/PlexServerManager.h"

#include "GlobalsHandling.h"
#include "JobManager.h"

#include <boost/shared_ptr.hpp>
#include <map>

#include "FileItem.h"
#include "plex/PlexTypes.h"
#include "threads/Timer.h"

/* Maps UUID->list of sections for the section */
typedef std::map<CStdString, CFileItemListPtr> ServerDataMap;
typedef std::pair<CStdString, CFileItemListPtr> ServerDataPair;
typedef std::map<CStdString, CPlexServerPtr> ServerMap;

class CPlexServerDataLoaderJob : public CJob
{
public:
  CPlexServerDataLoaderJob(const CPlexServerPtr& server) : m_server(server) {}

  bool DoWork();
  CFileItemListPtr FetchList(const CStdString& path);

  CPlexServerPtr m_server;
  CFileItemListPtr m_sectionList;
  CFileItemListPtr m_channelList;

  virtual bool operator==(const CJob* job) const
  {
    CPlexServerDataLoaderJob *oJob = (CPlexServerDataLoaderJob*)job;
    if (oJob->m_server == m_server)
      return true;
    return false;
  }
};

class CPlexServerDataLoader : public CJobQueue, public ITimerCallback
{
public:
  CPlexServerDataLoader();

  void LoadDataFromServer(const CPlexServerPtr& server);
  void RemoveServer(const CPlexServerPtr& server);

  CFileItemListPtr GetSectionsForUUID(const CStdString& uuid);
  CFileItemListPtr GetSectionsForServer(const CPlexServerPtr &server) { return GetSectionsForUUID(server->GetUUID()); }
  CFileItemListPtr GetChannelsForUUID(const CStdString& uuid);
  CFileItemListPtr GetChannelsForServer(const CPlexServerPtr &server) { return GetChannelsForUUID(server->GetUUID()); }

  CFileItemListPtr GetAllSections() const;
  CFileItemListPtr GetAllSharedSections() const;
  CFileItemListPtr GetAllChannels() const;

  bool HasChannels() const { return m_channelMap.size() > 0; }

  void OnJobComplete(unsigned int jobID, bool success, CJob *job);

private:
  void OnTimeout();

  CTimer *m_refreshTimer;

  CCriticalSection m_dataLock;
  CCriticalSection m_serverLock;

  ServerMap m_servers;

  ServerDataMap m_sectionMap;
  ServerDataMap m_channelMap;

  ServerDataMap m_sharedSectionsMap;
};

XBMC_GLOBAL_REF(CPlexServerDataLoader, g_plexServerDataLoader);
#define g_plexServerDataLoader XBMC_GLOBAL_USE(CPlexServerDataLoader)
