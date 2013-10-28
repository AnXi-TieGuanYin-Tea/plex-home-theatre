//
//  PlexAutoUpdate.h
//  Plex
//
//  Created by Tobias Hieta <tobias@plexapp.com> on 2012-10-24.
//  Copyright 2012 Plex Inc. All rights reserved.
//

#ifndef PLEXAUTOUPDATE_H
#define PLEXAUTOUPDATE_H

#include <string>
#include <vector>
#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>
#include "threads/Timer.h"
#include "Job.h"
#include "URL.h"
#include "FileItem.h"

#include "threads/Thread.h"

class CPlexAutoUpdate : public ITimerCallback, IJobCallback
{
  public:
    CPlexAutoUpdate(const CURL& updateUrl, int searchFrequency = 21600000); /* 6 hours default */
    void OnTimeout();
    virtual void OnJobComplete(unsigned int jobID, bool success, CJob *job);
    virtual void OnJobProgress(unsigned int jobID, unsigned int progress, unsigned int total, const CJob *job);

    bool IsReadyToInstall() const { return m_ready; }
    bool IsDownloadingUpdate() const { return m_isDownloading; }
    CStdString GetUpdateVersion() const { return m_downloadItem->GetProperty("version").asString(); }
    bool IsSearchingForUpdate() const { return m_isSearching; }
    void UpdateAndRestart();
    void ForceVersionCheckInBackground();
    void ResetTimer();

  private:
    void DownloadUpdate(CFileItemPtr updateItem);
    void ProcessDownloads();

    CFileItemPtr m_downloadItem;
    CFileItemPtr m_downloadPackage;
    int m_searchFrequency;
    CURL m_url;
    CTimer m_timer;

    CStdString m_localManifest;
    CStdString m_localBinary;
    CStdString m_localApplication;

    bool m_forced;
    bool m_isSearching;
    bool m_isDownloading;

    bool m_needManifest;
    bool m_needBinary;
    bool m_needApplication;

    bool m_ready;

    CFileItemPtr GetPackage(CFileItemPtr updateItem);
    bool NeedDownload(const std::string& localFile, const std::string& expectedHash);
    bool RenameLocalBinary();
};

#endif // PLEXAUTOUPDATE_H
