/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Sample Extension
 * Copyright (C) 2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#include "extension.h"
#include "extensionHelper.h"
#include "CDetour/detours.h"
#include "steam/steam_gameserver.h"
#include "sm_namehashset.h"
#include <sourcehook.h>
#include <bitbuf.h>
#include <netadr.h>
#include <ISDKTools.h>
#include <iserver.h>
#include <iclient.h>
#include <iplayerinfo.h>
#include <ihltvdirector.h>
#include <ihltv.h>
#include <inetchannelinfo.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sstream>

size_t
strlcpy(char *dst, const char *src, size_t dsize)
{
	const char *osrc = src;
	size_t nleft = dsize;

	/* Copy as many bytes as will fit. */
	if (nleft != 0) {
		while (--nleft != 0) {
			if ((*dst++ = *src++) == '\0')
				break;
		}
	}

	/* Not enough room in dst, add NUL and traverse rest of src. */
	if (nleft == 0) {
		if (dsize != 0)
			*dst = '\0';		/* NUL-terminate dst */
		while (*src++)
			;
	}

	return(src - osrc - 1);	/* count does not include NUL */
}

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

Connect g_Connect;		/**< Global singleton for extension's main interface */
ConnectEvents g_ConnectEvents;
ConnectTimer g_ConnectTimer;

SMEXT_LINK(&g_Connect);

ConVar *g_ConnectVersion = CreateConVar("connect_version", SMEXT_CONF_VERSION, FCVAR_REPLICATED|FCVAR_NOTIFY, SMEXT_CONF_DESCRIPTION " Version");
ConVar *g_SvNoSteam = CreateConVar("sv_nosteam", "1", FCVAR_NOTIFY, "Disable steam validation and force steam authentication.");
ConVar *g_SvForceSteam = CreateConVar("sv_forcesteam", "0", FCVAR_NOTIFY, "Force steam authentication.");
ConVar *g_SvLogging = CreateConVar("sv_connect_logging", "0", FCVAR_NOTIFY, "Log connection checks");
ConVar *g_SvGameDesc = CreateConVar("sv_gamedesc_override", "default", FCVAR_NOTIFY, "Overwrite the game description. Default = 'default'");
ConVar *g_SvMapName = CreateConVar("sv_mapname_override", "default", FCVAR_NOTIFY, "Overwrite the map name. Default = 'default'");
ConVar *g_SvCountBotsInfo = CreateConVar("sv_count_bots_info", "1", FCVAR_NOTIFY, "Display bots as players in the a2s_info server query. Enable = '1', Disable = '0'");
ConVar *g_SvCountBotsPlayer = CreateConVar("sv_count_bots_player", "0", FCVAR_NOTIFY, "Display bots as players in the a2s_player server query. Enable = '1', Disable = '0'");
ConVar *g_SvAuthSessionResponseLegal = CreateConVar("sv_auth_session_response_legal", "0,1,2,3,4,5,7,9", FCVAR_NOTIFY, "List of EAuthSessionResponse that are considered as Steam legal (Defined in steam_api_interop.cs).");
ConVar *g_pSvVisibleMaxPlayers;
ConVar *g_pSvTags;

IGameConfig *g_pGameConf = NULL;
IForward *g_pConnectForward = NULL;
IForward *g_pOnValidateAuthTicketResponse = NULL;
IGameEventManager2 *g_pGameEvents = NULL;
ITimer *g_pConnectTimer = NULL;
ISDKTools *g_pSDKTools = NULL;
IServer *iserver = NULL;
CGlobalVars *gpGlobals = NULL;
IHLTVDirector *hltvdirector = NULL;
IHLTVServer *hltv = NULL;
double *net_time = NULL;

uint8_t g_UserIDtoClientMap[USHRT_MAX + 1];
char g_ClientSteamIDMap[SM_MAXPLAYERS + 1][32];

typedef struct netpacket_s
{
	netadr_t		from;		// sender IP
	int				source;		// received source
	double			received;	// received time
	unsigned char	*data;		// pointer to raw packet data
	bf_read			message;	// easy bitbuf data access
	int				size;		// size in bytes
	int				wiresize;   // size in bytes before decompression
	bool			stream;		// was send as stream
	struct netpacket_s *pNext;	// for internal use, should be NULL in public
} netpacket_t;

typedef struct
{
	int			nPort;		// UDP/TCP use same port number
	bool		bListening;	// true if TCP port is listening
	int			hUDP;		// handle to UDP socket from socket()
	int			hTCP;		// handle to TCP socket from socket()
} netsocket_t;

CUtlVector<netsocket_t> *net_sockets;
int g_ServerUDPSocket = 0;

SH_DECL_MANUALHOOK1(ProcessConnectionlessPacket, 0, 0, 0, bool, netpacket_t *); // virtual bool IServer::ProcessConnectionlessPacket( netpacket_t *packet ) = 0;

void *s_queryRateChecker = NULL;
bool (*CIPRateLimit__CheckIP)(void *pThis, netadr_t adr);
bool (*CBaseServer__ValidChallenge)(void *pThis, netadr_t adr, int challengeNr);

typedef struct CPlayer
{
	bool active;
	bool fake;
	int userid;
	IClient *pClient;
	char name[MAX_PLAYER_NAME_LENGTH];
	unsigned nameLen;
	int32_t score;
	double time;
} CPlayer;

typedef struct CInfo
{
	uint8_t nProtocol = 17; // Protocol | byte | Protocol version used by the server.
	char aHostName[255]; // Name | string | Name of the server.
	uint8_t aHostNameLen;
	char aMapName[255]; // Map | string | Map the server has currently loaded.
	uint8_t aMapNameLen;
	char aGameDir[255]; // Folder | string | Name of the folder containing the game files.
	uint8_t aGameDirLen;
	char aGameDescription[255]; // Game | string | Full name of the game.
	uint8_t aGameDescriptionLen;
	uint16_t iSteamAppID; // ID | short | Steam Application ID of game.
	uint8_t nNumClients = 0; // Players | byte | Number of players on the server.
	uint8_t nMaxClients; // Max. Players | byte | Maximum number of players the server reports it can hold.
	uint8_t nFakeClients = 0; // Bots | byte | Number of bots on the server.
	uint8_t nServerType = 'd'; // Server type | byte | Indicates the type of server: 'd' for a dedicated server, 'l' for a non-dedicated server, 'p' for a SourceTV relay (proxy)
	uint8_t nEnvironment = 'l'; // Environment | byte | Indicates the operating system of the server: 'l' for Linux, 'w' for Windows, 'm' or 'o' for Mac (the code changed after L4D1)
	uint8_t nPassword; // Visibility | byte | Indicates whether the server requires a password: 0 for public, 1 for private
	uint8_t bIsSecure; // VAC | byte | Specifies whether the server uses VAC: 0 for unsecured, 1 for secured
	char aVersion[40]; // Version | string | Version of the game installed on the server.
	uint8_t aVersionLen;
	uint8_t nNewFlags = 0; // Extra Data Flag (EDF) | byte | If present, this specifies which additional data fields will be included.
	uint16_t iUDPPort; // EDF & 0x80 -> Port | short | The server's game port number.
	uint64_t iSteamID; // EDF & 0x10 -> SteamID | long long | Server's SteamID.
	uint16_t iHLTVUDPPort; // EDF & 0x40 -> Port | short | Spectator port number for SourceTV.
	char aHLTVName[255]; // EDF & 0x40 -> Name | string | Name of the spectator server for SourceTV.
	uint8_t aHLTVNameLen;
	char aKeywords[255]; // EDF & 0x20 -> Keywords | string | Tags that describe the game according to the server (for future use.) (sv_tags)
	uint8_t aKeywordsLen;
	uint64_t iGameID; // EDF & 0x01 -> GameID | long long | The server's 64-bit GameID. If this is present, a more accurate AppID is present in the low 24 bits. The earlier AppID could have been truncated as it was forced into 16-bit storage.
} CInfo;

