//
//  PlexAutoUpdate.cpp
//  Plex
//
//  Created by Tobias Hieta <tobias@plexapp.com> on 2012-10-24.
//  Copyright 2012 Plex Inc. All rights reserved.
//

#include "PlexAutoUpdate.h"
#include <boost/foreach.hpp>
#include "FileSystem/PlexDirectory.h"
#include "FileItem.h"
#include "PlexJobs.h"
#include "File.h"
#include "Directory.h"
#include "utils/URIUtils.h"
#include "settings/GUISettings.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "ApplicationMessenger.h"
#include "filesystem/SpecialProtocol.h"
#include "PlexApplication.h"
#include "Client/MyPlex/MyPlexManager.h"

#include "xbmc/Util.h"
#include "XBDateTime.h"
#include "GUIInfoManager.h"

using namespace XFILE;

//#define UPDATE_DEBUG 1

CPlexAutoUpdate::CPlexAutoUpdate(const CURL &updateUrl, int searchFrequency)
  : m_forced(false), m_isSearching(false), m_isDownloading(false), m_url(updateUrl), m_searchFrequency(searchFrequency), m_timer(this), m_ready(false)
{
  m_timer.Start(5 * 1000, true);
}

void CPlexAutoUpdate::OnTimeout()
{
  CFileItemList list;
  CPlexDirectory dir;
  m_isSearching = true;

  /* First, check if we tried and updated to a new version */

  std::string version, packageHash;
  bool isDelta;
  if (GetUpdateInfo(version, isDelta, packageHash))
  {
    if (version != g_infoManager.GetVersion())
    {
      CLog::Log(LOGWARNING, "CPlexAutoUpdate::OnTimeout we probably failed to update to version %s since this is %s", version.c_str(), g_infoManager.GetVersion().c_str());
      if (isDelta)
        CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Failed to upgrade!", "PHT failed to (delta) upgrade to version " + version, 10000, true);
      else
        CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Failed to upgrade!", "PHT failed to upgrade to version " + version, 10000, true);
    }
    else
    {
      CLog::Log(LOGINFO, "CPlexAutoUpdate::OnTimeout successfully upgraded to version %s", g_infoManager.GetVersion().c_str());
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "Upgrade succesfull!", "PHT is upgraded to version " + version, 10000, true);
      CFile::Delete("special://temp/autoupdate/plexupdateinfo.xml");
    }
  }

#ifdef UPDATE_DEBUG
  m_url.SetOption("version", "0.0.0.0");
#else
  m_url.SetOption("version", g_infoManager.GetVersion());
#endif
  m_url.SetOption("build", PLEX_BUILD_TAG);
  m_url.SetOption("channel", "6");
  if (g_plexApplication.myPlexManager->IsSignedIn())
    m_url.SetOption("X-Plex-Token", g_plexApplication.myPlexManager->GetAuthToken());

  CFileItemList updates;

  if (dir.GetDirectory(m_url, list))
  {
    m_isSearching = false;

    if (list.Size() > 0)
    {
      for (int i = 0; i < list.Size(); i++)
      {
        CFileItemPtr updateItem = list.Get(i);
        if (updateItem->HasProperty("version") &&
            updateItem->GetProperty("live").asBoolean() &&
            updateItem->GetProperty("autoupdate").asBoolean() &&
            updateItem->GetProperty("version").asString() != g_infoManager.GetVersion())
        {
          CLog::Log(LOGDEBUG, "CPlexAutoUpdate::OnTimeout got version %s from update endpoint", updateItem->GetProperty("version").asString().c_str());
          updates.Add(updateItem);
        }
      }
    }
  }

  CLog::Log(LOGDEBUG, "CPlexAutoUpdate::OnTimeout found %d candidates", updates.Size());
  CFileItemPtr selectedItem;

  for (int i = 0; i < updates.Size(); i++)
  {
    if (!selectedItem)
      selectedItem = updates.Get(i);
    else
    {
      CDateTime time1, time2;
      time1.SetFromDBDateTime(selectedItem->GetProperty("unprocessed_createdAt").asString().substr(0, 19));
      time2.SetFromDBDateTime(list.Get(i)->GetProperty("unprocessed_createdAt").asString().substr(0, 19));

      if (time2 > time1)
        selectedItem = updates.Get(i);
    }
  }

  if (selectedItem)
  {
    CLog::Log(LOGINFO, "CPlexAutoUpdate::OnTimeout update found! %s", selectedItem->GetProperty("version").asString().c_str());
    DownloadUpdate(selectedItem);
    m_timer.Stop();
    return;
  }

  CLog::Log(LOGDEBUG, "CPlexAutoUpdate::OnTimeout no updates available");

  if (m_forced)
  {
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "No update available!", "You are up-to-date!", 10000, false);
    m_forced = false;
  }

  if (g_guiSettings.GetBool("updates.auto"))
    m_timer.SetTimeout(m_searchFrequency);
  else
    m_timer.Stop();

  m_isSearching = false;
}

