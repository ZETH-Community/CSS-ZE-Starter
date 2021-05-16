//#undef REQUIRE_EXTENSIONS
#include "sourcetvmanager"

public void OnPluginStart()
{
	LoadTranslations("common.phrases");

	RegConsoleCmd("sm_servercount", Cmd_GetServerCount);
	RegConsoleCmd("sm_selectserver", Cmd_SelectServer);
	RegConsoleCmd("sm_selectedserver", Cmd_GetSelectedServer);
	RegConsoleCmd("sm_ismaster", Cmd_IsMasterProxy);
	RegConsoleCmd("sm_serverip", Cmd_GetServerIP);
	RegConsoleCmd("sm_serverport", Cmd_GetServerPort);
	RegConsoleCmd("sm_botindex", Cmd_GetBotIndex);
	RegConsoleCmd("sm_broadcasttick", Cmd_GetBroadcastTick);
	RegConsoleCmd("sm_localstats", Cmd_Localstats);
	RegConsoleCmd("sm_globalstats", Cmd_Globalstats);
	RegConsoleCmd("sm_getdelay", Cmd_GetDelay);
	RegConsoleCmd("sm_spectators", Cmd_Spectators);
	RegConsoleCmd("sm_spechintmsg", Cmd_SendHintMessage);
	RegConsoleCmd("sm_specchat", Cmd_SendChatMessage);
	RegConsoleCmd("sm_specchatlocal", Cmd_SendChatMessageLocal);
	RegConsoleCmd("sm_specconsole", Cmd_SendMessage);
	RegConsoleCmd("sm_viewentity", Cmd_GetViewEntity);
	RegConsoleCmd("sm_vieworigin", Cmd_GetViewOrigin);
	RegConsoleCmd("sm_forcechasecam", Cmd_ForceChaseCameraShot);
	//RegConsoleCmd("sm_forcefixedcam", Cmd_ForceFixedCameraShot);
	RegConsoleCmd("sm_startrecording", Cmd_StartRecording);
	RegConsoleCmd("sm_stoprecording", Cmd_StopRecording);
	RegConsoleCmd("sm_isrecording", Cmd_IsRecording);
	RegConsoleCmd("sm_demofile", Cmd_GetDemoFileName);
	RegConsoleCmd("sm_recordtick", Cmd_GetRecordTick);
	RegConsoleCmd("sm_specstatus", Cmd_SpecStatus);
	RegConsoleCmd("sm_democonsole", Cmd_PrintDemoConsole);
	RegConsoleCmd("sm_botcmd", Cmd_ExecuteStringCommand);
	RegConsoleCmd("sm_speckick", Cmd_KickClient);
	RegConsoleCmd("sm_specchatone", Cmd_PrintToChat);
	RegConsoleCmd("sm_specconsoleone", Cmd_PrintToConsole);
	RegConsoleCmd("sm_spectitle", Cmd_SetTVTitle);
}

public void SourceTV_OnStartRecording(int instance, const char[] filename)
{
	PrintToServer("Started recording sourcetv #%d demo to %s", instance, filename);
}

public void SourceTV_OnStopRecording(int instance, const char[] filename, int recordingtick)
{
	PrintToServer("Stopped recording sourcetv #%d demo to %s (%d ticks)", instance, filename, recordingtick);
}

public bool SourceTV_OnSpectatorPreConnect(const char[] name, char password[255], const char[] ip, char rejectReason[255])
{
	PrintToServer("SourceTV spectator is connecting! Name: %s, pw: %s, ip: %s", name, password, ip);
	if (StrEqual(password, "nope", false))
	{
		strcopy(rejectReason, 255, "Heh, that password sucks.");
		return false;
	}
	return true;
}

public void SourceTV_OnServerStart(int instance)
{
	PrintToServer("SourceTV instance %d started.", instance);
}

public void SourceTV_OnServerShutdown(int instance)
{
	PrintToServer("SourceTV instance %d shutdown.", instance);
}

public void SourceTV_OnSpectatorConnected(int client)
{
	PrintToServer("SourceTV client %d connected. (isconnected %d)", client, SourceTV_IsClientConnected(client));
}

public void SourceTV_OnSpectatorPutInServer(int client)
{
	PrintToServer("SourceTV client %d put in server.", client);
}

public void SourceTV_OnSpectatorDisconnect(int client, char reason[255])
{
	PrintToServer("SourceTV client %d is disconnecting (isconnected %d) with reason -> %s.", client, SourceTV_IsClientConnected(client), reason);
}

public void SourceTV_OnSpectatorDisconnected(int client, const char reason[255])
{
	PrintToServer("SourceTV client %d disconnected (isconnected %d) with reason -> %s.", client, SourceTV_IsClientConnected(client), reason);
}

