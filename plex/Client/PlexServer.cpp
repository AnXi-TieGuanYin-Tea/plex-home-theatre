
#include "Client/PlexServer.h"
#include "utils/XBMCTinyXML.h"
#include "utils/StringUtils.h"
#include "utils/log.h"
#include "threads/SingleLock.h"
#include "PlexConnection.h"

#include <boost/foreach.hpp>
#include <boost/timer.hpp>

using namespace std;

CPlexServerConnTestThread::CPlexServerConnTestThread(CPlexConnectionPtr conn, CPlexServerPtr server)
  : CThread("ConnectionTest: " + conn->GetAddress().GetHostName()), m_conn(conn), m_server(server)
{
  Create(true);
}

void
CPlexServerConnTestThread::Process()
{
  bool success = false;
  boost::timer t;
  CPlexConnection::ConnectionState state = m_conn->TestReachability(m_server);

  if (state == CPlexConnection::CONNECTION_STATE_REACHABLE)
  {
    CLog::Log(LOGDEBUG, "CPlexServerConnTestJob:DoWork took %f sec, Connection SUCCESS %s ~ localConn: %s conn: %s",
              t.elapsed(), m_server->GetName().c_str(), m_conn->IsLocal() ? "YES" : "NO", m_conn->GetAddress().Get().c_str());
    success = true;
  }
  else
  {
    CLog::Log(LOGDEBUG, "CPlexServerConnTestJob:DoWork took %f sec, Connection FAILURE %s ~ localConn: %s conn: %s",
              t.elapsed(), m_server->GetName().c_str(), m_conn->IsLocal() ? "YES" : "NO", m_conn->GetAddress().Get().c_str());
  }

  m_server->OnConnectionTest(m_conn, success);
  
  m_conn.reset();
  m_server.reset();
}

bool
CPlexServer::CollectDataFromRoot(const CStdString xmlData)
{
  CSingleLock lk(m_serverLock);

  CXBMCTinyXML doc;
  doc.Parse(xmlData);
  if (doc.RootElement() != 0)
  {
    TiXmlElement* root = doc.RootElement();
    bool boolValue;
    std::string uuid;

    /* first we need to check that this is the server we should talk to */
    if (root->QueryStringAttribute("machineIdentifier", &uuid))
    {
      if (!m_uuid.Equals(uuid.c_str()))
      {
        CLog::Log(LOGWARNING, "CPlexServer::CollectDataFromRoot we wanted to talk to %s but got %s, dropping this connection.", m_uuid.c_str(), uuid.c_str());
        return false;
      }
    }

    if (root->QueryBoolAttribute("allowMediaDeletion", &boolValue) == TIXML_SUCCESS)
      m_supportsDeletion = boolValue;

    if (root->QueryBoolAttribute("transcoderAudio", &boolValue) == TIXML_SUCCESS)
      m_supportsAudioTranscoding = boolValue;

    if (root->QueryBoolAttribute("transcoderVideo", &boolValue) == TIXML_SUCCESS)
      m_supportsVideoTranscoding = boolValue;

    root->QueryStringAttribute("serverClass", &m_serverClass);
    root->QueryStringAttribute("version", &m_version);

    CStdString stringValue;
    if (root->QueryStringAttribute("transcoderVideoResolutions", &stringValue) == TIXML_SUCCESS)
      m_transcoderResolutions = StringUtils::Split(stringValue, ",");

    if (root->QueryStringAttribute("transcoderVideoBitrates", &stringValue) == TIXML_SUCCESS)
      m_transcoderBitrates = StringUtils::Split(stringValue, ",");

    if (root->QueryStringAttribute("transcoderVideoQualities", &stringValue) == TIXML_SUCCESS)
      m_transcoderQualities = StringUtils::Split(stringValue, ",");

    CLog::Log(LOGDEBUG, "CPlexServer::CollectDataFromRoot knowledge complete: %s", toString().c_str());
  }
  else
  {
    CLog::Log(LOGWARNING, "CPlexServer::CollectDataFromRoot parser fail!");
    return false;
  }

  return true;
}

bool
CPlexServer::HasActiveLocalConnection() const
{
  CSingleLock lk(m_serverLock);
  return (m_activeConnection != NULL && m_activeConnection->IsLocal());
}

void
CPlexServer::MarkAsRefreshing()
{
  CSingleLock lk(m_serverLock);
  BOOST_FOREACH(CPlexConnectionPtr conn, m_connections)
    conn->SetRefreshed(false);
}

