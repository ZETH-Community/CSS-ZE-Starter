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

#ifndef _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_

#include <poll.h>
#include "smsdk_ext.h"
#include "celt_header.h"
#include "ringbuffer.h"

/**
 * @file extension.h
 * @brief Sample extension code header.
 */

#define MAX_CLIENTS 16

#ifdef _WIN32
typedef __int64		int64;
#else
typedef long long	int64;
#endif

class CDetour;
class IClient;
typedef void (*t_SV_BroadcastVoiceData)(IClient *, int, unsigned char *, int64);

/**
 * @brief Sample implementation of the SDK Extension.
 * Note: Uncomment one of the pre-defined virtual functions in order to use it.
 */
class CVoice :
	public SDKExtension,
	public IConCommandBaseAccessor
{
public:
	/**
	 * @brief This is called after the initial loading sequence has been processed.
	 *
	 * @param error		Error message buffer.
	 * @param maxlength	Size of error message buffer.
	 * @param late		Whether or not the module was loaded after map load.
	 * @return			True to succeed loading, false to fail.
	 */
	virtual bool SDK_OnLoad(char *error, size_t maxlength, bool late);

	/**
	 * @brief This is called right before the extension is unloaded.
	 */
	virtual void SDK_OnUnload();

	/**
	 * @brief This is called once all known extensions have been loaded.
	 * Note: It is is a good idea to add natives here, if any are provided.
	 */
	virtual void SDK_OnAllLoaded();

	/**
	 * @brief Called when the pause state is changed.
	 */
	//virtual void SDK_OnPauseChange(bool paused);

	/**
	 * @brief this is called when Core wants to know if your extension is working.
	 *
	 * @param error		Error message buffer.
	 * @param maxlength	Size of error message buffer.
	 * @return			True if working, false otherwise.
	 */
	//virtual bool QueryRunning(char *error, size_t maxlength);
public:
#if defined SMEXT_CONF_METAMOD
	/**
	 * @brief Called when Metamod is attached, before the extension version is called.
	 *
	 * @param error			Error buffer.
	 * @param maxlength		Maximum size of error buffer.
	 * @param late			Whether or not Metamod considers this a late load.
	 * @return				True to succeed, false to fail.
	 */
	virtual bool SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlength, bool late);

	/**
	 * @brief Called when Metamod is detaching, after the extension version is called.
	 * NOTE: By default this is blocked unless sent from SourceMod.
	 *
	 * @param error			Error buffer.
	 * @param maxlength		Maximum size of error buffer.
	 * @return				True to succeed, false to fail.
	 */
	//virtual bool SDK_OnMetamodUnload(char *error, size_t maxlength);

	/**
	 * @brief Called when Metamod's pause state is changing.
	 * NOTE: By default this is blocked unless sent from SourceMod.
	 *
	 * @param paused		Pause state being set.
	 * @param error			Error buffer.
	 * @param maxlength		Maximum size of error buffer.
	 * @return				True to succeed, false to fail.
	 */
	//virtual bool SDK_OnMetamodPauseChange(bool paused, char *error, size_t maxlength);
#endif

public:  // IConCommandBaseAccessor
	virtual bool RegisterConCommandBase(ConCommandBase *pVar);

public:
	CVoice();
	void OnGameFrame(bool simulating);
	bool OnBroadcastVoiceData(IClient *pClient, int nBytes, char *data);

	void ListenSocket();

private:
	int m_ListenSocket;

	struct CClient
	{
		int m_Socket;
		size_t m_BufferWriteIndex;
		size_t m_LastLength;
		double m_LastValidData;
		bool m_New;
		bool m_UnEven;
		unsigned char m_Remainder;
	} m_aClients[MAX_CLIENTS];

	struct pollfd m_aPollFds[1 + MAX_CLIENTS];
	int m_PollFds;

	CRingBuffer m_Buffer;

	double m_AvailableTime;

	struct CEncoderSettings
	{
		celt_int32 SampleRate_Hz;
		celt_int32 TargetBitRate_Kbps;
		celt_int32 FrameSize;
		celt_int32 PacketSize;
		celt_int32 Complexity;
		double FrameTime;
	} m_EncoderSettings;

	CELTMode *m_pMode;
	CELTEncoder *m_pCodec;

	t_SV_BroadcastVoiceData m_SV_BroadcastVoiceData;
	CDetour *m_VoiceDetour;

	void HandleNetwork();
	void OnDataReceived(CClient *pClient, int16_t *pData, size_t Samples);
	void HandleVoiceData();
	void BroadcastVoiceData(IClient *pClient, int nBytes, unsigned char *pData);
};

#endif // _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_