public Action SourceTV_OnSpectatorChatMessage(int client, char message[255], char chatgroup[255])
{
	PrintToServer("SourceTV client %d (chatgroup \"%s\") writes: %s", client, chatgroup, message);
	return Plugin_Continue;
}

public void SourceTV_OnSpectatorChatMessage_Post(int client, const char[] message, const char[] chatgroup)
{
	PrintToServer("SourceTV client %d (chatgroup \"%s\") wrote: %s", client, chatgroup, message);
}

public Action Cmd_GetServerCount(int client, int args)
{
	ReplyToCommand(client, "SourceTV server count: %d", SourceTV_GetServerInstanceCount());
	return Plugin_Handled;
}

public Action Cmd_SelectServer(int client, int args)
{
	if (args < 1)
	{
		ReplyToCommand(client, "Usage: sm_selectserver <instance id>");
		return Plugin_Handled;
	}
	
	char sArg[12];
	GetCmdArg(1, sArg, sizeof(sArg));
	int iInstance = StringToInt(sArg);
	
	SourceTV_SelectServerInstance(iInstance);
	ReplyToCommand(client, "SourceTV selecting server: %d", iInstance);
	return Plugin_Handled;
}

public Action Cmd_GetSelectedServer(int client, int args)
{
	ReplyToCommand(client, "SourceTV selected server: %d", SourceTV_GetSelectedServerInstance());
	return Plugin_Handled;
}

public Action Cmd_IsMasterProxy(int client, int args)
{
	ReplyToCommand(client, "SourceTV is master proxy: %d", SourceTV_IsMasterProxy());
	return Plugin_Handled;
}

public Action Cmd_GetServerIP(int client, int args)
{
	char sIP[32];
	bool bSuccess = SourceTV_GetServerIP(sIP, sizeof(sIP));
	ReplyToCommand(client, "SourceTV server ip (ret %d): %s", bSuccess, sIP);
	return Plugin_Handled;
}

public Action Cmd_GetServerPort(int client, int args)
{
	ReplyToCommand(client, "SourceTV server port: %d", SourceTV_GetServerPort());
	return Plugin_Handled;
}

public Action Cmd_GetBotIndex(int client, int args)
{
	ReplyToCommand(client, "SourceTV bot index: %d", SourceTV_GetBotIndex());
	return Plugin_Handled;
}

public Action Cmd_GetBroadcastTick(int client, int args)
{
	ReplyToCommand(client, "SourceTV broadcast tick: %d", SourceTV_GetBroadcastTick());
	return Plugin_Handled;
}

public Action Cmd_Localstats(int client, int args)
{
	int proxies, slots, specs;
	if (!SourceTV_GetLocalStats(proxies, slots, specs))
	{
		ReplyToCommand(client, "SourceTV local stats: no server selected :(");
		return Plugin_Handled;
	}
	ReplyToCommand(client, "SourceTV local stats: proxies %d - slots %d - specs %d", proxies, slots, specs);
	return Plugin_Handled;
}

public Action Cmd_Globalstats(int client, int args)
{
	int proxies, slots, specs;
	if (!SourceTV_GetGlobalStats(proxies, slots, specs))
	{
		ReplyToCommand(client, "SourceTV global stats: no server selected :(");
		return Plugin_Handled;
	}
	ReplyToCommand(client, "SourceTV global stats: proxies %d - slots %d - specs %d", proxies, slots, specs);
	return Plugin_Handled;
}

public Action Cmd_GetDelay(int client, int args)
{
	ReplyToCommand(client, "SourceTV delay: %f", SourceTV_GetDelay());
	return Plugin_Handled;
}

public Action Cmd_Spectators(int client, int args)
{
	ReplyToCommand(client, "SourceTV spectator count: %d/%d", SourceTV_GetSpectatorCount(), SourceTV_GetClientCount());
	char sName[64], sIP[16], sPassword[256];
	for (int i=1;i<=SourceTV_GetClientCount();i++)
	{
		if (!SourceTV_IsClientConnected(i))
			continue;
		
		SourceTV_GetClientName(i, sName, sizeof(sName));
		SourceTV_GetClientIP(i, sIP, sizeof(sIP));
		SourceTV_GetClientPassword(i, sPassword, sizeof(sPassword));
		ReplyToCommand(client, "Client %d%s: %s - %s (password: %s)", i, (SourceTV_IsClientProxy(i)?" (RELAY)":""), sName, sIP, sPassword);
	}
	return Plugin_Handled;
}

public Action Cmd_SendHintMessage(int client, int args)
{
	if (args < 1)
	{
		ReplyToCommand(client, "Usage: sm_spechintmsg <message>");
		return Plugin_Handled;
	}
	
	char sMsg[1024];
	GetCmdArgString(sMsg, sizeof(sMsg));
	StripQuotes(sMsg);
	
	bool bSent = SourceTV_BroadcastScreenMessage(BTarget_Everyone, "%s", sMsg);
	ReplyToCommand(client, "SourceTV sending hint message (success %d): %s", bSent, sMsg);
	return Plugin_Handled;
}

