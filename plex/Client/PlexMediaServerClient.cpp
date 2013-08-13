//
//  PlexMediaServerClient.cpp
//  Plex Home Theater
//
//  Created by Tobias Hieta on 2013-06-11.
//
//

#include "PlexMediaServerClient.h"
#include <boost/lexical_cast.hpp>
#include <string>
#include "PlexFile.h"
#include "video/VideoInfoTag.h"
#include "Client/PlexTranscoderClient.h"

using namespace XFILE;

bool
CPlexMediaServerClientJob::DoWork()
{
  CPlexFile file;
  bool success;
  
  if (m_put)
    success = file.Put(m_url.Get(), m_data);
  else
    success = file.Get(m_url.Get(), m_data);
  
  return success;
}

void
CPlexMediaServerClient::SelectStream(const CFileItemPtr &item,
                                     int partID,
                                     int subtitleStreamID,
                                     int audioStreamID)
{
  CURL u(item->GetPath());
  
  u.SetFileName("/library/parts/" + boost::lexical_cast<std::string>(partID));
  if (subtitleStreamID != -1)
    u.SetOption("subtitleStreamID", boost::lexical_cast<std::string>(subtitleStreamID));
  if (audioStreamID != -1)
    u.SetOption("audioStreamID", boost::lexical_cast<std::string>(audioStreamID));
  
  AddJob(new CPlexMediaServerClientJob(u, true));
}

void CPlexMediaServerClient::ReportItemProgress(const CFileItemPtr &item, const CStdString& state, int64_t currentPosition)
{
  CURL u(item->GetPath());
  u.SetFileName("/:/timeline");

  u.SetOption("state", state);
  u.SetOption("ratingKey", item->GetProperty("ratingKey").asString());
  u.SetOption("key", item->GetProperty("unprocessed_key").asString());
  u.SetOption("containerKey", item->GetProperty("containerKey").asString());
  
  if (item->HasProperty("guid"))
    u.SetOption("guid", item->GetProperty("guid").asString());

  if (item->HasProperty("url"))
    u.SetOption("url", item->GetProperty("url").asString());
  
  if (currentPosition != 0)
    u.SetOption("time", boost::lexical_cast<std::string>(currentPosition));
  
  if (item->HasVideoInfoTag())
    u.SetOption("duration", boost::lexical_cast<std::string>(item->GetVideoInfoTag()->m_duration * 1000));
  
  AddJob(new CPlexMediaServerClientJob(u));
}

void
CPlexMediaServerClient::ReportItemProgress(const CFileItemPtr &item, CPlexMediaServerClient::MediaState state, int64_t currentPosition)
{
  CStdString strstate;
  switch (state) {
    case MEDIA_STATE_STOPPED:
      strstate = "stopped";
      break;
    case MEDIA_STATE_BUFFERING:
      strstate = "buffering";
      break;
    case MEDIA_STATE_PLAYING:
      strstate = "playing";
      break;
    case MEDIA_STATE_PAUSED:
      strstate = "paused";
      break;
  }
  ReportItemProgress(item, strstate, currentPosition);
}

void
CPlexMediaServerClient::SetItemWatchStatus(const CFileItemPtr &item, bool watched)
{
  CURL u(item->GetPath());
  
  u.SetFileName(GetPrefix(item) + (watched ? "scrobble" : "unscrobble"));
  u.SetOption("key", item->GetProperty("ratingKey").asString());
  u.SetOption("identifier", item->GetProperty("identifier").asString());
  
  AddJob(new CPlexMediaServerClientJob(u));
}

void
CPlexMediaServerClient::SetItemRating(const CFileItemPtr &item, float rating)
{
  CURL u(item->GetPath());
  
  u.SetFileName(GetPrefix(item) + "rate");
  u.SetOption("rating", boost::lexical_cast<std::string>(rating));
  
  AddJob(new CPlexMediaServerClientJob(u));
}

void
CPlexMediaServerClient::SetViewMode(const CFileItem &item, int viewMode, int sortMode, int sortAsc)
{
  CURL u(item.GetPath());
  u.SetFileName("/:/viewChange");
  u.SetOption("identifier", item.GetProperty("identifier").asString());
  u.SetOption("viewGroup", item.GetProperty("viewGroup").asString());

  u.SetOption("viewMode", boost::lexical_cast<CStdString>(viewMode));
  u.SetOption("sortMode", boost::lexical_cast<CStdString>(sortMode));
  u.SetOption("sortAsc", boost::lexical_cast<CStdString>(sortAsc));

  AddJob(new CPlexMediaServerClientJob(u));
}

void CPlexMediaServerClient::StopTranscodeSession(CPlexServerPtr server)
{
  AddJob(new CPlexMediaServerClientJob(CPlexTranscoderClient::GetTranscodeStopURL(server)));
}
