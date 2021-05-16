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
#include "context.h"
#include "readerwriterqueue.h"
#include <uv.h>

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

moodycamel::ReaderWriterQueue<CSocketConnect *> g_ConnectQueue;
moodycamel::ReaderWriterQueue<CSocketError *> g_ErrorQueue;
moodycamel::ReaderWriterQueue<CSocketData *> g_DataQueue;

uv_loop_t *g_UV_Loop;
uv_thread_t g_UV_LoopThread;

uv_async_t g_UV_AsyncAdded;
moodycamel::ReaderWriterQueue<CAsyncAddJob> g_AsyncAddQueue;

bool g_Running;
AsyncSocket g_AsyncSocket;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_AsyncSocket);

CAsyncSocketContext *AsyncSocket::GetSocketInstanceByHandle(Handle_t handle)
{
	HandleSecurity sec;
	sec.pOwner = NULL;
	sec.pIdentity = myself->GetIdentity();

	CAsyncSocketContext *pSocketContext;

	if(handlesys->ReadHandle(handle, socketHandleType, &sec, (void **)&pSocketContext) != HandleError_None)
		return NULL;

	return pSocketContext;
}

void AsyncSocket::OnHandleDestroy(HandleType_t type, void *object)
{
	if(object != NULL)
	{
		CAsyncSocketContext *pSocketContext = (CAsyncSocketContext *)object;
		pSocketContext->m_Deleted = true;

		if(g_Running && (pSocketContext->m_pSocket || pSocketContext->m_pStream || pSocketContext->m_PendingCallback))
		{
			CAsyncAddJob Job;
			Job.CallbackFn = UV_DeleteAsyncContext;
			Job.pData = pSocketContext;
			g_AsyncAddQueue.enqueue(Job);

			uv_async_send(&g_UV_AsyncAdded);
		}
		else
		{
			delete pSocketContext;
		}
	}
}

void OnGameFrame(bool simulating)
{
	CSocketConnect *pConnect;
	while(g_ConnectQueue.try_dequeue(pConnect))
	{
		if(pConnect->pSocketContext->m_Server)
		{
			CAsyncSocketContext *pSocketContext = new CAsyncSocketContext(pConnect->pSocketContext->m_pContext);
			pSocketContext->m_Handle = handlesys->CreateHandle(g_AsyncSocket.socketHandleType, pSocketContext,
				pConnect->pSocketContext->m_pContext->GetIdentity(), myself->GetIdentity(), NULL);

			pSocketContext->m_pStream = pConnect->pClientSocket;
			pSocketContext->m_pStream->data = pSocketContext;

			pConnect->pSocketContext->OnConnect(pSocketContext);

			if(!pSocketContext->m_Deleted)
			{
				CAsyncAddJob Job;
				Job.CallbackFn = UV_StartRead;
				Job.pData = pSocketContext;
				g_AsyncAddQueue.enqueue(Job);

				uv_async_send(&g_UV_AsyncAdded);
			}
		}
		else
		{
			pConnect->pSocketContext->Connected();
		}

		free(pConnect);
	}

	CSocketData *pData;
	while(g_DataQueue.try_dequeue(pData))
	{
		pData->pSocketContext->OnData(pData->pBuffer, pData->BufferSize);

		free(pData->pBuffer);
		free(pData);
	}

	CSocketError *pError;
	while(g_ErrorQueue.try_dequeue(pError))
	{
		pError->pSocketContext->OnError(pError->Error);

		free(pError);
	}
}

// main event loop thread
void UV_EventLoop(void *data)
{
	uv_run(g_UV_Loop, UV_RUN_DEFAULT);
}

void UV_OnAsyncAdded(uv_async_t *pHandle)
{
	CAsyncAddJob Job;
	while(g_AsyncAddQueue.try_dequeue(Job))
	{
		uv_async_t *pAsync = (uv_async_t *)malloc(sizeof(uv_async_t));
		uv_async_init(g_UV_Loop, pAsync, Job.CallbackFn);
		pAsync->data = Job.pData;
		pAsync->close_cb = UV_FreeHandle;
		uv_async_send(pAsync);
	}
}

void UV_FreeHandle(uv_handle_t *handle)
{
	free(handle);
}

void UV_AllocBuffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
	buf->base = (char *)malloc(suggested_size);
	buf->len = suggested_size;
}