CFileItemPtr CPlexAutoUpdate::GetPackage(CFileItemPtr updateItem)
{
  CFileItemPtr deltaItem, fullItem;

  std::string version, packageHash;
  bool isDelta;

  GetUpdateInfo(version, isDelta, packageHash);

  if (updateItem && updateItem->m_mediaItems.size() > 0)
  {
    for (int i = 0; i < updateItem->m_mediaItems.size(); i ++)
    {
      CFileItemPtr package = updateItem->m_mediaItems[i];
      if (package->GetProperty("delta").asBoolean())
        deltaItem = package;
      else
        fullItem = package;
    }
  }

  if (deltaItem)
  {
    if (isDelta && deltaItem->GetProperty("manifestHash") == packageHash)
    {
      CLog::Log(LOGINFO, "CPlexAutoUpdate::GetPackage we failed installing delta %s so now we will ignore that.", packageHash.c_str());
      return fullItem;
    }
    else
      return deltaItem;
  }

  return fullItem;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CPlexAutoUpdate::NeedDownload(const std::string& localFile, const std::string& expectedHash)
{
  if (CFile::Exists(localFile, false) && PlexUtils::GetSHA1SumFromURL(CURL(localFile)) == expectedHash)
  {
    CLog::Log(LOGDEBUG, "CPlexAutoUpdate::DownloadUpdate we already have %s with correct SHA", localFile.c_str());
    return false;
  }
  return true;
}

void CPlexAutoUpdate::DownloadUpdate(CFileItemPtr updateItem)
{
  if (m_downloadItem)
    return;

  m_downloadPackage = GetPackage(updateItem);
  if (!m_downloadPackage)
    return;

  m_isDownloading = true;
  m_downloadItem = updateItem;
  m_needManifest = m_needBinary = m_needApplication = false;

  CDirectory::Create("special://temp/autoupdate");

  CStdString manifestUrl = m_downloadPackage->GetProperty("manifestPath").asString();
  CStdString updateUrl = m_downloadPackage->GetProperty("filePath").asString();
//  CStdString applicationUrl = m_downloadItem->GetProperty("updateApplication").asString();

  bool isDelta = m_downloadPackage->GetProperty("delta").asBoolean();
  std::string packageStr = isDelta ? "delta" : "full";
  m_localManifest = "special://temp/autoupdate/manifest-" + m_downloadItem->GetProperty("version").asString() + "." + packageStr + ".xml";
  m_localBinary = "special://temp/autoupdate/binary-" + m_downloadItem->GetProperty("version").asString() + "." + packageStr + ".zip";

  if (NeedDownload(m_localManifest, m_downloadPackage->GetProperty("manifestHash").asString()))
  {
    CLog::Log(LOGDEBUG, "CPlexAutoUpdate::DownloadUpdate need %s", manifestUrl.c_str());
    CJobManager::GetInstance().AddJob(new CPlexDownloadFileJob(manifestUrl, m_localManifest), this, CJob::PRIORITY_LOW);
    m_needManifest = true;
  }

  if (NeedDownload(m_localBinary, m_downloadPackage->GetProperty("fileHash").asString()))
  {
    CLog::Log(LOGDEBUG, "CPlexAutoUpdate::DownloadUpdate need %s", m_localBinary.c_str());
    CJobManager::GetInstance().AddJob(new CPlexDownloadFileJob(updateUrl, m_localBinary), this, CJob::PRIORITY_LOW);
    m_needBinary = true;
  }

  if (!m_needBinary && !m_needManifest)
    ProcessDownloads();
}

bool CPlexAutoUpdate::GetUpdateInfo(std::string& version, bool& isDelta, std::string& packageHash) const
{
  CXBMCTinyXML doc;
  doc.LoadFile("special://temp/autoupdate/plexupdateinfo.xml");
  if (!doc.RootElement())
    return false;

  if (doc.RootElement()->QueryStringAttribute("version", &version) != TIXML_SUCCESS)
    return false;
  if (doc.RootElement()->QueryBoolAttribute("isDelta", &isDelta) != TIXML_SUCCESS)
    return false;
  if (doc.RootElement()->QueryStringAttribute("packageHash", &packageHash) != TIXML_SUCCESS)
    return false;

  return true;
}

void CPlexAutoUpdate::WriteUpdateInfo()
{
  CXBMCTinyXML doc;

  doc.LinkEndChild(new TiXmlDeclaration("1.0", "utf-8", ""));
  TiXmlElement* el = new TiXmlElement("Update");
  el->SetAttribute("version", m_downloadItem->GetProperty("version").asString());
  el->SetAttribute("packageHash", m_downloadPackage->GetProperty("manifestHash").asString());
  el->SetAttribute("isDelta", m_downloadPackage->GetProperty("delta").asBoolean());
  doc.LinkEndChild(el);

  doc.SaveFile("special://temp/autoupdate/plexupdateinfo.xml");
}

void CPlexAutoUpdate::ProcessDownloads()
{
  CStdString verStr;
  verStr.Format("Version %s is now ready to be installed.", GetUpdateVersion());
  CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "Update available!", verStr, 10000, false);

  CGUIMessage msg(GUI_MSG_UPDATE_MAIN_MENU, PLEX_AUTO_UPDATER, 0);
  CApplicationMessenger::Get().SendGUIMessage(msg, WINDOW_HOME);

  m_isDownloading = false;
  m_ready = true;
  m_timer.Stop(); // no need to poll for any more updates
}

