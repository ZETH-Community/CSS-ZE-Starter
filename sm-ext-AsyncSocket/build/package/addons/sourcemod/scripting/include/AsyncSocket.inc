#if defined _AsyncSocket_included
 #endinput
#endif
#define _AsyncSocket_included

typedef AsyncSocketConnectCallback = function void(AsyncSocket socket);

typedef AsyncSocketErrorCallback = function void(AsyncSocket socket, int error, const char[] errorName);

typedef AsyncSocketDataCallback = function void(AsyncSocket socket, const char[] data, const int size);

methodmap AsyncSocket < Handle {
	public native AsyncSocket();

	public native bool Connect(const char[] host, const int port);

	public native bool Listen(const char[] host, const int port);

	public native bool Write(const char[] data, int length = -1);

	public native bool SetConnectCallback(AsyncSocketConnectCallback callback);

	public native bool SetErrorCallback(AsyncSocketErrorCallback callback);

	public native bool SetDataCallback(AsyncSocketDataCallback callback);
}

/**
 * Do not edit below this line!
 */
public Extension __ext_AsyncSocket =
{
	name = "AsyncSocket",
	file = "AsyncSocket.ext",
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
public __ext_AsyncSocket_SetNTVOptional()
{
	MarkNativeAsOptional("AsyncSocket.AsyncSocket");
	MarkNativeAsOptional("AsyncSocket.Connect");
	MarkNativeAsOptional("AsyncSocket.Listen");
	MarkNativeAsOptional("AsyncSocket.Write");
	MarkNativeAsOptional("AsyncSocket.SetConnectCallback");
	MarkNativeAsOptional("AsyncSocket.SetErrorCallback");
	MarkNativeAsOptional("AsyncSocket.SetDataCallback");
}
#endif
