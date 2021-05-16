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
//#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>

#include <iclient.h>
#include <iserver.h>
#include <ISDKTools.h>

#include "CDetour/detours.h"
#include "extension.h"

// voice packets are sent over unreliable netchannel
//#define NET_MAX_DATAGRAM_PAYLOAD	4000	// = maximum unreliable payload size
// voice packetsize = 64 | netchannel overflows at >4000 bytes
// with 22050 samplerate and 512 frames per packet -> 23.22ms per packet
// SVC_VoiceData overhead = 5 bytes
// sensible limit of 8 packets per frame = 552 bytes -> 185.76ms of voice data per frame
#define NET_MAX_VOICE_BYTES_FRAME (8 * (5 + 64))

ConVar g_SmVoiceAddr("sm_voice_addr", "127.0.0.1", FCVAR_PROTECTED, "Voice server listen ip address.");
ConVar g_SmVoicePort("sm_voice_port", "27020", FCVAR_PROTECTED, "Voice server listen port.", true, 1025.0, true, 65535.0);

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

template <typename T> inline T min(T a, T b) { return a<b?a:b; }

CVoice g_Interface;
SMEXT_LINK(&g_Interface);

CGlobalVars *gpGlobals = NULL;
ISDKTools *g_pSDKTools = NULL;
IServer *iserver = NULL;

double g_fLastVoiceData[SM_MAXPLAYERS + 1];
int g_aFrameVoiceBytes[SM_MAXPLAYERS + 1];

DETOUR_DECL_STATIC4(SV_BroadcastVoiceData, void, IClient *, pClient, int, nBytes, char *, data, int64, xuid)
{
	if(g_Interface.OnBroadcastVoiceData(pClient, nBytes, data))
		DETOUR_STATIC_CALL(SV_BroadcastVoiceData)(pClient, nBytes, data, xuid);
}

#ifdef _WIN32
DETOUR_DECL_STATIC2(SV_BroadcastVoiceData_LTCG, void, char *, data, int64, xuid)
{
	IClient *pClient = NULL;
	int nBytes = 0;

	__asm mov pClient, ecx;
	__asm mov nBytes, edx;

	bool ret = g_Interface.OnBroadcastVoiceData(pClient, nBytes, data);

	__asm mov ecx, pClient;
	__asm mov edx, nBytes;

	if(ret)
		DETOUR_STATIC_CALL(SV_BroadcastVoiceData_LTCG)(data, xuid);
}
#endif

double getTime()
{
    struct timespec tv;
    if(clock_gettime(CLOCK_REALTIME, &tv) != 0)
    	return 0;

    return (tv.tv_sec + (tv.tv_nsec / 1000000000.0));
}

void OnGameFrame(bool simulating)
{
	g_Interface.OnGameFrame(simulating);
}

CVoice::CVoice()
{
	m_ListenSocket = -1;

	m_PollFds = 0;
	for(int i = 1; i < 1 + MAX_CLIENTS; i++)
		m_aPollFds[i].fd = -1;

	for(int i = 0; i < MAX_CLIENTS; i++)
		m_aClients[i].m_Socket = -1;

	m_AvailableTime = 0.0;

	m_pMode = NULL;
	m_pCodec = NULL;

	m_VoiceDetour = NULL;
	m_SV_BroadcastVoiceData = NULL;
}