void CPlexAutoUpdate::OnJobComplete(unsigned int jobID, bool success, CJob *job)
{
  CPlexDownloadFileJob *fj = static_cast<CPlexDownloadFileJob*>(job);
  if (fj && success)
  {
    if (fj->m_destination == m_localManifest)
    {
      if (NeedDownload(m_localManifest, m_downloadPackage->GetProperty("manifestHash").asString()))
      {
        CLog::Log(LOGWARNING, "CPlexAutoUpdate::OnJobComplete failed to download manifest, SHA mismatch. Retrying in %d seconds", m_searchFrequency);
        return;
      }

      CLog::Log(LOGDEBUG, "CPlexAutoUpdate::OnJobComplete got manifest.");
      m_needManifest = false;
    }
    else if (fj->m_destination == m_localBinary)
    {
      if (NeedDownload(m_localBinary, m_downloadPackage->GetProperty("fileHash").asString()))
      {
        CLog::Log(LOGWARNING, "CPlexAutoUpdate::OnJobComplete failed to download update, SHA mismatch. Retrying in %d seconds", m_searchFrequency);
        return;
      }

      CLog::Log(LOGDEBUG, "CPlexAutoUpdate::OnJobComplete got update binary.");
      m_needBinary = false;
    }
    else
      CLog::Log(LOGDEBUG, "CPlexAutoUpdate::OnJobComplete What is %s", fj->m_destination.c_str());
  }
  else if (!success)
  {
    CLog::Log(LOGWARNING, "CPlexAutoUpdate::OnJobComplete failed to run a download job, will retry in %d seconds.", m_searchFrequency);
    m_timer.Start(m_searchFrequency, true);
    return;
  }

  if (!m_needApplication && !m_needBinary && !m_needManifest)
    ProcessDownloads();
}