bool
CPlexServer::MarkUpdateFinished(int connType)
{
  vector<CPlexConnectionPtr> connsToRemove;
  
  CSingleLock lk(m_serverLock);

  BOOST_FOREACH(CPlexConnectionPtr conn, m_connections)
  {
    if (conn->GetRefreshed() == false)
      conn->m_type &= ~connType;

    if (conn->m_type == 0)
      connsToRemove.push_back(conn);
  }

  BOOST_FOREACH(CPlexConnectionPtr conn, connsToRemove)
  {
    CLog::Log(LOGDEBUG, "CPlexServer::MarkUpdateFinished Removing connection for %s after update finished for type %d: %s", m_name.c_str(), connType, conn->toString().c_str());
    vector<CPlexConnectionPtr>::iterator it = find(m_connections.begin(), m_connections.end(), conn);
    m_connections.erase(it);

    if (m_activeConnection == conn)
      m_activeConnection.reset();

    if (m_bestConnection == conn)
      m_bestConnection.reset();
  }

  return m_connections.size() > 0;
}

bool
ConnectionSortFunction(CPlexConnectionPtr c1, CPlexConnectionPtr c2)
{
  if (c1->IsLocal() && !c2->IsLocal()) return false;
  if (!c1->IsLocal() && c2->IsLocal()) return true;
  return c1->GetAddress().Get() < c2->GetAddress().Get();
}

bool
CPlexServer::UpdateReachability()
{
  if (m_connections.size() == 0)
    return false;

  m_connTestTimer.restart();
  CLog::Log(LOGDEBUG, "CPlexServer::UpdateReachability Updating reachability for %s with %ld connections.", m_name.c_str(), m_connections.size());

  m_bestConnection.reset();
  m_testEvent.Reset();
  m_connectionsLeft = m_connections.size();
  m_complete = false;

  vector<CPlexConnectionPtr> sortedConnections = m_connections;
  sort(sortedConnections.begin(), sortedConnections.end(), ConnectionSortFunction);

  BOOST_FOREACH(CPlexConnectionPtr conn, sortedConnections)
  {
    CLog::Log(LOGDEBUG, "CPlexServer::UpdateReachability testing connection %s", conn->toString().c_str());

    new CPlexServerConnTestThread(conn, GetShared());
  }

  m_testEvent.WaitMSec(30000);

  CSingleLock tlk(m_testingLock);
  m_complete = true;
  m_activeConnection = m_bestConnection;

  CLog::Log(LOGDEBUG, "CPlexServer::UpdateReachability Connectivity test to %s completed in %.1f Seconds -> %s",
            m_name.c_str(), m_connTestTimer.elapsed(), m_activeConnection ? m_activeConnection->toString().c_str() : "FAILED");

  return (bool)m_bestConnection;
}

void CPlexServer::OnConnectionTest(CPlexConnectionPtr conn, bool success)
{
  CSingleLock lk(m_testingLock);
  if (success)
  {
    if (!m_bestConnection)
    {
      m_bestConnection = conn;
    }
    else if (conn->IsLocal() && !m_bestConnection->IsLocal())
      m_activeConnection = conn;
  }

  if (--m_connectionsLeft == 0 && m_complete == false)
  {
    m_testEvent.Set();
  }
}

void
CPlexServer::Merge(CPlexServerPtr otherServer)
{
  CSingleLock lk(m_serverLock);

  m_name = otherServer->m_name;
  m_version = otherServer->m_version;
  m_owned = otherServer->m_owned;
  m_owner = otherServer->m_owner;

  BOOST_FOREACH(CPlexConnectionPtr conn, otherServer->m_connections)
  {
    vector<CPlexConnectionPtr>::iterator it;
    it = find(m_connections.begin(), m_connections.end(), conn);
    if (it != m_connections.end())
      (*it)->Merge(conn);
    else
      AddConnection(conn);
  }
}

void
CPlexServer::GetConnections(std::vector<CPlexConnectionPtr> &conns)
{
  conns = m_connections;
}

int
CPlexServer::GetNumConnections() const
{
  return m_connections.size();
}

CPlexConnectionPtr
CPlexServer::GetActiveConnection() const
{
  return m_activeConnection;
}

CURL
CPlexServer::GetActiveConnectionURL() const
{
  return m_activeConnection->GetAddress();
}

CURL
CPlexServer::BuildPlexURL(const CStdString& path) const
{
  CURL url;
  url.SetProtocol("plexserver");
  url.SetHostName(m_uuid);
  url.SetFileName(path);
  return url.Get();
}

CURL
CPlexServer::BuildURL(const CStdString &path, const CStdString &options) const
{
  CPlexConnectionPtr connection = m_activeConnection;

  if (!connection && m_connections.size() > 0)
    /* no active connection, just take the first one at random */
    connection = m_connections[0];
  else if (!connection && m_connections.size() == 0)
    /* no connections are no gooooood */
    return CURL();

  CURL url = connection->BuildURL(path);

  if (!options.empty())
    url.SetOptions(options);

  if (!url.HasOption(connection->GetAccessTokenParameter()))
  {
    /* See if we can find a token in our other connections */
    CStdString token;
    BOOST_FOREACH(CPlexConnectionPtr conn, m_connections)
    {
      if (!conn->GetAccessToken().empty())
      {
        token = conn->GetAccessToken();
        break;
      }
    }

    if (!token.empty())
      url.SetOption(connection->GetAccessTokenParameter(), token);
  }
  return url;
}