typedef struct CQueryCache
{
	CPlayer players[SM_MAXPLAYERS + 1];
	CInfo info;

	uint8_t info_cache[sizeof(CInfo)] = {0xFF, 0xFF, 0xFF, 0xFF, 'I'};
	uint16_t info_cache_len;
} CQueryCache;

CQueryCache g_QueryCache;

class CBaseClient;
class CBaseServer;

typedef enum EConnect
{
	k_OnClientPreConnectEx_Reject = 0,
	k_OnClientPreConnectEx_Accept = 1,
	k_OnClientPreConnectEx_Async = -1
} EConnect;

typedef enum EAuthProtocol
{
	k_EAuthProtocolWONCertificate = 1,
	k_EAuthProtocolHashedCDKey = 2,
	k_EAuthProtocolSteam = 3,
} EAuthProtocol;

const char *CSteamID::Render() const
{
	static char szSteamID[64];
	V_snprintf(szSteamID, sizeof(szSteamID), "STEAM_0:%u:%u", (m_steamid.m_comp.m_unAccountID % 2) ? 1 : 0, (int32)m_steamid.m_comp.m_unAccountID/2);
	return szSteamID;
}

class CSteam3Server
{
public:
	void *m_pSteamClient;
	ISteamGameServer *m_pSteamGameServer;
	void *m_pSteamGameServerUtils;
	void *m_pSteamGameServerNetworking;
	void *m_pSteamGameServerStats;
	void *m_pSteamHTTP;
	void *m_pSteamInventory;
	void *m_pSteamUGC;
	void *m_pSteamApps;
} *g_pSteam3Server;

CBaseServer *g_pBaseServer = NULL;

typedef CSteam3Server *(*Steam3ServerFunc)();

#ifndef WIN32
typedef void (*RejectConnectionFunc)(CBaseServer *, const netadr_t &address, int iClientChallenge, const char *pchReason);
#else
typedef void (__fastcall *RejectConnectionFunc)(CBaseServer *, void *, const netadr_t &address, int iClientChallenge, const char *pchReason);
#endif

#ifndef WIN32
typedef void (*SetSteamIDFunc)(CBaseClient *, const CSteamID &steamID);
#else
typedef void (__fastcall *SetSteamIDFunc)(CBaseClient *, void *, const CSteamID &steamID);
#endif

Steam3ServerFunc g_pSteam3ServerFunc = NULL;
RejectConnectionFunc g_pRejectConnectionFunc = NULL;
SetSteamIDFunc g_pSetSteamIDFunc = NULL;

CSteam3Server *Steam3Server()
{
	if(!g_pSteam3ServerFunc)
		return NULL;

	return g_pSteam3ServerFunc();
}

void RejectConnection(const netadr_t &address, int iClientChallenge, const char *pchReason)
{
	if(!g_pRejectConnectionFunc || !g_pBaseServer)
		return;

#ifndef WIN32
	g_pRejectConnectionFunc(g_pBaseServer, address, iClientChallenge, pchReason);
#else
	g_pRejectConnectionFunc(g_pBaseServer, NULL, address, iClientChallenge, pchReason);
#endif
}

void SetSteamID(CBaseClient *pClient, const CSteamID &steamID)
{
	if(!pClient || !g_pSetSteamIDFunc)
		return;

#ifndef WIN32
	g_pSetSteamIDFunc(pClient, steamID);
#else
	g_pSetSteamIDFunc(pClient, NULL, steamID);
#endif
}

EBeginAuthSessionResult BeginAuthSession(const void *pAuthTicket, int cbAuthTicket, CSteamID steamID)
{
	if(!g_pSteam3Server || !g_pSteam3Server->m_pSteamGameServer)
		return k_EBeginAuthSessionResultOK;

	return g_pSteam3Server->m_pSteamGameServer->BeginAuthSession(pAuthTicket, cbAuthTicket, steamID);
}

void EndAuthSession(CSteamID steamID)
{
	if(!g_pSteam3Server || !g_pSteam3Server->m_pSteamGameServer)
		return;

	g_pSteam3Server->m_pSteamGameServer->EndAuthSession(steamID);
}

bool BLoggedOn()
{
	if(!g_pSteam3Server || !g_pSteam3Server->m_pSteamGameServer)
		return false;

	return g_pSteam3Server->m_pSteamGameServer->BLoggedOn();
}

bool BSecure()
{
	if(!g_pSteam3Server || !g_pSteam3Server->m_pSteamGameServer)
		return false;

	return g_pSteam3Server->m_pSteamGameServer->BSecure();
}

CDetour *g_Detour_CBaseServer__ConnectClient = NULL;
CDetour *g_Detour_CBaseServer__RejectConnection = NULL;
CDetour *g_Detour_CBaseServer__CheckChallengeType = NULL;
CDetour *g_Detour_CBaseServer__InactivateClients = NULL;
CDetour *g_Detour_CSteam3Server__OnValidateAuthTicketResponse = NULL;

class ConnectClientStorage
{
public:
	void* pThis;

	netadr_t address;
	int nProtocol;
	int iChallenge;
	int iClientChallenge;
	int nAuthProtocol;
	char pchName[256];
	char pchPassword[256];
	char pCookie[256];
	int cbCookie;
	IClient *pClient;

	uint64 ullSteamID;
	ValidateAuthTicketResponse_t ValidateAuthTicketResponse;
	EAuthSessionResponse eAuthSessionResponse;
	bool GotValidateAuthTicketResponse;
	bool SteamLegal;
	bool SteamAuthFailed;