void CPlexAutoUpdate::OnJobProgress(unsigned int jobID, unsigned int progress, unsigned int total, const CJob *job)
{
}

bool CPlexAutoUpdate::RenameLocalBinary()
{
  CXBMCTinyXML doc;

  doc.LoadFile(m_localManifest);
  if (!doc.RootElement())
  {
    CLog::Log(LOGWARNING, "CPlexAutoUpdate::RenameLocalBinary failed to parse mainfest!");
    return false;
  }

  std::string newName;
  TiXmlElement *el = doc.RootElement()->FirstChildElement();
  while(el)
  {
    if (el->ValueStr() == "packages" || el->ValueStr() == "package")
    {
      el=el->FirstChildElement();
      continue;
    }
    if (el->ValueStr() == "name")
    {
      newName = el->GetText();
      break;
    }

    el = el->NextSiblingElement();
  }

  if (newName.empty())
  {
    CLog::Log(LOGWARNING, "CPlexAutoUpdater::RenameLocalBinary failed to get the new name from the manifest!");
    return false;
  }

  std::string bpath = CSpecialProtocol::TranslatePath(m_localBinary);
  std::string tgd = CSpecialProtocol::TranslatePath("special://temp/autoupdate/" + newName + ".zip");

  return CopyFile(bpath.c_str(), tgd.c_str(), false);
}

#ifdef TARGET_POSIX
#include <signal.h>
#endif

#ifdef TARGET_DARWIN_OSX
#include "DarwinUtils.h"
#endif
 
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

std::string quoteArgs(const std::list<std::string>& arguments)
{
	std::string quotedArgs;
	for (std::list<std::string>::const_iterator iter = arguments.begin();
	     iter != arguments.end();
	     iter++)
	{
		std::string arg = *iter;

		bool isQuoted = !arg.empty() &&
		                 arg.at(0) == '"' &&
		                 arg.at(arg.size()-1) == '"';

		if (!isQuoted && arg.find(' ') != std::string::npos)
		{
			arg.insert(0,"\"");
			arg.append("\"");
		}
		quotedArgs += arg;
		quotedArgs += " ";
	}
	return quotedArgs;
}