public Action Cmd_SendMessage(int client, int args)
{
	if (args < 1)
	{
		ReplyToCommand(client, "Usage: sm_specconsole <message>");
		return Plugin_Handled;
	}
	
	char sMsg[1024];
	GetCmdArgString(sMsg, sizeof(sMsg));
	StripQuotes(sMsg);
	
	bool bSent = SourceTV_BroadcastConsoleMessage("%s", sMsg);
	ReplyToCommand(client, "SourceTV sending console message (success %d): %s", bSent, sMsg);
	return Plugin_Handled;
}

public Action Cmd_SendChatMessage(int client, int args)
{
	if (args < 1)
	{
		ReplyToCommand(client, "Usage: sm_specchat <message>");
		return Plugin_Handled;
	}
	
	char sMsg[128];
	GetCmdArgString(sMsg, sizeof(sMsg));
	StripQuotes(sMsg);
	
	bool bSent = SourceTV_BroadcastChatMessage(BTarget_Everyone, "%s", sMsg);
	ReplyToCommand(client, "SourceTV sending chat message to all spectators (including relays) (success %d): %s", bSent, sMsg);
	return Plugin_Handled;
}

public Action Cmd_SendChatMessageLocal(int client, int args)
{
	if (args < 1)
	{
		ReplyToCommand(client, "Usage: sm_specchatlocal <message>");
		return Plugin_Handled;
	}
	
	char sMsg[128];
	GetCmdArgString(sMsg, sizeof(sMsg));
	StripQuotes(sMsg);
	
	bool bSent = SourceTV_BroadcastChatMessage(BTarget_OnlyLocal, "%s", sMsg);
	ReplyToCommand(client, "SourceTV sending chat message to local spectators (success %d): %s", bSent, sMsg);
	return Plugin_Handled;
}

public Action Cmd_GetViewEntity(int client, int args)
{
	ReplyToCommand(client, "SourceTV view entity: %d", SourceTV_GetViewEntity());
	return Plugin_Handled;
}

public Action Cmd_GetViewOrigin(int client, int args)
{
	float pos[3];
	SourceTV_GetViewOrigin(pos);
	ReplyToCommand(client, "SourceTV view origin: %f %f %f", pos[0], pos[1], pos[2]);
	return Plugin_Handled;
}

public Action Cmd_ForceChaseCameraShot(int client, int args)
{
	if (args < 1)
	{
		ReplyToCommand(client, "Usage: sm_forcechasecam <target> <ineye>");
		return Plugin_Handled;
	}
	
	char sTarget[PLATFORM_MAX_PATH];
	GetCmdArg(1, sTarget, sizeof(sTarget));
	StripQuotes(sTarget);
	int iTarget = FindTarget(client, sTarget, false, false);
	if (iTarget == -1)
		return Plugin_Handled;
	
	bool bInEye;
	if (args >= 2)
	{
		char sInEye[16];
		GetCmdArg(2, sInEye, sizeof(sInEye));
		StripQuotes(sInEye);
		bInEye = sInEye[0] == '1';
	}
	
	SourceTV_ForceChaseCameraShot(iTarget, 0, 96, -20, (GetRandomFloat()>0.5)?30:-30, bInEye, 20.0);
	ReplyToCommand(client, "SourceTV forcing camera shot on %N.", iTarget);
	return Plugin_Handled;
}

public Action Cmd_StartRecording(int client, int args)
{
	if (args < 1)
	{
		ReplyToCommand(client, "Usage: sm_startrecording <filename>");
		return Plugin_Handled;
	}
	
	char sFilename[PLATFORM_MAX_PATH];
	GetCmdArgString(sFilename, sizeof(sFilename));
	StripQuotes(sFilename);
	
	if (SourceTV_StartRecording(sFilename))
	{
		SourceTV_GetDemoFileName(sFilename, sizeof(sFilename));
		ReplyToCommand(client, "SourceTV started recording to: %s", sFilename);
	}
	else
		ReplyToCommand(client, "SourceTV failed to start recording to: %s", sFilename);
	return Plugin_Handled;
}

public Action Cmd_StopRecording(int client, int args)
{
	ReplyToCommand(client, "SourceTV stopped recording %d", SourceTV_StopRecording());
	return Plugin_Handled;
}

public Action Cmd_IsRecording(int client, int args)
{
	ReplyToCommand(client, "SourceTV is recording: %d", SourceTV_IsRecording());
	return Plugin_Handled;
}

public Action Cmd_GetDemoFileName(int client, int args)
{
	char sFileName[PLATFORM_MAX_PATH];
	ReplyToCommand(client, "SourceTV demo file name (%d): %s", SourceTV_GetDemoFileName(sFileName, sizeof(sFileName)), sFileName);
	return Plugin_Handled;
}