bool CVoice::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	// Setup engine-specific data.
	Dl_info info;
	void *engineFactory = (void *)g_SMAPI->GetEngineFactory(false);
	if(dladdr(engineFactory, &info) == 0)
	{
		g_SMAPI->Format(error, maxlength, "dladdr(engineFactory) failed.");
		return false;
	}

	void *pEngineSo = dlopen(info.dli_fname, RTLD_NOW);
	if(pEngineSo == NULL)
	{
		g_SMAPI->Format(error, maxlength, "dlopen(%s) failed.", info.dli_fname);
		return false;
	}

	int engineVersion = g_SMAPI->GetSourceEngineBuild();
	void *adrVoiceData = NULL;

	switch (engineVersion)
	{
		case SOURCE_ENGINE_CSGO:
#ifdef _WIN32
			adrVoiceData = memutils->FindPattern(pEngineSo, "\x55\x8B\xEC\x81\xEC\xD0\x00\x00\x00\x53\x56\x57", 12);
#else
			adrVoiceData = memutils->ResolveSymbol(pEngineSo, "_Z21SV_BroadcastVoiceDataP7IClientiPcx");
#endif
			break;

		case SOURCE_ENGINE_LEFT4DEAD2:
#ifdef _WIN32
			adrVoiceData = memutils->FindPattern(pEngineSo, "\x55\x8B\xEC\x83\xEC\x70\xA1\x2A\x2A\x2A\x2A\x33\xC5\x89\x45\xFC\xA1\x2A\x2A\x2A\x2A\x53\x56", 23);
#else
			adrVoiceData = memutils->ResolveSymbol(pEngineSo, "_Z21SV_BroadcastVoiceDataP7IClientiPcx");
#endif
			break;

		case SOURCE_ENGINE_NUCLEARDAWN:
#ifdef _WIN32
			adrVoiceData = memutils->FindPattern(pEngineSo, "\x55\x8B\xEC\xA1\x2A\x2A\x2A\x2A\x83\xEC\x58\x57\x33\xFF", 14);
#else
			adrVoiceData = memutils->ResolveSymbol(pEngineSo, "_Z21SV_BroadcastVoiceDataP7IClientiPcx");
#endif
			break;

		case SOURCE_ENGINE_INSURGENCY:
#ifdef _WIN32
			adrVoiceData = memutils->FindPattern(pEngineSo, "\x55\x8B\xEC\x83\xEC\x74\x68\x2A\x2A\x2A\x2A\x8D\x4D\xE4\xE8", 15);
#else
			adrVoiceData = memutils->ResolveSymbol(pEngineSo, "_Z21SV_BroadcastVoiceDataP7IClientiPcx");
#endif
			break;

		case SOURCE_ENGINE_TF2:
		case SOURCE_ENGINE_CSS:
		case SOURCE_ENGINE_HL2DM:
		case SOURCE_ENGINE_DODS:
		case SOURCE_ENGINE_SDK2013:
#ifdef _WIN32
			adrVoiceData = memutils->FindPattern(pEngineSo, "\x55\x8B\xEC\xA1\x2A\x2A\x2A\x2A\x83\xEC\x50\x83\x78\x30", 14);
#else
			adrVoiceData = memutils->ResolveSymbol(pEngineSo, "_Z21SV_BroadcastVoiceDataP7IClientiPcx");
#endif
			break;

		default:
			g_SMAPI->Format(error, maxlength, "Unsupported game.");
			dlclose(pEngineSo);
			return false;
	}
	dlclose(pEngineSo);

	m_SV_BroadcastVoiceData = (t_SV_BroadcastVoiceData)adrVoiceData;
	if(!m_SV_BroadcastVoiceData)
	{
		g_SMAPI->Format(error, maxlength, "SV_BroadcastVoiceData sigscan failed.");
		return false;
	}

	// Setup voice detour.
	CDetourManager::Init(g_pSM->GetScriptingEngine(), NULL);

#ifdef _WIN32
	if (engineVersion == SOURCE_ENGINE_CSGO || engineVersion == SOURCE_ENGINE_INSURGENCY)
	{
		m_VoiceDetour = DETOUR_CREATE_STATIC(SV_BroadcastVoiceData_LTCG, adrVoiceData);
	}
	else
	{
		m_VoiceDetour = DETOUR_CREATE_STATIC(SV_BroadcastVoiceData, adrVoiceData);
	}
#else
	m_VoiceDetour = DETOUR_CREATE_STATIC(SV_BroadcastVoiceData, adrVoiceData);