bool CPlexServer::HasAuthToken() const
{
  CStdString token;
  BOOST_FOREACH(CPlexConnectionPtr conn, m_connections)
  {
    if (!conn->GetAccessToken().empty())
      return true;
  }
  return false;
}

void
CPlexServer::AddConnection(CPlexConnectionPtr connection)
{
  if (m_activeConnection && m_activeConnection->IsLocal() &&
      (!connection->GetAccessToken().empty() && !HasAuthToken()))
    m_activeConnection.reset();
  
  m_connections.push_back(connection);
}

CStdString
CPlexServer::toString() const
{
  CStdString ret;
  ret.Format("%s version: %s owned: %s videoTranscode: %s audioTranscode: %s deletion: %s class: %s",
             m_name,
             m_version,
             m_owned ? "YES" : "NO",
             m_supportsVideoTranscoding ? "YES" : "NO",
             m_supportsAudioTranscoding ? "YES" : "NO",
             m_supportsDeletion ? "YES" : "NO",
             m_serverClass);

  return ret;
}

void CPlexServer::save(TiXmlNode *parent)
{
  CSingleLock lk(m_serverLock);

  TiXmlElement serverEl("server");

  serverEl.SetAttribute("name", m_name.c_str());
  serverEl.SetAttribute("version", m_version.c_str());
  serverEl.SetAttribute("uuid", m_uuid.c_str());
  serverEl.SetAttribute("owner", m_owner.c_str());

  serverEl.SetAttribute("owned", m_owned);
  serverEl.SetAttribute("serverClass", m_serverClass.c_str());
  serverEl.SetAttribute("supportsDeletion", m_supportsDeletion);
  serverEl.SetAttribute("supportsVideoTranscoding", m_supportsVideoTranscoding);
  serverEl.SetAttribute("supportsAudioTranscoding", m_supportsAudioTranscoding);

  serverEl.SetAttribute("transcoderQualities", StringUtils::Join(m_transcoderQualities, ",").c_str());
  serverEl.SetAttribute("transcoderBitrates", StringUtils::Join(m_transcoderBitrates, ",").c_str());
  serverEl.SetAttribute("transcoderResolutions", StringUtils::Join(m_transcoderResolutions, ",").c_str());

  TiXmlNode *server = parent->InsertEndChild(serverEl);

  BOOST_FOREACH(CPlexConnectionPtr conn, m_connections)
    conn->save(server);
}

CPlexServerPtr CPlexServer::load(TiXmlElement *element)
{
  std::string name, uuid;
  bool owned;

  CPlexServerPtr fail;

  if (element->QueryStringAttribute("name", &name) != TIXML_SUCCESS)
    return fail;

  if (element->QueryStringAttribute("uuid", &uuid) != TIXML_SUCCESS)
    return fail;

  if (element->QueryBoolAttribute("owned", &owned) != TIXML_SUCCESS)
    return fail;

  CPlexServerPtr server = CPlexServerPtr(new CPlexServer(uuid, name, owned));

  element->QueryStringAttribute("version", &server->m_version);
  element->QueryStringAttribute("owner", &server->m_owner);
  element->QueryStringAttribute("serverClass", &server->m_serverClass);
  element->QueryBoolAttribute("supportsDeletion", &server->m_supportsDeletion);
  element->QueryBoolAttribute("supportsVideoTranscoding", &server->m_supportsDeletion);
  element->QueryBoolAttribute("supportsAudioTranscoding", &server->m_supportsDeletion);

  std::string lists;
  if (element->QueryStringAttribute("transcoderQualities", &lists) == TIXML_SUCCESS)
    server->m_transcoderQualities = StringUtils::Split(lists, ",");
  if (element->QueryStringAttribute("transcoderBitrates", &lists) == TIXML_SUCCESS)
    server->m_transcoderBitrates = StringUtils::Split(lists, ",");
  if (element->QueryStringAttribute("transcoderResolutions", &lists) == TIXML_SUCCESS)
    server->m_transcoderResolutions = StringUtils::Split(lists, ",");

  TiXmlElement* conn = element->FirstChildElement();
  while (conn)
  {
    CPlexConnectionPtr cptr = CPlexConnection::load(conn);
    if (cptr)
      server->m_connections.push_back(cptr);
    conn = conn->NextSiblingElement();
  }

  return server;
}