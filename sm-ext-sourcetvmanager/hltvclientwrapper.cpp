#include "hltvclientwrapper.h"

HLTVClientWrapper::HLTVClientWrapper()
{
	m_Client = nullptr;
}

void HLTVClientWrapper::Initialize(const char *ip, const char *password, IClient *client)
{
	m_Ip = ip;
	m_Password = password;
	m_Client = client;
}

const char *HLTVClientWrapper::Name()
{
	return m_Client->GetClientName();
}

const char *HLTVClientWrapper::Ip()
{
	return m_Ip.c_str();
}

const char *HLTVClientWrapper::Password()
{
	return m_Password.c_str();
}

unsigned int HLTVClientWrapper::GetLanguageId()
{
	// TODO expose natives to override spectator client language and remove native ifdefs
	unsigned int langid = translator->GetServerLanguage();
	translator->GetLanguageByName(m_Client->GetUserSetting("cl_language"), &langid);
	return langid;
}

bool HLTVClientWrapper::IsConnected()
{
	return m_Client && m_Client->IsConnected();
}

IClient *HLTVClientWrapper::BaseClient()
{
	return m_Client;
}

void HLTVClientWrapper::Kick(const char *reason)
{
	// Go this route due to different IClient::Disconnect signatures in games..
	m_Client->GetServer()->DisconnectClient(m_Client, reason);
}