#endif

	if (!m_VoiceDetour)
	{
		g_SMAPI->Format(error, maxlength, "SV_BroadcastVoiceData detour failed.");
		return false;
	}

	m_VoiceDetour->EnableDetour();

	// Encoder settings
	m_EncoderSettings.SampleRate_Hz = 22050;
	m_EncoderSettings.TargetBitRate_Kbps = 64;
	m_EncoderSettings.FrameSize = 512; // samples
	m_EncoderSettings.PacketSize = 64;
	m_EncoderSettings.Complexity = 10; // 0 - 10
	m_EncoderSettings.FrameTime = (double)m_EncoderSettings.FrameSize / (double)m_EncoderSettings.SampleRate_Hz;

	// Init CELT encoder
	int theError;
	m_pMode = celt_mode_create(m_EncoderSettings.SampleRate_Hz, m_EncoderSettings.FrameSize, &theError);
	if(!m_pMode)
	{
		g_SMAPI->Format(error, maxlength, "celt_mode_create error: %d", theError);
		SDK_OnUnload();
		return false;
	}

	m_pCodec = celt_encoder_create_custom(m_pMode, 1, &theError);
	if(!m_pCodec)
	{
		g_SMAPI->Format(error, maxlength, "celt_encoder_create_custom error: %d", theError);
		SDK_OnUnload();
		return false;
	}

	celt_encoder_ctl(m_pCodec, CELT_RESET_STATE_REQUEST, NULL);
	celt_encoder_ctl(m_pCodec, CELT_SET_BITRATE(m_EncoderSettings.TargetBitRate_Kbps * 1000));
	celt_encoder_ctl(m_pCodec, CELT_SET_COMPLEXITY(m_EncoderSettings.Complexity));

	return true;
}

bool CVoice::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	gpGlobals = ismm->GetCGlobals();
	ConVar_Register(0, this);

	return true;
}

bool CVoice::RegisterConCommandBase(ConCommandBase *pVar)
{
	/* Always call META_REGCVAR instead of going through the engine. */
	return META_REGCVAR(pVar);
}

cell_t IsClientTalking(IPluginContext *pContext, const cell_t *params)
{
	int client = params[1];

	if(client < 1 || client > SM_MAXPLAYERS)
	{
		return pContext->ThrowNativeError("Client index %d is invalid", client);
	}

	double d = gpGlobals->curtime - g_fLastVoiceData[client];

	if(d < 0) // mapchange
		return false;

	if(d > 0.33)
		return false;

	return true;
}

const sp_nativeinfo_t MyNatives[] =
{
	{ "IsClientTalking", IsClientTalking },
	{ NULL, NULL }
};

static void ListenSocketAction(void *pData)
{
	CVoice *pThis = (CVoice *)pData;
	pThis->ListenSocket();
}