void UV_Quit(uv_async_t *pHandle)
{
	uv_close((uv_handle_t *)pHandle, pHandle->close_cb);

	uv_close((uv_handle_t *)&g_UV_AsyncAdded, NULL);

	uv_stop(g_UV_Loop);
}

void UV_DeleteAsyncContext(uv_async_t *pHandle)
{
	CAsyncSocketContext *pSocketContext = (CAsyncSocketContext *)pHandle->data;
	uv_close((uv_handle_t *)pHandle, pHandle->close_cb);

	if(pSocketContext->m_pStream)
	{
		uv_close((uv_handle_t *)pSocketContext->m_pStream, pSocketContext->m_pStream->close_cb);
		pSocketContext->m_pStream = NULL;
		pSocketContext->m_pSocket = NULL;
	}

	if(pSocketContext->m_pSocket)
	{
		uv_close((uv_handle_t *)pSocketContext->m_pSocket, pSocketContext->m_pSocket->close_cb);
		pSocketContext->m_pSocket = NULL;
	}

	delete pSocketContext;
}

void UV_PushError(CAsyncSocketContext *pSocketContext, int error)
{
	pSocketContext->m_PendingCallback = true;
	CSocketError *pError = (CSocketError *)malloc(sizeof(CSocketError));

	pError->pSocketContext = pSocketContext;
	pError->Error = error;

	g_ErrorQueue.enqueue(pError);
}

void UV_OnRead(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf)
{
	CAsyncSocketContext *pSocketContext = (CAsyncSocketContext *)client->data;
	if(pSocketContext->m_Deleted)
	{
		free(buf->base);
		uv_close((uv_handle_t *)client, client->close_cb);
		pSocketContext->m_pStream = NULL;
		pSocketContext->m_pSocket = NULL;
		return;
	}

	if(nread < 0)
	{
		// Connection closed
		free(buf->base);
		// But let the client disconnect.
		//uv_close((uv_handle_t *)client, client->close_cb);
		//pSocketContext->m_pStream = NULL;
		//pSocketContext->m_pSocket = NULL;

		UV_PushError(pSocketContext, nread);
		return;
	}

	pSocketContext->m_PendingCallback = true;

	char *data = (char *)malloc(sizeof(char) * (nread + 1));
	data[nread] = 0;
	strncpy(data, buf->base, nread);
	free(buf->base);

	CSocketData *pData = (CSocketData *)malloc(sizeof(CSocketData));
	pData->pSocketContext = pSocketContext;
	pData->pBuffer = data;
	pData->BufferSize = nread;

	g_DataQueue.enqueue(pData);
}

void UV_OnConnect(uv_connect_t *req, int status)
{
	CAsyncSocketContext *pSocketContext = (CAsyncSocketContext *)req->data;
	if(pSocketContext->m_Deleted)
	{
		free(req);
		uv_close((uv_handle_t *)req->handle, req->handle->close_cb);
		return;
	}

	if(status < 0)
	{
		free(req);
		UV_PushError(pSocketContext, status);
		return;
	}

	pSocketContext->m_PendingCallback = true;

	pSocketContext->m_pStream = req->handle;
	free(req);
	pSocketContext->m_pStream->data = pSocketContext;

	CSocketConnect *pConnect = (CSocketConnect *)malloc(sizeof(CSocketConnect));
	pConnect->pSocketContext = pSocketContext;
	pConnect->pClientSocket = pSocketContext->m_pStream;
	g_ConnectQueue.enqueue(pConnect);

	uv_read_start(pSocketContext->m_pStream, UV_AllocBuffer, UV_OnRead);
}

void UV_StartRead(uv_async_t *pHandle)
{
	CAsyncSocketContext *pSocketContext = (CAsyncSocketContext *)pHandle->data;
	uv_close((uv_handle_t *)pHandle, pHandle->close_cb);

	if(pSocketContext->m_Deleted || !pSocketContext->m_pStream)
		return;

	uv_read_start(pSocketContext->m_pStream, UV_AllocBuffer, UV_OnRead);
}