	ConnectClientStorage()
	{
		this->GotValidateAuthTicketResponse = false;
		this->SteamLegal = false;
		this->SteamAuthFailed = false;
		this->eAuthSessionResponse = k_EAuthSessionResponseAuthTicketInvalid;
	}
	ConnectClientStorage(netadr_t address, int nProtocol, int iChallenge, int iClientChallenge, int nAuthProtocol, const char *pchName, const char *pchPassword, const char *pCookie, int cbCookie)
	{
		this->address = address;
		this->nProtocol = nProtocol;
		this->iChallenge = iChallenge;
		this->iClientChallenge = iClientChallenge;
		this->nAuthProtocol = nAuthProtocol;
		strlcpy(this->pchName, pchName, sizeof(this->pchName));
		strlcpy(this->pchPassword, pchPassword, sizeof(this->pchPassword));
		strlcpy(this->pCookie, pCookie, sizeof(this->pCookie));
		this->cbCookie = cbCookie;
		this->pClient = NULL;
		this->GotValidateAuthTicketResponse = false;
		this->SteamLegal = false;
		this->SteamAuthFailed = false;
		this->eAuthSessionResponse = k_EAuthSessionResponseAuthTicketInvalid;
	}
};
StringHashMap<ConnectClientStorage> g_ConnectClientStorage;

bool g_bEndAuthSessionOnRejectConnection = false;
CSteamID g_lastClientSteamID;
bool g_bSuppressCheckChallengeType = false;

bool IsAuthSessionResponseSteamLegal(EAuthSessionResponse eAuthSessionResponse)
{
	std::stringstream ss(g_SvAuthSessionResponseLegal->GetString());
	int legalAuthSessionResponse[10];
	char ch;
	int n;
	int size = 0;

	while(ss >> n)
	{
		if(ss >> ch)
			legalAuthSessionResponse[size] = n;
		else
			legalAuthSessionResponse[size] = n;
		size++;
	}

	for (int y = 0; y < size; y++)
	{
	    if (eAuthSessionResponse == legalAuthSessionResponse[y])
	        return true;
	}
	return false;
}

