//
//  PlexDirectory.h
//  Plex
//
//  Created by Tobias Hieta <tobias@plexapp.com> on 2013-04-05.
//  Copyright 2013 Plex Inc. All rights reserved.
//

#ifndef PLEXDIRECTORY_H
#define PLEXDIRECTORY_H

#include "filesystem/IDirectory.h"
#include "URL.h"
#include "XBMCTinyXML.h"
#include "FileItem.h"

#include "PlexAttributeParser.h"

#include <map>
#include <boost/foreach.hpp>

#include "PlexTypes.h"
#include "JobManager.h"

#include "utils/log.h"

#include "FileSystem/PlexFile.h"

namespace XFILE
{
  class CPlexDirectory : public IDirectory, public IJobCallback
  {
    public:

      class CPlexDirectoryFetchJob : public CJob
      {
        public:
          CPlexDirectoryFetchJob(const CURL &url) : CJob(), m_url(url) {}

          virtual bool operator==(const CJob* job) const
          {
            const CPlexDirectoryFetchJob *fjob = static_cast<const CPlexDirectoryFetchJob*>(job);
            if (fjob && fjob->m_url.Get() == m_url.Get())
              return true;
            return false;
          }

          virtual const char* GetType() const { return "plexdirectoryfetch"; }

          virtual bool DoWork();

          CFileItemList m_items;
          CURL m_url;
      };

      CPlexDirectory() : m_isAugmented(false) {}

      bool GetDirectory(const CURL& url, CFileItemList& items);

      /* plexserver://shared */
      bool GetSharedServerDirectory(CFileItemList& items);

      /* plexserver://channels */
      bool GetChannelDirectory(CFileItemList& items);

      /* plexserver://channeldirectory */
      bool GetOnlineChannelDirectory(CFileItemList &items);

      virtual bool GetDirectory(const CStdString& strPath, CFileItemList& items)
      {
        return GetDirectory(CURL(strPath), items);
      }

      virtual void CancelDirectory();

      static EPlexDirectoryType GetDirectoryType(const CStdString& typeStr);
      static CStdString GetDirectoryTypeString(EPlexDirectoryType typeNr);

      static CStdString GetContentFromType(EPlexDirectoryType typeNr);

      /* Legacy functions we need to revisit */
      void SetBody(const CStdString& body) { m_body = body; }
      static CFileItemListPtr GetFilterList() { return CFileItemListPtr(); }
      CStdString GetData() const { return m_data; }

      static void CopyAttributes(TiXmlElement* element, CFileItem* fileItem, const CURL &url);
      static CFileItemPtr NewPlexElement(TiXmlElement *element, const CFileItem& parentItem, const CURL &url = CURL());

      static bool IsFolder(const CFileItemPtr& item, TiXmlElement* element);

      virtual void OnJobComplete(unsigned int jobID, bool success, CJob *job);

      long GetHTTPResponseCode() const { return m_file.GetLastHTTPResponseCode(); }
    
      virtual DIR_CACHE_TYPE GetCacheType(const CStdString& strPath) const { return DIR_CACHE_NEVER; };

    private:
      bool ReadMediaContainer(TiXmlElement* root, CFileItemList& mediaContainer);
      void ReadChildren(TiXmlElement* element, CFileItemList& container);

      void DoAugmentation(CFileItemList& fileItems);

      void AddAugmentation(const CURL &url)
      {
        CLog::Log(LOGDEBUG, "CPlexDirectory::AddAugmentation %s", url.Get().c_str());
        CSingleLock lk(m_augmentationLock);
        int id = CJobManager::GetInstance().AddJob(new CPlexDirectoryFetchJob(url), this, CJob::PRIORITY_HIGH);
        m_augmentationJobs.push_back(id);
        m_isAugmented = true;
      }

      void CancelAugmentations()
      {
        CSingleLock lk(m_augmentationLock);
        BOOST_FOREACH(int id, m_augmentationJobs)
          CJobManager::GetInstance().CancelJob(id);
      }

      CCriticalSection m_augmentationLock;
      std::vector<int> m_augmentationJobs;
      bool m_isAugmented;

      std::vector<CFileItemList*> m_augmentationItems;
      CEvent m_augmentationEvent;

      CStdString m_body;
      CStdString m_data;
      CURL m_url;

      CPlexFile m_file;

  };
}



#endif // PLEXDIRECTORY_H