public Action Cmd_GetRecordTick(int client, int args)
{
	ReplyToCommand(client, "SourceTV recording tick: %d", SourceTV_GetRecordingTick());
	return Plugin_Handled;
}
	
public Action Cmd_SpecStatus(int client, int args)
{
	int iSourceTV = SourceTV_GetBotIndex();
	if (!iSourceTV)
		return Plugin_Handled;
	FakeClientCommand(iSourceTV, "status");
	ReplyToCommand(client, "Sent status bot console.");
	return Plugin_Handled;
}

public Action Cmd_PrintDemoConsole(int client, int args)
{
	if (args < 1)
	{
		ReplyToCommand(client, "Usage: sm_democonsole <message>");
		return Plugin_Handled;
	}
	
	char sMsg[1024];
	GetCmdArgString(sMsg, sizeof(sMsg));
	StripQuotes(sMsg);
	
	bool bSent = SourceTV_PrintToDemoConsole("%s", sMsg);
	ReplyToCommand(client, "SourceTV printing to demo console (success %d): %s", bSent, sMsg);
	return Plugin_Handled;
}

public Action Cmd_ExecuteStringCommand(int client, int args)
{
	if (args < 1)
	{
		ReplyToCommand(client, "Usage: sm_botcmd <cmd>");
		return Plugin_Handled;
	}
	
	char sCmd[1024];
	GetCmdArgString(sCmd, sizeof(sCmd));
	StripQuotes(sCmd);
	
	int iSourceTV = SourceTV_GetBotIndex();
	if (!iSourceTV)
		return Plugin_Handled;
	FakeClientCommand(iSourceTV, sCmd);
	ReplyToCommand(client, "SourceTV executing command on bot: %s", sCmd);
	return Plugin_Handled;
}

public Action Cmd_KickClient(int client, int args)
{
	if (args < 2)
	{
		ReplyToCommand(client, "Usage: sm_speckick <index> <reason>");
		return Plugin_Handled;
	}
	
	char sIndex[16], sMsg[1024];
	GetCmdArg(1, sIndex, sizeof(sIndex));
	StripQuotes(sIndex);
	GetCmdArg(2, sMsg, sizeof(sMsg));
	StripQuotes(sMsg);
	
	int iTarget = StringToInt(sIndex);
	SourceTV_KickClient(iTarget, sMsg);
	ReplyToCommand(client, "SourceTV kicking spectator %d with reason %s", iTarget, sMsg);
	return Plugin_Handled;
}

public Action Cmd_PrintToChat(int client, int args)
{
	if (args < 2)
	{
		ReplyToCommand(client, "Usage: sm_specchatone <index> <message>");
		return Plugin_Handled;
	}
	
	char sIndex[16], sMsg[1024];
	GetCmdArg(1, sIndex, sizeof(sIndex));
	StripQuotes(sIndex);
	GetCmdArg(2, sMsg, sizeof(sMsg));
	StripQuotes(sMsg);
	
	int iTarget = StringToInt(sIndex);
	SourceTV_PrintToChat(iTarget, "%s", sMsg);
	ReplyToCommand(client, "SourceTV sending chat message to spectator %d: %s", iTarget, sMsg);
	return Plugin_Handled;
}

public Action Cmd_PrintToConsole(int client, int args)
{
	if (args < 2)
	{
		ReplyToCommand(client, "Usage: sm_specconsoleone <index> <message>");
		return Plugin_Handled;
	}
	
	char sIndex[16], sMsg[1024];
	GetCmdArg(1, sIndex, sizeof(sIndex));
	StripQuotes(sIndex);
	GetCmdArg(2, sMsg, sizeof(sMsg));
	StripQuotes(sMsg);
	
	int iTarget = StringToInt(sIndex);
	SourceTV_PrintToConsole(iTarget, "%s", sMsg);
	ReplyToCommand(client, "SourceTV sending console message to spectator %d: %s", iTarget, sMsg);
	return Plugin_Handled;
}

public Action Cmd_SetTVTitle(int client, int args)
{
	if (args < 2)
	{
		ReplyToCommand(client, "Usage: sm_spectitle <index> <title>");
		return Plugin_Handled;
	}
	
	char sIndex[16], sMsg[1024];
	GetCmdArg(1, sIndex, sizeof(sIndex));
	StripQuotes(sIndex);
	GetCmdArg(2, sMsg, sizeof(sMsg));
	StripQuotes(sMsg);
	
	int iTarget = StringToInt(sIndex);
	SourceTV_SetClientTVTitle(iTarget, "%s", sMsg);
	ReplyToCommand(client, "SourceTV set stream title of spectator %d to %s", iTarget, sMsg);
	return Plugin_Handled;
}