void UV_OnNewConnection(uv_stream_t *server, int status)
{
	// server context
	CAsyncSocketContext *pSocketContext = (CAsyncSocketContext *)server->data;
	if(pSocketContext->m_Deleted)
	{
		uv_close((uv_handle_t *)server, server->close_cb);
		return;
	}

	if(status < 0)
	{
		uv_close((uv_handle_t *)server, server->close_cb);
		//uv_close((uv_handle_t *)pSocketContext->m_pSocket, pSocketContext->m_pSocket->close_cb);
		UV_PushError(pSocketContext, status);
		return;
	}

	uv_tcp_t *pClientSocket = (uv_tcp_t *)malloc(sizeof(uv_tcp_t));
	uv_tcp_init(g_UV_Loop, pClientSocket);
	pClientSocket->close_cb = UV_FreeHandle;

	if(uv_accept((uv_stream_t *)pSocketContext->m_pSocket, (uv_stream_t *)pClientSocket) == 0)
	{
		pSocketContext->m_PendingCallback = true;
		CSocketConnect *pConnect = (CSocketConnect *)malloc(sizeof(CSocketConnect));
		pConnect->pSocketContext = pSocketContext;
		pConnect->pClientSocket = (uv_stream_t *)pClientSocket;
		g_ConnectQueue.enqueue(pConnect);
	}
	else
	{
		uv_close((uv_handle_t *)pClientSocket, pClientSocket->close_cb);
	}
}

void UV_OnAsyncResolved(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res)
{
	if(resolver->service != NULL)
		free(resolver->service);

	CAsyncSocketContext *pSocketContext = (CAsyncSocketContext *)resolver->data;
	if(pSocketContext->m_Deleted || pSocketContext->m_pSocket)
	{
		uv_freeaddrinfo(res);
		return;
	}

	if(status < 0)
	{
		pSocketContext->m_Pending = false;
		uv_freeaddrinfo(res);
		UV_PushError(pSocketContext, status);
		return;
	}

	uv_tcp_t *pSocket = (uv_tcp_t *)malloc(sizeof(uv_tcp_t));
	uv_tcp_init(g_UV_Loop, pSocket);
	pSocket->close_cb = UV_FreeHandle;
	pSocket->data = pSocketContext;

	pSocketContext->m_pSocket = pSocket;
	pSocketContext->m_Pending = false;

	if(pSocketContext->m_Server)
	{
		uv_tcp_bind(pSocket, (const struct sockaddr *)res->ai_addr, 0);

		int err = uv_listen((uv_stream_t *)pSocket, 32, UV_OnNewConnection);
		if(err)
		{
			uv_close((uv_handle_t *)pSocket, pSocket->close_cb);
			pSocketContext->m_pSocket = NULL;
			UV_PushError(pSocketContext, err);
		}
	}
	else
	{
		uv_connect_t *pConnectReq = (uv_connect_t *)malloc(sizeof(uv_connect_t));
		pConnectReq->data = pSocketContext;

		uv_tcp_connect(pConnectReq, pSocket, (const struct sockaddr *)res->ai_addr, UV_OnConnect);
	}

	uv_freeaddrinfo(res);
}

void UV_OnAsyncResolve(uv_async_t *pHandle)
{
	CAsyncSocketContext *pSocketContext = (CAsyncSocketContext *)pHandle->data;
	uv_close((uv_handle_t *)pHandle, pHandle->close_cb);

	if(pSocketContext->m_Deleted || pSocketContext->m_pSocket)
		return;

	pSocketContext->m_Resolver.data = pSocketContext;

	char *service = (char *)malloc(8);
	sprintf(service, "%d", pSocketContext->m_Port);

	struct addrinfo hints;
	hints.ai_family = PF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = 0;

	int err = uv_getaddrinfo(g_UV_Loop, &pSocketContext->m_Resolver, UV_OnAsyncResolved, pSocketContext->m_pHost, service, &hints);
	if(err)
	{
		if(service != NULL)
			free(service);
		UV_PushError(pSocketContext, err);
	}
}

void UV_OnAsyncWriteCleanup(uv_write_t *req, int status)
{
	CAsyncWrite *pWrite = (CAsyncWrite *)req->data;

	free(pWrite->pBuffer->base);
	free(pWrite->pBuffer);
	free(pWrite);
	free(req);
}

