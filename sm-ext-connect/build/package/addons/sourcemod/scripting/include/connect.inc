#if defined _Connect_Included
 #endinput
#endif
#define _Connect_Included

enum EConnect
{
	k_OnClientPreConnectEx_Reject = 0,
	k_OnClientPreConnectEx_Accept = 1,
	k_OnClientPreConnectEx_Async = -1
};

forward EConnect OnClientPreConnectEx(const char[] sName, char sPassword[255], const char[] sIP, const char[] sSteam32ID, char sRejectReason[255]);

native bool ClientPreConnectEx(const char[] sSteam32ID, EConnect RetVal, char sRejectReason[255]);
native bool SteamClientAuthenticated(const char[] sSteam32ID);

/**
 * Do not edit below this line!
 */
public Extension __ext_connect =
{
	name = "Connect",
	file = "connect.ext",
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};

#if !defined REQUIRE_EXTENSIONS
public __ext_connect_SetNTVOptional()
{
	MarkNativeAsOptional("ClientPreConnectEx");
}
#endif