void CPlexAutoUpdate::UpdateAndRestart()
{  
  /* first we need to copy the updater app to our tmp directory, it might change during install.. */
  CStdString updaterPath;
  CUtil::GetHomePath(updaterPath);

#ifdef TARGET_DARWIN_OSX
  updaterPath += "/tools/updater";
#elif TARGET_WINDOWS
  updaterPath += "\\updater.exe";
#endif

#ifdef TARGET_DARWIN_OSX
  std::string updater = CSpecialProtocol::TranslatePath("special://temp/autoupdate/updater");
#elif TARGET_WINDOWS
  std::string updater = CSpecialProtocol::TranslatePath("special://temp/autoupdate/updater.exe");
#endif

  if (!CopyFile(updaterPath.c_str(), updater.c_str(), false))
  {
    CLog::Log(LOGWARNING, "CPlexAutoUpdate::UpdateAndRestart failed to copy %s to %s", updaterPath.c_str(), updater.c_str());
    return;
  }

#ifdef TARGET_POSIX
  chmod(updater.c_str(), 0755);
#endif

  if (!RenameLocalBinary())
    return;

  std::string script = CSpecialProtocol::TranslatePath(m_localManifest);
  std::string packagedir = CSpecialProtocol::TranslatePath("special://temp/autoupdate");
  CStdString appdir;

#ifdef TARGET_DARWIN_OSX
  char installdir[2*MAXPATHLEN];

  uint32_t size;
  GetDarwinBundlePath(installdir, &size);
  appdir = std::string(installdir) + "/..";
#elif TARGET_WINDOWS
  CUtil::GetHomePath(appdir);
#endif

#ifdef TARGET_POSIX
  CStdString args;
  args.Format("--install-dir \"%s\" --package-dir \"%s\" --script \"%s\" --auto-close", appdir, packagedir, script);
  WriteUpdateInfo();

  CStdString exec;
  exec.Format("\"%s\" %s", updater, args);

  CLog::Log(LOGDEBUG, "CPlexAutoUpdate::UpdateAndRestart going to run %s", exec.c_str());

  pid_t pid = fork();
  if (pid == -1)
  {
    CLog::Log(LOGWARNING, "CPlexAutoUpdate::UpdateAndRestart major fail when installing update, can't fork!");
    return;
  }
  else if (pid == 0)
  {
    /* hack! we don't know the parents all open file descriptiors, so we need
     * to loop over them and kill them :( not nice! */
    struct rlimit rlim;

    if (getrlimit(RLIMIT_NOFILE, &rlim) == -1)
    {
      fprintf(stderr, "Couldn't get the max number of fd's!");
      exit(1);
    }

    int maxFd = rlim.rlim_cur;
    fprintf(stderr, "Total number of fd's %d\n", maxFd);
    for (int i = 3; i < maxFd; ++i)
      close(i);

    /* Child */
    pid_t parentPid = getppid();
    fprintf(stderr, "Waiting for PHT to quit...\n");

    time_t start = time(NULL);

    while(kill(parentPid, SIGHUP) == 0)
    {
      /* wait for parent process 30 seconds... */
      if ((time(NULL) - start) > 30)
      {
        fprintf(stderr, "PHT still haven't quit after 30 seconds, let's be a bit more forceful...sending KILL to %d\n", parentPid);
        kill(parentPid, SIGKILL);
        usleep(1000 * 100);
        start = time(NULL);
      }
      else
        usleep(1000 * 10);
    }

    fprintf(stderr, "PHT seems to have quit, running updater\n");

    system(exec.c_str());

    exit(0);
  }
  else
  {
    CApplicationMessenger::Get().Quit();
  }
#elif TARGET_WINDOWS
  DWORD pid = GetCurrentProcessId();

  std::list<std::string> args;
  args.push_back("--wait");
  args.push_back(boost::lexical_cast<std::string>(pid));
  
  args.push_back("--install-dir");
  args.push_back(appdir);

  args.push_back("--package-dir");
  args.push_back(packagedir);

  args.push_back("--script");
  args.push_back(script);

  args.push_back("--auto-close");

  char *arguments = strdup(quoteArgs(args).c_str());

  CLog::Log(LOGDEBUG, "CPlexAutoUpdate::UpdateAndRestart going to run %s %s", updater.c_str(), arguments);

	STARTUPINFO startupInfo;
	ZeroMemory(&startupInfo,sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);

	PROCESS_INFORMATION processInfo;
	ZeroMemory(&processInfo,sizeof(processInfo));


  if (CreateProcess(updater.c_str(), arguments, NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS, NULL, NULL, &startupInfo, &processInfo) == 0)
  {
    CLog::Log(LOGWARNING, "CPlexAutoUpdate::UpdateAndRestart CreateProcess failed! %d", GetLastError());
  }
  else
  {
    //CloseHandle(pInfo.hProcess);
    //CloseHandle(pInfo.hProcess);
    CApplicationMessenger::Get().Quit();
  }

  free(arguments);

#endif
}

void CPlexAutoUpdate::ForceVersionCheckInBackground()
{
  if (m_timer.IsRunning())
    m_timer.Stop(true);

  m_forced = true;
  m_isSearching = true;
  // restart with a short time out, just to make sure that we get it running in the background thread
  m_timer.Start(1);
}

void CPlexAutoUpdate::ResetTimer()
{
  if (g_guiSettings.GetBool("updates.auto"))
  {
    if (m_timer.IsRunning())
      m_timer.Stop(true);
    m_timer.Start(m_searchFrequency);
  }
}