void CVoice::SDK_OnAllLoaded()
{
	sharesys->AddNatives(myself, MyNatives);
	sharesys->RegisterLibrary(myself, "Voice");

	SM_GET_LATE_IFACE(SDKTOOLS, g_pSDKTools);
	if(g_pSDKTools == NULL)
	{
		smutils->LogError(myself, "SDKTools interface not found");
		SDK_OnUnload();
		return;
	}

	iserver = g_pSDKTools->GetIServer();
	if(iserver == NULL)
	{
		smutils->LogError(myself, "Failed to get IServer interface from SDKTools!");
		SDK_OnUnload();
		return;
	}

	// Init tcp server
	m_ListenSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(m_ListenSocket < 0)
	{
		smutils->LogError(myself, "Failed creating socket.");
		SDK_OnUnload();
		return;
	}

	int yes = 1;
	if(setsockopt(m_ListenSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0)
	{
		smutils->LogError(myself, "Failed setting SO_REUSEADDR on socket.");
		SDK_OnUnload();
		return;
	}

	// This doesn't seem to work right away ...
	engine->ServerCommand("exec sourcemod/extension.Voice.cfg\n");
	engine->ServerExecute();

	// ... delay starting listen server to next frame
	smutils->AddFrameAction(ListenSocketAction, this);
}

void CVoice::ListenSocket()
{
	if(m_PollFds > 0)
		return;

	sockaddr_in bindAddr;
	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin_family = AF_INET;
	inet_aton(g_SmVoiceAddr.GetString(), &bindAddr.sin_addr);
	bindAddr.sin_port = htons(g_SmVoicePort.GetInt());

	smutils->LogMessage(myself, "Binding to %s:%d!\n", g_SmVoiceAddr.GetString(), g_SmVoicePort.GetInt());

	if(bind(m_ListenSocket, (sockaddr *)&bindAddr, sizeof(sockaddr_in)) < 0)
	{
		smutils->LogError(myself, "Failed binding to socket (%d '%s').", errno, strerror(errno));
		SDK_OnUnload();
		return;
	}

	if(listen(m_ListenSocket, MAX_CLIENTS) < 0)
	{
		smutils->LogError(myself, "Failed listening on socket.");
		SDK_OnUnload();
		return;
	}

	m_aPollFds[0].fd = m_ListenSocket;
	m_aPollFds[0].events = POLLIN;
	m_PollFds++;

	smutils->AddGameFrameHook(::OnGameFrame);
}

void CVoice::SDK_OnUnload()
{
	smutils->RemoveGameFrameHook(::OnGameFrame);

	if(m_VoiceDetour)
	{
		m_VoiceDetour->Destroy();
		m_VoiceDetour = NULL;
	}

	if(m_ListenSocket != -1)
	{
		close(m_ListenSocket);
		m_ListenSocket = -1;
	}

	for(int Client = 0; Client < MAX_CLIENTS; Client++)
	{
		if(m_aClients[Client].m_Socket != -1)
		{
			close(m_aClients[Client].m_Socket);
			m_aClients[Client].m_Socket = -1;
		}
	}

	if(m_pCodec)
		celt_encoder_destroy(m_pCodec);

	if(m_pMode)
		celt_mode_destroy(m_pMode);
}

void CVoice::OnGameFrame(bool simulating)
{
	HandleNetwork();
	HandleVoiceData();

	// Reset per-client voice byte counter to 0 every frame.
	memset(g_aFrameVoiceBytes, 0, sizeof(g_aFrameVoiceBytes));
}

bool CVoice::OnBroadcastVoiceData(IClient *pClient, int nBytes, char *data)
{
	// Reject empty packets
	if(nBytes < 1)
		return false;

	int client = pClient->GetPlayerSlot() + 1;

	// Reject voice packet if we'd send more than NET_MAX_VOICE_BYTES_FRAME voice bytes from this client in the current frame.
	// 5 = SVC_VoiceData header/overhead
	g_aFrameVoiceBytes[client] += 5 + nBytes;

	if(g_aFrameVoiceBytes[client] > NET_MAX_VOICE_BYTES_FRAME)
		return false;

	g_fLastVoiceData[client] = gpGlobals->curtime;

	return true;
}

void CVoice::HandleNetwork()
{
	if(m_ListenSocket == -1)
		return;

	int PollRes = poll(m_aPollFds, m_PollFds, 0);
	if(PollRes <= 0)
		return;

	// Accept new clients
	if(m_aPollFds[0].revents & POLLIN)
	{
		// Find slot
		int Client;
		for(Client = 0; Client < MAX_CLIENTS; Client++)
		{
			if(m_aClients[Client].m_Socket == -1)
				break;
		}

		// no free slot
		if(Client != MAX_CLIENTS)
		{
			sockaddr_in addr;
			size_t size = sizeof(sockaddr_in);
			int Socket = accept(m_ListenSocket, (sockaddr *)&addr, &size);

			m_aClients[Client].m_Socket = Socket;
			m_aClients[Client].m_BufferWriteIndex = 0;
			m_aClients[Client].m_LastLength = 0;
			m_aClients[Client].m_LastValidData = 0.0;
			m_aClients[Client].m_New = true;
			m_aClients[Client].m_UnEven = false;

			m_aPollFds[m_PollFds].fd = Socket;
			m_aPollFds[m_PollFds].events = POLLIN | POLLHUP;
			m_aPollFds[m_PollFds].revents = 0;
			m_PollFds++;

			//smutils->LogMessage(myself, "Client %d connected!\n", Client);
		}
	}

	bool CompressPollFds = false;
	for(int PollFds = 1; PollFds < m_PollFds; PollFds++)
	{
		int Client = -1;
		for(Client = 0; Client < MAX_CLIENTS; Client++)
		{
			if(m_aClients[Client].m_Socket == m_aPollFds[PollFds].fd)
				break;
		}
		if(Client == -1)
			continue;

		CClient *pClient = &m_aClients[Client];

		// Connection shutdown prematurely ^C
		// Make sure to set SO_LINGER l_onoff = 1, l_linger = 0
		if(m_aPollFds[PollFds].revents & POLLHUP)
		{
			close(pClient->m_Socket);
			pClient->m_Socket = -1;
			m_aPollFds[PollFds].fd = -1;
			CompressPollFds = true;
			//smutils->LogMessage(myself, "Client %d disconnected!(2)\n", Client);
			continue;
		}

		// Data available?
		if(!(m_aPollFds[PollFds].revents & POLLIN))
			continue;

		size_t BytesAvailable;
		if(ioctl(pClient->m_Socket, FIONREAD, &BytesAvailable) == -1)
			continue;

		if(pClient->m_New)
		{
			pClient->m_BufferWriteIndex = m_Buffer.GetReadIndex();
			pClient->m_New = false;
		}

		m_Buffer.SetWriteIndex(pClient->m_BufferWriteIndex);

		// Don't recv() when we can't fit data into the ringbuffer
		unsigned char aBuf[32768];
		if(min(BytesAvailable, sizeof(aBuf)) > m_Buffer.CurrentFree() * sizeof(int16_t))
			continue;

		// Edge case: previously received data is uneven and last recv'd byte has to be prepended
		int Shift = 0;
		if(pClient->m_UnEven)
		{
			Shift = 1;
			aBuf[0] = pClient->m_Remainder;
			pClient->m_UnEven = false;
		}

		ssize_t Bytes = recv(pClient->m_Socket, &aBuf[Shift], sizeof(aBuf) - Shift, 0);

		if(Bytes <= 0)
		{
			close(pClient->m_Socket);
			pClient->m_Socket = -1;
			m_aPollFds[PollFds].fd = -1;
			CompressPollFds = true;
			//smutils->LogMessage(myself, "Client %d disconnected!(1)\n", Client);
			continue;
		}

		Bytes += Shift;

		// Edge case: data received is uneven (can't be divided by two)
		// store last byte, drop it here and prepend it right before the next recv
		if(Bytes & 1)
		{
			pClient->m_UnEven = true;
			pClient->m_Remainder = aBuf[Bytes - 1];
			Bytes -= 1;
		}

		// Got data!
		OnDataReceived(pClient, (int16_t *)aBuf, Bytes / sizeof(int16_t));

		pClient->m_LastLength = m_Buffer.CurrentLength();
		pClient->m_BufferWriteIndex = m_Buffer.GetWriteIndex();
	}

	if(CompressPollFds)
	{
		for(int PollFds = 1; PollFds < m_PollFds; PollFds++)
		{
			if(m_aPollFds[PollFds].fd != -1)
				continue;

			for(int PollFds_ = PollFds; PollFds_ < 1 + MAX_CLIENTS; PollFds_++)
				m_aPollFds[PollFds_].fd = m_aPollFds[PollFds_ + 1].fd;

			PollFds--;
			m_PollFds--;
		}
	}
}

void CVoice::OnDataReceived(CClient *pClient, int16_t *pData, size_t Samples)
{
	// Check for empty input
	ssize_t DataStartsAt = -1;
	for(size_t i = 0; i < Samples; i++)
	{
		if(pData[i] == 0)
			continue;

		DataStartsAt = i;
		break;
	}

	// Discard empty data if last vaild data was more than a second ago.
	if(pClient->m_LastValidData + 1.0 < getTime())
	{
		// All empty
		if(DataStartsAt == -1)
			return;

		// Data starts here
		pData += DataStartsAt;
		Samples -= DataStartsAt;
	}

	if(!m_Buffer.Push(pData, Samples))
	{
		smutils->LogError(myself, "Buffer push failed!!! Samples: %u, Free: %u\n", Samples, m_Buffer.CurrentFree());
		return;
	}

	pClient->m_LastValidData = getTime();
}

void CVoice::HandleVoiceData()
{
	int SamplesPerFrame = m_EncoderSettings.FrameSize;
	int PacketSize = m_EncoderSettings.PacketSize;
	int FramesAvailable = m_Buffer.TotalLength() / SamplesPerFrame;
	float TimeAvailable = (float)m_Buffer.TotalLength() / (float)m_EncoderSettings.SampleRate_Hz;

	if(!FramesAvailable)
		return;

	// Before starting playback we want at least 100ms in the buffer
	if(m_AvailableTime < getTime() && TimeAvailable < 0.1)
		return;

	// let the clients have no more than 500ms
	if(m_AvailableTime > getTime() + 0.5)
		return;

	// 5 = max frames per packet
	FramesAvailable = min(FramesAvailable, 5);

	// 0 = SourceTV
	IClient *pClient = iserver->GetClient(0);
	if(!pClient)
		return;

	for(int Frame = 0; Frame < FramesAvailable; Frame++)
	{
		// Get data into buffer from ringbuffer.
		int16_t aBuffer[SamplesPerFrame];

		size_t OldReadIdx = m_Buffer.m_ReadIndex;
		size_t OldCurLength = m_Buffer.CurrentLength();
		size_t OldTotalLength = m_Buffer.TotalLength();

		if(!m_Buffer.Pop(aBuffer, SamplesPerFrame))
		{
			printf("Buffer pop failed!!! Samples: %u, Length: %u\n", SamplesPerFrame, m_Buffer.TotalLength());
			return;
		}

		// Encode it!
		unsigned char aFinal[PacketSize];
		size_t FinalSize = 0;

		FinalSize = celt_encode(m_pCodec, aBuffer, SamplesPerFrame, aFinal, sizeof(aFinal));

		if(FinalSize <= 0)
		{
			smutils->LogError(myself, "Compress returned %d\n", FinalSize);
			return;
		}

		// Check for buffer underruns
		for(int Client = 0; Client < MAX_CLIENTS; Client++)
		{
			CClient *pClient = &m_aClients[Client];
			if(pClient->m_Socket == -1 || pClient->m_New == true)
				continue;

			m_Buffer.SetWriteIndex(pClient->m_BufferWriteIndex);

			if(m_Buffer.CurrentLength() > pClient->m_LastLength)
			{
				pClient->m_BufferWriteIndex = m_Buffer.GetReadIndex();
				m_Buffer.SetWriteIndex(pClient->m_BufferWriteIndex);
				pClient->m_LastLength = m_Buffer.CurrentLength();
			}
		}

		BroadcastVoiceData(pClient, FinalSize, aFinal);
	}

	if(m_AvailableTime < getTime())
		m_AvailableTime = getTime();

	m_AvailableTime += (double)FramesAvailable * m_EncoderSettings.FrameTime;
}

void CVoice::BroadcastVoiceData(IClient *pClient, int nBytes, unsigned char *pData)
{
	#ifdef _WIN32
	__asm mov ecx, pClient;
	__asm mov edx, nBytes;

	DETOUR_STATIC_CALL(SV_BroadcastVoiceData_LTCG)((char *)pData, 0);
	#else
	DETOUR_STATIC_CALL(SV_BroadcastVoiceData)(pClient, nBytes, (char *)pData, 0);
	#endif
}