DETOUR_DECL_MEMBER1(CSteam3Server__OnValidateAuthTicketResponse, int, ValidateAuthTicketResponse_t *, pResponse)
{
	char aSteamID[32];
	strlcpy(aSteamID, pResponse->m_SteamID.Render(), sizeof(aSteamID));

	EAuthSessionResponse eAuthSessionResponse = pResponse->m_eAuthSessionResponse;
	bool SteamLegal = IsAuthSessionResponseSteamLegal(pResponse->m_eAuthSessionResponse);
	bool force = g_SvNoSteam->GetInt() || g_SvForceSteam->GetInt() || !BLoggedOn();

	if (g_SvLogging->GetInt())
		g_pSM->LogMessage(myself, "%s SteamLegal: %d (%d)", aSteamID, SteamLegal, pResponse->m_eAuthSessionResponse);

	if(!SteamLegal && force)
		pResponse->m_eAuthSessionResponse = k_EAuthSessionResponseOK;

	ConnectClientStorage Storage;
	if(g_ConnectClientStorage.retrieve(aSteamID, &Storage))
	{
		if(!Storage.GotValidateAuthTicketResponse)
		{
			Storage.GotValidateAuthTicketResponse = true;
			Storage.ValidateAuthTicketResponse = *pResponse;
			Storage.SteamLegal = SteamLegal;
			Storage.eAuthSessionResponse = eAuthSessionResponse;
			g_ConnectClientStorage.replace(aSteamID, Storage);
		}
	}

	g_pOnValidateAuthTicketResponse->PushCell(Storage.eAuthSessionResponse);
	g_pOnValidateAuthTicketResponse->PushCell(Storage.GotValidateAuthTicketResponse);
	g_pOnValidateAuthTicketResponse->PushCell(Storage.SteamLegal);
	g_pOnValidateAuthTicketResponse->PushStringEx(aSteamID, sizeof(aSteamID), SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
	g_pOnValidateAuthTicketResponse->Execute();

	return DETOUR_MEMBER_CALL(CSteam3Server__OnValidateAuthTicketResponse)(pResponse);
}

DETOUR_DECL_MEMBER9(CBaseServer__ConnectClient, IClient *, netadr_t &, address, int, nProtocol, int, iChallenge, int, iClientChallenge, int, nAuthProtocol, const char *, pchName, const char *, pchPassword, const char *, pCookie, int, cbCookie)
{
	if(nAuthProtocol != k_EAuthProtocolSteam)
	{
		// This is likely a SourceTV client, we don't want to interfere here.
		return DETOUR_MEMBER_CALL(CBaseServer__ConnectClient)(address, nProtocol, iChallenge, iClientChallenge, nAuthProtocol, pchName, pchPassword, pCookie, cbCookie);
	}

	g_pBaseServer = (CBaseServer *)this;

	if(pCookie == NULL || (size_t)cbCookie < sizeof(uint64))
	{
		RejectConnection(address, iClientChallenge, "#GameUI_ServerRejectInvalidSteamCertLen");
		return NULL;
	}

	char ipString[32];
	V_snprintf(ipString, sizeof(ipString), "%u.%u.%u.%u", address.ip[0], address.ip[1], address.ip[2], address.ip[3]);

	char passwordBuffer[255];
	strlcpy(passwordBuffer, pchPassword, sizeof(passwordBuffer));
	uint64 ullSteamID = *(uint64 *)pCookie;

	void *pvTicket = (void *)((intptr_t)pCookie + sizeof(uint64));
	int cbTicket = cbCookie - sizeof(uint64);

	g_bEndAuthSessionOnRejectConnection = true;
	g_lastClientSteamID = CSteamID(ullSteamID);

	char aSteamID[32];
	strlcpy(aSteamID, g_lastClientSteamID.Render(), sizeof(aSteamID));

	// If client is in async state remove the old object and fake an async retVal
	// This can happen if the async ClientPreConnectEx takes too long to be called
	// and the client auto-retries.
	bool AsyncWaiting = false;
	bool ExistingSteamid = false;
	ConnectClientStorage Storage(address, nProtocol, iChallenge, iClientChallenge, nAuthProtocol, pchName, pchPassword, pCookie, cbCookie);
	if(g_ConnectClientStorage.retrieve(aSteamID, &Storage))
	{
		ExistingSteamid = true;
		g_ConnectClientStorage.remove(aSteamID);
		EndAuthSession(g_lastClientSteamID);

		// Only wait for async on auto-retry, manual retries should go through the full chain
		// Don't want to leave the client waiting forever if something breaks in the async forward
		if(Storage.iClientChallenge == iClientChallenge)
			AsyncWaiting = true;
	}

	bool NoSteam = g_SvNoSteam->GetInt() || !BLoggedOn();
	bool SteamAuthFailed = false;
	EBeginAuthSessionResult result = BeginAuthSession(pvTicket, cbTicket, g_lastClientSteamID);
	if(result != k_EBeginAuthSessionResultOK)
	{
		if(!NoSteam)
		{
			RejectConnection(address, iClientChallenge, "#GameUI_ServerRejectSteam");
			return NULL;
		}
		Storage.SteamAuthFailed = SteamAuthFailed = true;
	}

	if(ExistingSteamid && !AsyncWaiting)
	{
		// Another player trying to spoof a Steam ID or game crashed?
		if(memcmp(address.ip, Storage.address.ip, sizeof(address.ip)) != 0)
		{
			// Reject NoSteam players
			if(SteamAuthFailed)
			{
				RejectConnection(address, iClientChallenge, "Steam ID already in use.");
				return NULL;
			}

			// Kick existing player
			if(Storage.pClient)
			{
				Storage.pClient->Disconnect("Same Steam ID connected.");
			}
			else
			{
				RejectConnection(address, iClientChallenge, "Please try again later.");
				return NULL;
			}
		}
	}

	char rejectReason[255];
	cell_t retVal = 1;

	if(AsyncWaiting)
		retVal = -1; // Fake async return code when waiting for async call
	else
	{
		g_pConnectForward->PushString(pchName);
		g_pConnectForward->PushStringEx(passwordBuffer, sizeof(passwordBuffer), SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
		g_pConnectForward->PushString(ipString);
		g_pConnectForward->PushString(aSteamID);
		g_pConnectForward->PushStringEx(rejectReason, sizeof(rejectReason), SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
		g_pConnectForward->Execute(&retVal);
		pchPassword = passwordBuffer;
	}

	if (g_SvLogging->GetInt())
		g_pSM->LogMessage(myself, "%s SteamAuthFailed: %d (%d) | retVal = %d", aSteamID, SteamAuthFailed, result, retVal);

	if(retVal == k_OnClientPreConnectEx_Reject)
	{
		g_ConnectClientStorage.remove(aSteamID);
		RejectConnection(address, iClientChallenge, rejectReason);
		return NULL;
	}

	Storage.pThis = this;
	Storage.ullSteamID = ullSteamID;
	Storage.SteamAuthFailed = SteamAuthFailed;

	if(!g_ConnectClientStorage.replace(aSteamID, Storage))
	{
		RejectConnection(address, iClientChallenge, "Internal error.");
		return NULL;
	}

	if(retVal == k_OnClientPreConnectEx_Async)
	{
		return NULL;
	}

	// k_OnClientPreConnectEx_Accept
	g_bSuppressCheckChallengeType = true;
	IClient *pClient = DETOUR_MEMBER_CALL(CBaseServer__ConnectClient)(address, nProtocol, iChallenge, iClientChallenge, nAuthProtocol, pchName, pchPassword, pCookie, cbCookie);

	if(pClient)
		strlcpy(g_ClientSteamIDMap[pClient->GetPlayerSlot() + 1], aSteamID, sizeof(*g_ClientSteamIDMap));

	Storage.pClient = pClient;
	g_ConnectClientStorage.replace(aSteamID, Storage);

	if(pClient && SteamAuthFailed)
	{
		ValidateAuthTicketResponse_t Response;
		Response.m_SteamID = g_lastClientSteamID;
		Response.m_eAuthSessionResponse = k_EAuthSessionResponseAuthTicketInvalid;
		Response.m_OwnerSteamID = Response.m_SteamID;
		DETOUR_MEMBER_MCALL_CALLBACK(CSteam3Server__OnValidateAuthTicketResponse, g_pSteam3Server)(&Response);
	}

	return pClient;
}

DETOUR_DECL_MEMBER3(CBaseServer__RejectConnection, void, netadr_t &, address, int, iClientChallenge, const char *, pchReason)
{
	if(g_bEndAuthSessionOnRejectConnection)
	{
		EndAuthSession(g_lastClientSteamID);
		g_bEndAuthSessionOnRejectConnection = false;
	}

	return DETOUR_MEMBER_CALL(CBaseServer__RejectConnection)(address, iClientChallenge, pchReason);
}

DETOUR_DECL_MEMBER7(CBaseServer__CheckChallengeType, bool, CBaseClient *, pClient, int, nUserID, netadr_t &, address, int, nAuthProtocol, const char *, pCookie, int, cbCookie, int, iClientChallenge)
{
	if(g_bSuppressCheckChallengeType)
	{
		g_bEndAuthSessionOnRejectConnection = false;

		SetSteamID(pClient, g_lastClientSteamID);

		g_bSuppressCheckChallengeType = false;
		return true;
	}

	return DETOUR_MEMBER_CALL(CBaseServer__CheckChallengeType)(pClient, nUserID, address, nAuthProtocol, pCookie, cbCookie, iClientChallenge);
}

DETOUR_DECL_MEMBER0(CBaseServer__InactivateClients, void)
{
	for(int slot = 0; slot < iserver->GetClientCount(); slot++)
	{
		int client = slot + 1;
		IClient *pClient = iserver->GetClient(slot);
		if(!pClient)
			continue;

		// Disconnect all fake clients manually before the engine just nukes them.
		if(pClient->IsFakeClient() && !pClient->IsHLTV())
		{
			pClient->Disconnect("");
		}
	}

	return DETOUR_MEMBER_CALL(CBaseServer__InactivateClients)();
}

void UpdateQueryCache()
{
	CInfo &info = g_QueryCache.info;
	info.aHostNameLen = strlcpy(info.aHostName, iserver->GetName(), sizeof(info.aHostName));

	if(strcmp(g_SvMapName->GetString(), "default") == 0)
		info.aMapNameLen = strlcpy(info.aMapName, iserver->GetMapName(), sizeof(info.aMapName));
	else
		info.aMapNameLen = strlcpy(info.aMapName, g_SvMapName->GetString(), sizeof(info.aMapName));

	if(strcmp(g_SvGameDesc->GetString(), "default") == 0)
		info.aGameDescriptionLen = strlcpy(info.aGameDescription, gamedll->GetGameDescription(), sizeof(info.aGameDescription));
	else
		info.aGameDescriptionLen = strlcpy(info.aGameDescription, g_SvGameDesc->GetString(), sizeof(info.aGameDescription));

	if(g_pSvVisibleMaxPlayers->GetInt() >= 0)
		info.nMaxClients = g_pSvVisibleMaxPlayers->GetInt();
	else
		info.nMaxClients = iserver->GetMaxClients();
	info.nPassword = iserver->GetPassword() ? 1 : 0;
	info.bIsSecure = BSecure();

	if(!(info.nNewFlags & 0x10) && engine->GetGameServerSteamID())
	{
		info.iSteamID = engine->GetGameServerSteamID()->ConvertToUint64();
		info.nNewFlags |= 0x10;
	}

	if(!(info.nNewFlags & 0x40) && hltvdirector->IsActive()) // tv_name can't change anymore
	{
		hltv = hltvdirector->GetHLTVServer();
		if(hltv)
		{
			IServer *ihltvserver = hltv->GetBaseServer();
			if(ihltvserver)
			{
				info.iHLTVUDPPort = ihltvserver->GetUDPPort();
				info.aHLTVNameLen = strlcpy(info.aHLTVName, ihltvserver->GetName(), sizeof(info.aHLTVName));
				info.nNewFlags |= 0x40;
			}
		}
	}

	info.aKeywordsLen = strlcpy(info.aKeywords, g_pSvTags->GetString(), sizeof(info.aKeywords));
	if(info.aKeywordsLen)
		info.nNewFlags |= 0x20;
	else
		info.nNewFlags &= ~0x20;


	uint8_t *info_cache = g_QueryCache.info_cache;
	uint16_t pos = 5; // header: FF FF FF FF I

	info_cache[pos++] = info.nProtocol;

	memcpy(&info_cache[pos], info.aHostName, info.aHostNameLen + 1);
	pos += info.aHostNameLen + 1;

	memcpy(&info_cache[pos], info.aMapName, info.aMapNameLen + 1);
	pos += info.aMapNameLen + 1;

	memcpy(&info_cache[pos], info.aGameDir, info.aGameDirLen + 1);
	pos += info.aGameDirLen + 1;

	memcpy(&info_cache[pos], info.aGameDescription, info.aGameDescriptionLen + 1);
	pos += info.aGameDescriptionLen + 1;

	*(uint16_t *)&info_cache[pos] = info.iSteamAppID;
	pos += 2;

	info_cache[pos++] = info.nNumClients;

	info_cache[pos++] = info.nMaxClients;

	if (g_SvCountBotsInfo->GetInt())
		info_cache[pos++] = 0;
	else
		info_cache[pos++] = info.nFakeClients;

	info_cache[pos++] = info.nServerType;

	info_cache[pos++] = info.nEnvironment;

	info_cache[pos++] = info.nPassword;

	info_cache[pos++] = info.bIsSecure;

	memcpy(&info_cache[pos], info.aVersion, info.aVersionLen + 1);
	pos += info.aVersionLen + 1;

	info_cache[pos++] = info.nNewFlags;

	if(info.nNewFlags & 0x80) {
		*(uint16_t *)&info_cache[pos] = info.iUDPPort;
		pos += 2;
	}

	if(info.nNewFlags & 0x10) {
		*(uint64_t *)&info_cache[pos] = info.iSteamID;
		pos += 8;
	}

	if(info.nNewFlags & 0x40) {
		*(uint16_t *)&info_cache[pos] = info.iHLTVUDPPort;
		pos += 2;

		memcpy(&info_cache[pos], info.aHLTVName, info.aHLTVNameLen + 1);
		pos += info.aHLTVNameLen + 1;
	}

	if(info.nNewFlags & 0x20) {
		memcpy(&info_cache[pos], info.aKeywords, info.aKeywordsLen + 1);
		pos += info.aKeywordsLen + 1;
	}

	if(info.nNewFlags & 0x01) {
		*(uint64_t *)&info_cache[pos] = info.iGameID;
		pos += 8;
	}

	g_QueryCache.info_cache_len = pos;
}

bool Hook_ProcessConnectionlessPacket(netpacket_t * packet)
{
	if(packet->size >= 25 && packet->data[4] == 'T')
	{
		if(!CIPRateLimit__CheckIP(s_queryRateChecker, packet->from))
			RETURN_META_VALUE(MRES_SUPERCEDE, false);

		sockaddr_in to;
		to.sin_family = AF_INET;
		to.sin_port = packet->from.port;
		to.sin_addr.s_addr = *(int32_t *)&packet->from.ip;

		sendto(g_ServerUDPSocket, g_QueryCache.info_cache, g_QueryCache.info_cache_len, 0, (sockaddr *)&to, sizeof(to));

		RETURN_META_VALUE(MRES_SUPERCEDE, true);
	}

	if((packet->size == 5 || packet->size == 9) && packet->data[4] == 'U')
	{
		if(!CIPRateLimit__CheckIP(s_queryRateChecker, packet->from))
			RETURN_META_VALUE(MRES_SUPERCEDE, false);

		sockaddr_in to;
		to.sin_family = AF_INET;
		to.sin_port = packet->from.port;
		to.sin_addr.s_addr = *(int32_t *)&packet->from.ip;

		int32_t challengeNr = -1;
		if(packet->size == 9)
			challengeNr = *(int32_t *)&packet->data[5];

		/* This is a complete nonsense challenge as the client can easily break it.
		 * The point of this challenge is to stop spoofed source DDoS reflection attacks,
		 * so it doesn't really matter if one single server out of thousands doesn't
		 * implement this correctly. If you do happen to use this on thousands of servers
		 * though then please do implement it correctly.
		 */
		int32_t realChallengeNr = *(int32_t *)&packet->from.ip ^ 0x55AADD88;
		if(challengeNr != realChallengeNr)
		{
			uint8_t response[9] = {0xFF, 0xFF, 0xFF, 0xFF, 'A'};
			*(int32_t *)&response[5] = realChallengeNr;
			sendto(g_ServerUDPSocket, response, sizeof(response), 0, (sockaddr *)&to, sizeof(to));
			RETURN_META_VALUE(MRES_SUPERCEDE, true);
		}

		uint8_t response[4+1+1+SM_MAXPLAYERS*(1+MAX_PLAYER_NAME_LENGTH+4+4)] = {0xFF, 0xFF, 0xFF, 0xFF, 'D', 0};
		short pos = 6;
		for(int i = 1; i <= SM_MAXPLAYERS; i++)
		{
			const CPlayer &player = g_QueryCache.players[i];
			if(!player.active || (player.fake && !g_SvCountBotsPlayer->GetInt()))
				continue;

			response[pos++] = response[5]; // Index | byte | Index of player chunk starting from 0.
			response[5]++; // Players | byte | Number of players whose information was gathered.
			memcpy(&response[pos], player.name, player.nameLen + 1); // Name | string | Name of the player.
			pos += player.nameLen + 1;
			*(int32_t *)&response[pos] = player.score; // Score | long | Player's score (usually "frags" or "kills".)
			pos += 4;
			*(float *)&response[pos] = *net_time - player.time; // Duration | float | Time (in seconds) player has been connected to the server.
			pos += 4;
		}

		sendto(g_ServerUDPSocket, response, pos, 0, (sockaddr *)&to, sizeof(to));

		RETURN_META_VALUE(MRES_SUPERCEDE, true);
	}

	RETURN_META_VALUE(MRES_IGNORED, false);
}

bool Connect::SDK_OnLoad(char *error, size_t maxlen, bool late)
{
	char conf_error[255] = "";
	if(!gameconfs->LoadGameConfigFile("connect2.games", &g_pGameConf, conf_error, sizeof(conf_error)))
	{
		if(conf_error[0])
		{
			snprintf(error, maxlen, "Could not read connect2.games.txt: %s\n", conf_error);
		}
		return false;
	}

	if(!g_pGameConf->GetMemSig("CBaseServer__RejectConnection", (void **)(&g_pRejectConnectionFunc)) || !g_pRejectConnectionFunc)
	{
		snprintf(error, maxlen, "Failed to find CBaseServer__RejectConnection function.\n");
		return false;
	}

	if(!g_pGameConf->GetMemSig("CBaseClient__SetSteamID", (void **)(&g_pSetSteamIDFunc)) || !g_pSetSteamIDFunc)
	{
		snprintf(error, maxlen, "Failed to find CBaseClient__SetSteamID function.\n");
		return false;
	}

#ifndef WIN32
	if(!g_pGameConf->GetMemSig("Steam3Server", (void **)(&g_pSteam3ServerFunc)) || !g_pSteam3ServerFunc)
	{
		snprintf(error, maxlen, "Failed to find Steam3Server function.\n");
		return false;
	}
#else
	void *address;
	if(!g_pGameConf->GetMemSig("CBaseServer__CheckMasterServerRequestRestart", &address) || !address)
	{
		snprintf(error, maxlen, "Failed to find CBaseServer__CheckMasterServerRequestRestart function.\n");
		return false;
	}

	//META_CONPRINTF("CheckMasterServerRequestRestart: %p\n", address);
	address = (void *)((intptr_t)address + 1); // Skip CALL opcode
	intptr_t offset = (intptr_t)(*(void **)address); // Get offset

	g_pSteam3ServerFunc = (Steam3ServerFunc)((intptr_t)address + offset + sizeof(intptr_t));
	//META_CONPRINTF("Steam3Server: %p\n", g_pSteam3ServerFunc);
#endif

	g_pSteam3Server = Steam3Server();
	if(!g_pSteam3Server)
	{
		snprintf(error, maxlen, "Unable to get Steam3Server singleton.\n");
		return false;
	}

	/*
	META_CONPRINTF("ISteamGameServer: %p\n", g_pSteam3Server->m_pSteamGameServer);
	META_CONPRINTF("ISteamUtils: %p\n", g_pSteam3Server->m_pSteamGameServerUtils);
	META_CONPRINTF("ISteamMasterServerUpdater: %p\n", g_pSteam3Server->m_pSteamMasterServerUpdater);
	META_CONPRINTF("ISteamNetworking: %p\n", g_pSteam3Server->m_pSteamGameServerNetworking);
	META_CONPRINTF("ISteamGameServerStats: %p\n", g_pSteam3Server->m_pSteamGameServerStats);
	*/

	if(!g_pGameConf->GetMemSig("s_queryRateChecker", &s_queryRateChecker) || !s_queryRateChecker)
	{
		snprintf(error, maxlen, "Failed to find s_queryRateChecker address.\n");
		return false;
	}

	if(!g_pGameConf->GetMemSig("CIPRateLimit__CheckIP", (void **)&CIPRateLimit__CheckIP) || !CIPRateLimit__CheckIP)
	{
		snprintf(error, maxlen, "Failed to find CIPRateLimit::CheckIP address.\n");
		return false;
	}

	if(!g_pGameConf->GetMemSig("CBaseServer__ValidChallenge", (void **)&CBaseServer__ValidChallenge) || !CBaseServer__ValidChallenge)
	{
		snprintf(error, maxlen, "Failed to find CBaseServer::ValidChallenge address.\n");
		return false;
	}

	if(!g_pGameConf->GetMemSig("net_sockets", (void **)&net_sockets) || !net_sockets)
	{
		snprintf(error, maxlen, "Failed to find net_sockets address.\n");
		return false;
	}

	if(!g_pGameConf->GetMemSig("net_time", (void **)&net_time) || !net_time)
	{
		snprintf(error, maxlen, "Failed to find net_time address.\n");
		return false;
	}

	CDetourManager::Init(g_pSM->GetScriptingEngine(), g_pGameConf);

	g_Detour_CBaseServer__ConnectClient = DETOUR_CREATE_MEMBER(CBaseServer__ConnectClient, "CBaseServer__ConnectClient");
	if(!g_Detour_CBaseServer__ConnectClient)
	{
		snprintf(error, maxlen, "Failed to detour CBaseServer__ConnectClient.\n");
		return false;
	}
	g_Detour_CBaseServer__ConnectClient->EnableDetour();

	g_Detour_CBaseServer__RejectConnection = DETOUR_CREATE_MEMBER(CBaseServer__RejectConnection, "CBaseServer__RejectConnection");
	if(!g_Detour_CBaseServer__RejectConnection)
	{
		snprintf(error, maxlen, "Failed to detour CBaseServer__RejectConnection.\n");
		return false;
	}
	g_Detour_CBaseServer__RejectConnection->EnableDetour();

	g_Detour_CBaseServer__CheckChallengeType = DETOUR_CREATE_MEMBER(CBaseServer__CheckChallengeType, "CBaseServer__CheckChallengeType");
	if(!g_Detour_CBaseServer__CheckChallengeType)
	{
		snprintf(error, maxlen, "Failed to detour CBaseServer__CheckChallengeType.\n");
		return false;
	}
	g_Detour_CBaseServer__CheckChallengeType->EnableDetour();

	g_Detour_CBaseServer__InactivateClients = DETOUR_CREATE_MEMBER(CBaseServer__InactivateClients, "CBaseServer__InactivateClients");
	if(!g_Detour_CBaseServer__InactivateClients)
	{
		snprintf(error, maxlen, "Failed to detour CBaseServer__InactivateClients.\n");
		return false;
	}
	g_Detour_CBaseServer__InactivateClients->EnableDetour();

	g_Detour_CSteam3Server__OnValidateAuthTicketResponse = DETOUR_CREATE_MEMBER(CSteam3Server__OnValidateAuthTicketResponse, "CSteam3Server__OnValidateAuthTicketResponse");
	if(!g_Detour_CSteam3Server__OnValidateAuthTicketResponse)
	{
		snprintf(error, maxlen, "Failed to detour CSteam3Server__OnValidateAuthTicketResponse.\n");
		return false;
	}
	g_Detour_CSteam3Server__OnValidateAuthTicketResponse->EnableDetour();

	g_pConnectForward = g_pForwards->CreateForward("OnClientPreConnectEx", ET_LowEvent, 5, NULL, Param_String, Param_String, Param_String, Param_String, Param_String);
	g_pOnValidateAuthTicketResponse = g_pForwards->CreateForward("OnValidateAuthTicketResponse", ET_Ignore, 4, NULL, Param_Cell, Param_Cell, Param_Cell, Param_String);

	g_pGameEvents->AddListener(&g_ConnectEvents, "player_connect", true);
	g_pGameEvents->AddListener(&g_ConnectEvents, "player_disconnect", true);
	g_pGameEvents->AddListener(&g_ConnectEvents, "player_changename", true);

	playerhelpers->AddClientListener(this);

	AutoExecConfig(g_pCVar, true);

	return true;
}

bool Connect::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_ANY(GetServerFactory, gamedll, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameEvents, IGameEventManager2, INTERFACEVERSION_GAMEEVENTSMANAGER2);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetServerFactory, hltvdirector, IHLTVDirector, INTERFACEVERSION_HLTVDIRECTOR);

	gpGlobals = ismm->GetCGlobals();
	ConVar_Register(0, this);

	g_pSvVisibleMaxPlayers = g_pCVar->FindVar("sv_visiblemaxplayers");
	g_pSvTags = g_pCVar->FindVar("sv_tags");

	return true;
}

void Connect::SDK_OnUnload()
{
	if(g_pConnectForward)
		g_pForwards->ReleaseForward(g_pConnectForward);

	if(g_pOnValidateAuthTicketResponse)
		g_pForwards->ReleaseForward(g_pOnValidateAuthTicketResponse);

	if(g_Detour_CBaseServer__ConnectClient)
	{
		g_Detour_CBaseServer__ConnectClient->Destroy();
		g_Detour_CBaseServer__ConnectClient = NULL;
	}
	if(g_Detour_CBaseServer__RejectConnection)
	{
		g_Detour_CBaseServer__RejectConnection->Destroy();
		g_Detour_CBaseServer__RejectConnection = NULL;
	}
	if(g_Detour_CBaseServer__CheckChallengeType)
	{
		g_Detour_CBaseServer__CheckChallengeType->Destroy();
		g_Detour_CBaseServer__CheckChallengeType = NULL;
	}
	if(g_Detour_CBaseServer__InactivateClients)
	{
		g_Detour_CBaseServer__InactivateClients->Destroy();
		g_Detour_CBaseServer__InactivateClients = NULL;
	}
	if(g_Detour_CSteam3Server__OnValidateAuthTicketResponse)
	{
		g_Detour_CSteam3Server__OnValidateAuthTicketResponse->Destroy();
		g_Detour_CSteam3Server__OnValidateAuthTicketResponse = NULL;
	}

	g_pGameEvents->RemoveListener(&g_ConnectEvents);

	playerhelpers->RemoveClientListener(this);

	if(g_pConnectTimer)
		timersys->KillTimer(g_pConnectTimer);

	gameconfs->CloseGameConfigFile(g_pGameConf);
}

bool Connect::RegisterConCommandBase(ConCommandBase *pVar)
{
	/* Always call META_REGCVAR instead of going through the engine. */
	return META_REGCVAR(pVar);
}

cell_t ClientPreConnectEx(IPluginContext *pContext, const cell_t *params)
{
	char *pSteamID;
	pContext->LocalToString(params[1], &pSteamID);

	int retVal = params[2];

	char *rejectReason;
	pContext->LocalToString(params[3], &rejectReason);

	ConnectClientStorage Storage;
	if(!g_ConnectClientStorage.retrieve(pSteamID, &Storage))
		return 1;

	if(retVal == 0)
	{
		RejectConnection(Storage.address, Storage.iClientChallenge, rejectReason);
		return 0;
	}

	g_bSuppressCheckChallengeType = true;
	IClient *pClient = DETOUR_MEMBER_MCALL_ORIGINAL(CBaseServer__ConnectClient, Storage.pThis)(Storage.address, Storage.nProtocol, Storage.iChallenge, Storage.iClientChallenge,
		Storage.nAuthProtocol, Storage.pchName, Storage.pchPassword, Storage.pCookie, Storage.cbCookie);

	if(!pClient)
		return 1;

	bool force = g_SvNoSteam->GetInt() || g_SvForceSteam->GetInt() || !BLoggedOn();


	if(Storage.SteamAuthFailed && force && !Storage.GotValidateAuthTicketResponse)
	{
		if (g_SvLogging->GetInt())
			g_pSM->LogMessage(myself, "%s Force ValidateAuthTicketResponse", pSteamID);

		Storage.ValidateAuthTicketResponse.m_SteamID = CSteamID(Storage.ullSteamID);
		Storage.ValidateAuthTicketResponse.m_eAuthSessionResponse = k_EAuthSessionResponseOK;
		Storage.ValidateAuthTicketResponse.m_OwnerSteamID = Storage.ValidateAuthTicketResponse.m_SteamID;
		Storage.GotValidateAuthTicketResponse = true;
	}

	// Make sure this is always called in order to verify the client on the server
	if(Storage.GotValidateAuthTicketResponse)
	{
		if (g_SvLogging->GetInt())
			g_pSM->LogMessage(myself, "%s Replay ValidateAuthTicketResponse", pSteamID);

		DETOUR_MEMBER_MCALL_ORIGINAL(CSteam3Server__OnValidateAuthTicketResponse, g_pSteam3Server)(&Storage.ValidateAuthTicketResponse);
	}

	return 0;
}

cell_t SteamClientAuthenticated(IPluginContext *pContext, const cell_t *params)
{
	char *pSteamID;
	pContext->LocalToString(params[1], &pSteamID);

	ConnectClientStorage Storage;
	g_ConnectClientStorage.retrieve(pSteamID, &Storage);

	if (g_SvLogging->GetInt())
		g_pSM->LogMessage(myself, "%s SteamClientAuthenticated: %d", pSteamID, Storage.SteamLegal);

	return Storage.SteamLegal;
}

cell_t SteamClientGotValidateAuthTicketResponse(IPluginContext *pContext, const cell_t *params)
{
	char *pSteamID;
	pContext->LocalToString(params[1], &pSteamID);

	ConnectClientStorage Storage;
	g_ConnectClientStorage.retrieve(pSteamID, &Storage);

	if (g_SvLogging->GetInt())
		g_pSM->LogMessage(myself, "%s SteamClientGotValidateAuthTicketResponse: %d", pSteamID, Storage.GotValidateAuthTicketResponse);

	return Storage.GotValidateAuthTicketResponse;
}

const sp_nativeinfo_t MyNatives[] =
{
	{ "ClientPreConnectEx", ClientPreConnectEx },
	{ "SteamClientAuthenticated", SteamClientAuthenticated },
	{ "SteamClientGotValidateAuthTicketResponse", SteamClientGotValidateAuthTicketResponse},
	{ NULL, NULL }
};

void Connect::SDK_OnAllLoaded()
{
	sharesys->AddNatives(myself, MyNatives);

	SM_GET_LATE_IFACE(SDKTOOLS, g_pSDKTools);

	iserver = g_pSDKTools->GetIServer();
	if (!iserver) {
		smutils->LogError(myself, "Failed to get IServer interface from SDKTools!");
		return;
	}

	int offset;
	if (g_pGameConf->GetOffset("CBaseServer__m_Socket", &offset))
	{
		int socknum = *((uint8_t *)iserver + offset);
		g_ServerUDPSocket = (*net_sockets)[socknum].hUDP;
	}
	else
	{
		smutils->LogError(myself, "Failed to find CBaseServer::m_Socket offset.");
		return;
	}

	if (g_pGameConf->GetOffset("IServer__ProcessConnectionlessPacket", &offset))
	{
		SH_MANUALHOOK_RECONFIGURE(ProcessConnectionlessPacket, offset, 0, 0);
		SH_ADD_MANUALHOOK(ProcessConnectionlessPacket, iserver, SH_STATIC(Hook_ProcessConnectionlessPacket), false);
	}
	else
	{
		smutils->LogError(myself, "Failed to find IServer::ProcessConnectionlessPacket offset.");
		return;
	}

	g_pConnectTimer = timersys->CreateTimer(&g_ConnectTimer, 1.0, NULL, TIMER_FLAG_REPEAT);

	// A2S_INFO
	CInfo &info = g_QueryCache.info;
	info.aGameDirLen = strlcpy(info.aGameDir, smutils->GetGameFolderName(), sizeof(info.aGameDir));

	info.iSteamAppID = engine->GetAppID();

	info.aVersionLen = snprintf(info.aVersion, sizeof(info.aVersion), "%d", engine->GetServerVersion());

	info.iUDPPort = iserver->GetUDPPort();
	info.nNewFlags |= 0x80;

	info.iGameID = info.iSteamAppID;
	info.nNewFlags |= 0x01;

	UpdateQueryCache();

	// A2S_PLAYER
	for(int slot = 0; slot < iserver->GetClientCount(); slot++)
	{
		int client = slot + 1;
		IClient *pClient = iserver->GetClient(slot);
		if(!pClient || !pClient->IsConnected())
			continue;

		CPlayer &player = g_QueryCache.players[client];
		IGamePlayer *gplayer = playerhelpers->GetGamePlayer(client);

		if(!player.active)
		{
			g_QueryCache.info.nNumClients++;
			if(pClient->IsFakeClient() && !pClient->IsHLTV() && (!gplayer || (gplayer->IsConnected() && !gplayer->IsSourceTV())))
			{
				g_QueryCache.info.nFakeClients++;
				player.fake = true;
			}
		}

		player.active = true;
		player.pClient = pClient;
		player.nameLen = strlcpy(player.name, pClient->GetClientName(), sizeof(player.name));

		INetChannelInfo *netinfo = (INetChannelInfo *)player.pClient->GetNetChannel();
		if(netinfo)
			player.time = *net_time - netinfo->GetTimeConnected();
		else
			player.time = 0;

		if(gplayer && gplayer->IsConnected())
		{
			IPlayerInfo *info = gplayer->GetPlayerInfo();
			if(info)
				player.score = info->GetFragCount();
			else
				player.score = 0;
		}

		g_UserIDtoClientMap[pClient->GetUserID()] = client;
	}
}

void Connect::OnClientSettingsChanged(int client)
{
	if(client >= 1 && client <= SM_MAXPLAYERS)
	{
		CPlayer &player = g_QueryCache.players[client];
		if(player.active && player.pClient)
			player.nameLen = strlcpy(player.name, player.pClient->GetClientName(), sizeof(player.name));
	}
}

void Connect::OnClientPutInServer(int client)
{
	if(client >= 1 && client <= SM_MAXPLAYERS)
	{
		CPlayer &player = g_QueryCache.players[client];
		IGamePlayer *gplayer = playerhelpers->GetGamePlayer(client);
		if(player.active && player.fake && gplayer->IsSourceTV())
		{
			player.fake = false;
			g_QueryCache.info.nFakeClients--;
		}
	}
}

void Connect::OnTimer()
{
	for(int client = 1; client <= SM_MAXPLAYERS; client++)
	{
		CPlayer &player = g_QueryCache.players[client];
		if(!player.active)
			continue;

		IGamePlayer *gplayer = playerhelpers->GetGamePlayer(client);
		if(!gplayer || !gplayer->IsConnected())
			continue;

		IPlayerInfo *info = gplayer->GetPlayerInfo();
		if(info)
			player.score = info->GetFragCount();
	}

	UpdateQueryCache();
}

void PlayerConnect(const int client, const int userid, const bool bot, const char *name)
{
	if (g_SvLogging->GetInt())
		g_pSM->LogMessage(myself, "player_connect(client=%d, userid=%d, bot=%d, name=%s)", client, userid, bot, name);

	if(client >= 1 && client <= SM_MAXPLAYERS)
	{
		CPlayer &player = g_QueryCache.players[client];

		player.active = true;
		player.fake = false;
		player.pClient = iserver->GetClient(client - 1);
		g_QueryCache.info.nNumClients++;
		if(bot)
		{
			player.fake = true;
			g_QueryCache.info.nFakeClients++;
		}
		player.time = *net_time;
		player.score = 0;
		player.nameLen = strlcpy(player.name, player.pClient->GetClientName(), sizeof(player.name));

		g_UserIDtoClientMap[userid] = client;

		if (g_SvLogging->GetInt())
			g_pSM->LogMessage(myself, "\tCPlayer(active=%d, fake=%d, pClient=%p, name=%s)", player.active, player.fake, player.pClient, player.name);
	}
}

void PlayerChangeName(const int userid)
{
	const int client = g_UserIDtoClientMap[userid];

	g_Connect.OnClientSettingsChanged(client);
}

void PlayerDisconnect(const int userid)
{
	const int client = g_UserIDtoClientMap[userid];
	g_UserIDtoClientMap[userid] = 0;

	if (g_SvLogging->GetInt())
		g_pSM->LogMessage(myself, "player_disconnect(userid=%d, client=%d)", userid, client);

	if(client >= 1 && client <= SM_MAXPLAYERS)
	{
		CPlayer &player = g_QueryCache.players[client];
		if (g_SvLogging->GetInt())
			g_pSM->LogMessage(myself, "\tCPlayer(active=%d, fake=%d, pClient=%p, name=%s)", player.active, player.fake, player.pClient, player.name);

		if(player.active)
		{
			g_QueryCache.info.nNumClients--;
			if(player.fake)
				g_QueryCache.info.nFakeClients--;
		}
		player.active = false;
		player.pClient = NULL;
	}

	if(client >= 1 && client <= SM_MAXPLAYERS)
	{
		char *pSteamID = g_ClientSteamIDMap[client];
		if(*pSteamID)
		{
			if (g_SvLogging->GetInt())
				g_pSM->LogMessage(myself, "%s OnClientDisconnecting: %d", pSteamID, client);

			g_ConnectClientStorage.remove(pSteamID);
			*pSteamID = 0;
		}
	}
}

void ConnectEvents::FireGameEvent(IGameEvent *event)
{
	const char *name = event->GetName();

	if(strcmp(name, "player_connect") == 0)
	{
		const int client = event->GetInt("index") + 1;
		const int userid = event->GetInt("userid");
		const bool bot = event->GetBool("bot");
		const char *name = event->GetString("name");
		PlayerConnect(client, userid, bot, name);
	}
	else if(strcmp(name, "player_disconnect") == 0)
	{
		const int userid = event->GetInt("userid");
		PlayerDisconnect(userid);
	}
	else if(strcmp(name, "player_changename") == 0)
	{
		const int userid = event->GetInt("userid");
		PlayerChangeName(userid);
	}
}

ResultType ConnectTimer::OnTimer(ITimer *pTimer, void *pData)
{
	g_Connect.OnTimer();
	return Pl_Continue;
}

void ConnectTimer::OnTimerEnd(ITimer *pTimer, void *pData) {}