void UV_OnAsyncWrite(uv_async_t *handle)
{
	CAsyncWrite *pWrite = (CAsyncWrite *)handle->data;
	uv_close((uv_handle_t *)handle, handle->close_cb);

	if(pWrite == NULL || pWrite->pBuffer == NULL)
		return;

	if(pWrite->pSocketContext == NULL || pWrite->pSocketContext->m_pStream == NULL)
	{
		free(pWrite->pBuffer->base);
		free(pWrite->pBuffer);
		free(pWrite);
		return;
	}

	uv_write_t *req = (uv_write_t *)malloc(sizeof(uv_write_t));
	req->data = pWrite;

	uv_write(req, pWrite->pSocketContext->m_pStream, pWrite->pBuffer, 1, UV_OnAsyncWriteCleanup);
}

cell_t Native_AsyncSocket_Create(IPluginContext *pContext, const cell_t *params)
{
	CAsyncSocketContext *pSocketContext = new CAsyncSocketContext(pContext);

	pSocketContext->m_Handle = handlesys->CreateHandle(g_AsyncSocket.socketHandleType, pSocketContext,
		pContext->GetIdentity(), myself->GetIdentity(), NULL);

	return pSocketContext->m_Handle;
}

cell_t Native_AsyncSocket_Connect(IPluginContext *pContext, const cell_t *params)
{
	CAsyncSocketContext *pSocketContext = g_AsyncSocket.GetSocketInstanceByHandle(params[1]);

	if(pSocketContext == NULL)
		return pContext->ThrowNativeError("Invalid socket handle");

	if(params[3] < 0 || params[3] > 65535)
		return pContext->ThrowNativeError("Invalid port specified");

	if(pSocketContext->m_pSocket)
		return pContext->ThrowNativeError("Socket is already connected");

	if(pSocketContext->m_Pending)
		return pContext->ThrowNativeError("Socket is currently pending");

	char *address = NULL;
	pContext->LocalToString(params[2], &address);

	pSocketContext->m_pHost = strdup(address);
	pSocketContext->m_Port = params[3];
	pSocketContext->m_Server = false;
	pSocketContext->m_Pending = true;

	CAsyncAddJob Job;
	Job.CallbackFn = UV_OnAsyncResolve;
	Job.pData = pSocketContext;
	g_AsyncAddQueue.enqueue(Job);

	uv_async_send(&g_UV_AsyncAdded);

	return 1;
}

cell_t Native_AsyncSocket_Listen(IPluginContext *pContext, const cell_t *params)
{
	CAsyncSocketContext *pSocketContext = g_AsyncSocket.GetSocketInstanceByHandle(params[1]);

	if(pSocketContext == NULL)
		return pContext->ThrowNativeError("Invalid socket handle");

	if(params[3] < 0 || params[3] > 65535)
		return pContext->ThrowNativeError("Invalid port specified");

	if(pSocketContext->m_pSocket)
		return pContext->ThrowNativeError("Socket is already connected");

	if(pSocketContext->m_Pending)
		return pContext->ThrowNativeError("Socket is currently pending");

	char *address = NULL;
	pContext->LocalToString(params[2], &address);

	pSocketContext->m_pHost = strdup(address);
	pSocketContext->m_Port = params[3];
	pSocketContext->m_Server = true;
	pSocketContext->m_Pending = true;

	CAsyncAddJob Job;
	Job.CallbackFn = UV_OnAsyncResolve;
	Job.pData = pSocketContext;
	g_AsyncAddQueue.enqueue(Job);

	uv_async_send(&g_UV_AsyncAdded);

	return 1;
}

cell_t Native_AsyncSocket_Write(IPluginContext *pContext, const cell_t *params)
{
	CAsyncSocketContext *pSocketContext = g_AsyncSocket.GetSocketInstanceByHandle(params[1]);

	if(pSocketContext == NULL)
		return pContext->ThrowNativeError("Invalid socket handle");

	if(!pSocketContext->m_pStream)
		return pContext->ThrowNativeError("Socket is not connected");

	char *data = NULL;
	pContext->LocalToPhysAddr(params[2], (cell_t **)&data);

	uv_buf_t *buffer = (uv_buf_t *)malloc(sizeof(uv_buf_t));

	if(params[3] >= 0)
		buffer->len = params[3];
	else
		buffer->len = strlen(data);

	buffer->base = (char *)malloc(buffer->len + 1);
	memcpy(buffer->base, data, buffer->len + 1);
	buffer->base[buffer->len] = 0;

	CAsyncWrite *pWrite = (CAsyncWrite *)malloc(sizeof(CAsyncWrite));

	pWrite->pSocketContext = pSocketContext;
	pWrite->pBuffer = buffer;

	CAsyncAddJob Job;
	Job.CallbackFn = UV_OnAsyncWrite;
	Job.pData = pWrite;
	g_AsyncAddQueue.enqueue(Job);

	uv_async_send(&g_UV_AsyncAdded);

	return 1;
}

cell_t Native_AsyncSocket_SetConnectCallback(IPluginContext *pContext, const cell_t *params)
{
	CAsyncSocketContext *pSocketContext = g_AsyncSocket.GetSocketInstanceByHandle(params[1]);

	if(pSocketContext == NULL)
		return pContext->ThrowNativeError("Invalid socket handle");

	if(!pSocketContext->SetConnectCallback(params[2]))
		return pContext->ThrowNativeError("Invalid callback");

	return true;
}

cell_t Native_AsyncSocket_SetErrorCallback(IPluginContext *pContext, const cell_t *params)
{
	CAsyncSocketContext *pSocketContext = g_AsyncSocket.GetSocketInstanceByHandle(params[1]);

	if(pSocketContext == NULL)
		return pContext->ThrowNativeError("Invalid socket handle");

	if(!pSocketContext->SetErrorCallback(params[2]))
		return pContext->ThrowNativeError("Invalid callback");

	return true;
}

cell_t Native_AsyncSocket_SetDataCallback(IPluginContext *pContext, const cell_t *params)
{
	CAsyncSocketContext *pSocketContext = g_AsyncSocket.GetSocketInstanceByHandle(params[1]);

	if(pSocketContext == NULL)
		return pContext->ThrowNativeError("Invalid socket handle");

	if(!pSocketContext->SetDataCallback(params[2]))
		return pContext->ThrowNativeError("Invalid callback");

	return true;
}

// Sourcemod Plugin Events
bool AsyncSocket::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	g_Running = true;
	sharesys->AddNatives(myself, AsyncSocketNatives);
	sharesys->RegisterLibrary(myself, "AsyncSocket");

	socketHandleType = handlesys->CreateType("AsyncSocket", this, 0, NULL, NULL, myself->GetIdentity(), NULL);

	smutils->AddGameFrameHook(OnGameFrame);

	g_UV_Loop = uv_default_loop();

	uv_async_init(g_UV_Loop, &g_UV_AsyncAdded, UV_OnAsyncAdded);
	g_UV_AsyncAdded.close_cb = NULL;

	uv_thread_create(&g_UV_LoopThread, UV_EventLoop, NULL);

	return true;
}

void UV_OnWalk(uv_handle_t *pHandle, void *pArg)
{
	uv_close(pHandle, pHandle->close_cb);
}

void AsyncSocket::SDK_OnUnload()
{
	g_Running = false;
	handlesys->RemoveType(socketHandleType, myself->GetIdentity());

	CAsyncAddJob Job;
	Job.CallbackFn = UV_Quit;
	Job.pData = NULL;
	g_AsyncAddQueue.enqueue(Job);

	uv_async_send(&g_UV_AsyncAdded);

	uv_thread_join(&g_UV_LoopThread);

	uv_walk(g_UV_Loop, UV_OnWalk, NULL);

	uv_run(g_UV_Loop, UV_RUN_DEFAULT);

	uv_loop_close(g_UV_Loop);

	smutils->RemoveGameFrameHook(OnGameFrame);
}

const sp_nativeinfo_t AsyncSocketNatives[] = {
	{"AsyncSocket.AsyncSocket", Native_AsyncSocket_Create},
	{"AsyncSocket.Connect", Native_AsyncSocket_Connect},
	{"AsyncSocket.Listen", Native_AsyncSocket_Listen},
	{"AsyncSocket.Write", Native_AsyncSocket_Write},
	{"AsyncSocket.SetConnectCallback", Native_AsyncSocket_SetConnectCallback},
	{"AsyncSocket.SetErrorCallback", Native_AsyncSocket_SetErrorCallback},
	{"AsyncSocket.SetDataCallback", Native_AsyncSocket_SetDataCallback},
	{NULL, NULL}
};
