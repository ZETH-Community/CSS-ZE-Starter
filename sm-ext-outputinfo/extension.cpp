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

#include <amtl/am-string.h>
#include "extension.h"

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

Outputinfo g_Outputinfo;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_Outputinfo);

IGameConfig *g_pGameConf = NULL;

#include <isaverestore.h>
#include <variant_t.h>

struct varianthax_t
{
	union
	{
		bool bVal;
		string_t iszVal;
		int iVal;
		float flVal;
		float vecVal[3];
		color32 rgbaVal;
	};
	CHandle<CBaseEntity> eVal; // this can't be in the union because it has a constructor.

	fieldtype_t fieldType;
};

class CEventAction
{
public:
	CEventAction( const char *ActionData = NULL );

	string_t m_iTarget; // name of the entity(s) to cause the action in
	string_t m_iTargetInput; // the name of the action to fire
	string_t m_iParameter; // parameter to send, 0 if none
	float m_flDelay; // the number of seconds to wait before firing the action
	int m_nTimesToFire; // The number of times to fire this event, or EVENT_FIRE_ALWAYS (-1).

	int m_iIDStamp;	// unique identifier stamp

	static int s_iNextIDStamp;

	CEventAction *m_pNext;
	DECLARE_SIMPLE_DATADESC();

#ifdef PLATFORM_WINDOWS
	static int *s_pBlocksAllocated;
	static void **s_ppHeadOfFreeList;
#else
	static void (*s_pOperatorDeleteFunc)(void *pMem);
#endif

	static void operator delete(void *pMem);
};

#ifdef PLATFORM_WINDOWS
	int *CEventAction::s_pBlocksAllocated;
	void **CEventAction::s_ppHeadOfFreeList;
#else
	void (*CEventAction::s_pOperatorDeleteFunc)(void *pMem);
#endif

void CEventAction::operator delete(void *pMem)
{
#ifdef PLATFORM_WINDOWS
	(*s_pBlocksAllocated)--;

	// make the block point to the first item in the list
	*((void **)pMem) = *s_ppHeadOfFreeList;

	// the list head is now the new block
	*s_ppHeadOfFreeList = pMem;
#else
	s_pOperatorDeleteFunc(pMem);
#endif
}

class CBaseEntityOutput
{
public:
	varianthax_t m_Value;
	CEventAction *m_ActionList;
	DECLARE_SIMPLE_DATADESC();

	int NumberOfElements(void);
	CEventAction *GetElement(int Index);
	int DeleteElement(int Index);
	int DeleteAllElements(void);
};

int CBaseEntityOutput::NumberOfElements(void)
{
	int Count = 0;
	for(CEventAction *ev = m_ActionList; ev != NULL; ev = ev->m_pNext)
		Count++;

	return Count;
}

CEventAction *CBaseEntityOutput::GetElement(int Index)
{
	int Count = 0;
	for(CEventAction *ev = m_ActionList; ev != NULL; ev = ev->m_pNext)
	{
		if(Count == Index)
			return ev;

		Count++;
	}

	return NULL;
}

int CBaseEntityOutput::DeleteElement(int Index)
{
	CEventAction *pPrevEvent = NULL;
	CEventAction *pEvent = NULL;

	int Count = 0;
	for(CEventAction *ev = m_ActionList; ev != NULL; ev = ev->m_pNext)
	{
		if(Count == Index)
		{
			pEvent = ev;
			break;
		}
		pPrevEvent = ev;
		Count++;
	}

	if(pEvent == NULL)
		return 0;

	if(pPrevEvent != NULL)
		pPrevEvent->m_pNext = pEvent->m_pNext;
	else
		m_ActionList = pEvent->m_pNext;

	delete pEvent;
	return 1;
}

int CBaseEntityOutput::DeleteAllElements(void)
{
	// walk front to back, deleting as we go. We needn't fix up pointers because
	// EVERYTHING will die.

	int Count = 0;
	CEventAction *pNext = m_ActionList;
	// wipe out the head
	m_ActionList = NULL;
	while(pNext)
	{
		CEventAction *pStrikeThis = pNext;
		pNext = pNext->m_pNext;
		delete pStrikeThis;
		Count++;
	}

	return Count;
}

inline int GetDataMapOffset(CBaseEntity *pEnt, const char *pName, typedescription_t **ppTypeDesc=NULL)
{
	datamap_t *pMap = gamehelpers->GetDataMap(pEnt);
	if(!pMap)
		return -1;

	typedescription_t *pTypeDesc = gamehelpers->FindInDataMap(pMap, pName);
	if(pTypeDesc == NULL)
		return -1;

	if(ppTypeDesc)
		*ppTypeDesc = pTypeDesc;

#if SOURCE_ENGINE >= SE_LEFT4DEAD
	return pTypeDesc->fieldOffset;
#else
	return pTypeDesc->fieldOffset[TD_OFFSET_NORMAL];
#endif
}

inline CBaseEntityOutput *GetOutput(CBaseEntity *pEntity, const char *pOutput, typedescription_t **ppTypeDesc=NULL)
{
	typedescription_t *pTypeDesc = NULL;
	int Offset = GetDataMapOffset(pEntity, pOutput, &pTypeDesc);

	if(ppTypeDesc)
		*ppTypeDesc = pTypeDesc;

	if(Offset == -1)
		return NULL;

	if(pTypeDesc->fieldType != FIELD_CUSTOM)
		return NULL;

	if(!(pTypeDesc->flags & FTYPEDESC_OUTPUT))
		return NULL;

	return (CBaseEntityOutput *)((intptr_t)pEntity + Offset);
}

cell_t GetOutputCount(IPluginContext *pContext, const cell_t *params)
{
	char *pOutput;
	pContext->LocalToString(params[2], &pOutput);

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(gamehelpers->IndexToReference(params[1]));
	if(!pEntity)
		return -1;

	CBaseEntityOutput *pEntityOutput = GetOutput(pEntity, pOutput);
	if(pEntityOutput == NULL)
		return -1;

	return pEntityOutput->NumberOfElements();
}

cell_t GetOutputTarget(IPluginContext *pContext, const cell_t *params)
{
	char *pOutput;
	pContext->LocalToString(params[2], &pOutput);

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(gamehelpers->IndexToReference(params[1]));
	if(!pEntity)
		return 0;

	CBaseEntityOutput *pEntityOutput = GetOutput(pEntity, pOutput);
	if(pEntityOutput == NULL || pEntityOutput->m_ActionList == NULL)
		return 0;

	CEventAction *pAction = pEntityOutput->GetElement(params[3]);
	if(!pAction)
		return 0;

	size_t Length;
	pContext->StringToLocalUTF8(params[4], params[5], pAction->m_iTarget.ToCStr(), &Length);

	return Length;
}

cell_t GetOutputTargetInput(IPluginContext *pContext, const cell_t *params)
{
	char *pOutput;
	pContext->LocalToString(params[2], &pOutput);

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(gamehelpers->IndexToReference(params[1]));
	if(!pEntity)
		return 0;

	CBaseEntityOutput *pEntityOutput = GetOutput(pEntity, pOutput);
	if(pEntityOutput == NULL || pEntityOutput->m_ActionList == NULL)
		return 0;

	CEventAction *pAction = pEntityOutput->GetElement(params[3]);
	if(!pAction)
		return 0;

	size_t Length;
	pContext->StringToLocalUTF8(params[4], params[5], pAction->m_iTargetInput.ToCStr(), &Length);

	return Length;
}

cell_t GetOutputParameter(IPluginContext *pContext, const cell_t *params)
{
	char *pOutput;
	pContext->LocalToString(params[2], &pOutput);

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(gamehelpers->IndexToReference(params[1]));
	if(!pEntity)
		return 0;

	CBaseEntityOutput *pEntityOutput = GetOutput(pEntity, pOutput);
	if(pEntityOutput == NULL || pEntityOutput->m_ActionList == NULL)
		return 0;

	CEventAction *pAction = pEntityOutput->GetElement(params[3]);
	if(!pAction)
		return 0;

	size_t Length;
	pContext->StringToLocalUTF8(params[4], params[5], pAction->m_iParameter.ToCStr(), &Length);

	return Length;
}

cell_t GetOutputDelay(IPluginContext *pContext, const cell_t *params)
{
	char *pOutput;
	pContext->LocalToString(params[2], &pOutput);

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(gamehelpers->IndexToReference(params[1]));
	if(!pEntity)
		return 0;

	CBaseEntityOutput *pEntityOutput = GetOutput(pEntity, pOutput);
	if(pEntityOutput == NULL || pEntityOutput->m_ActionList == NULL)
		return -1;

	CEventAction *pAction = pEntityOutput->GetElement(params[3]);
	if(!pAction)
		return -1;

	return *(cell_t *)&pAction->m_flDelay;
}

cell_t GetOutputFormatted(IPluginContext *pContext, const cell_t *params)
{
	char *pOutput;
	pContext->LocalToString(params[2], &pOutput);

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(gamehelpers->IndexToReference(params[1]));
	if(!pEntity)
		return 0;

	CBaseEntityOutput *pEntityOutput = GetOutput(pEntity, pOutput);
	if(pEntityOutput == NULL || pEntityOutput->m_ActionList == NULL)
		return 0;

	CEventAction *pAction = pEntityOutput->GetElement(params[3]);
	if(!pAction)
		return 0;

	char aBuffer[1024];
	ke::SafeSprintf(aBuffer, sizeof(aBuffer), "%s,%s,%s,%g,%d",
		pAction->m_iTarget.ToCStr(),
		pAction->m_iTargetInput.ToCStr(),
		pAction->m_iParameter.ToCStr(),
		pAction->m_flDelay,
		pAction->m_nTimesToFire);

	size_t Length;
	pContext->StringToLocalUTF8(params[4], params[5], aBuffer, &Length);

	return Length;
}

cell_t GetOutputValue(IPluginContext *pContext, const cell_t *params)
{
	char *pOutput;
	pContext->LocalToString(params[2], &pOutput);

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(gamehelpers->IndexToReference(params[1]));
	if(!pEntity)
		return -1;

	CBaseEntityOutput *pEntityOutput = GetOutput(pEntity, pOutput);
	if(pEntityOutput == NULL)
		return -1;

	switch(pEntityOutput->m_Value.fieldType)
	{
	case FIELD_TICK:
	case FIELD_MODELINDEX:
	case FIELD_MATERIALINDEX:
	case FIELD_INTEGER:
	case FIELD_COLOR32:
	case FIELD_SHORT:
	case FIELD_CHARACTER:
	case FIELD_BOOLEAN:
		break;
	default:
		return pContext->ThrowNativeError("%s value is not an integer (%d)", pOutput, pEntityOutput->m_Value.fieldType);
	}

	return (cell_t)pEntityOutput->m_Value.iVal;
}

cell_t GetOutputValueFloat(IPluginContext *pContext, const cell_t *params)
{
	char *pOutput;
	pContext->LocalToString(params[2], &pOutput);

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(gamehelpers->IndexToReference(params[1]));
	if(!pEntity)
		return -1;

	CBaseEntityOutput *pEntityOutput = GetOutput(pEntity, pOutput);
	if(pEntityOutput == NULL)
		return -1;

	switch(pEntityOutput->m_Value.fieldType)
	{
	case FIELD_FLOAT:
	case FIELD_TIME:
		break;
	default:
		return pContext->ThrowNativeError("%s value is not a float (%d)", pOutput, pEntityOutput->m_Value.fieldType);
	}

	return sp_ftoc((cell_t)pEntityOutput->m_Value.flVal);
}

cell_t GetOutputValueString(IPluginContext *pContext, const cell_t *params)
{
	char *pOutput;
	pContext->LocalToString(params[2], &pOutput);

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(gamehelpers->IndexToReference(params[1]));
	if(!pEntity)
		return -1;

	CBaseEntityOutput *pEntityOutput = GetOutput(pEntity, pOutput);
	if(pEntityOutput == NULL)
		return -1;

	switch(pEntityOutput->m_Value.fieldType)
	{
	case FIELD_CHARACTER:
	case FIELD_STRING:
	case FIELD_MODELNAME:
	case FIELD_SOUNDNAME:
		break;
	default:
		return pContext->ThrowNativeError("%s value is not a string (%d)", pOutput, pEntityOutput->m_Value.fieldType);
	}

	size_t len;
	pContext->StringToLocalUTF8(params[3], params[4], pEntityOutput->m_Value.iszVal.ToCStr(), &len);

	return len;
}

cell_t GetOutputValueVector(IPluginContext *pContext, const cell_t *params)
{
	char *pOutput;
	pContext->LocalToString(params[2], &pOutput);

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(gamehelpers->IndexToReference(params[1]));
	if(!pEntity)
		return -1;

	CBaseEntityOutput *pEntityOutput = GetOutput(pEntity, pOutput);
	if(pEntityOutput == NULL)
		return -1;

	switch(pEntityOutput->m_Value.fieldType)
	{
	case FIELD_FLOAT:
	case FIELD_TIME:
		break;
	default:
		return pContext->ThrowNativeError("%s value is not a float (%d)", pOutput, pEntityOutput->m_Value.fieldType);
	}

	cell_t *vec;
	pContext->LocalToPhysAddr(params[3], &vec);

	vec[0] = sp_ftoc(pEntityOutput->m_Value.vecVal[0]);
	vec[1] = sp_ftoc(pEntityOutput->m_Value.vecVal[1]);
	vec[2] = sp_ftoc(pEntityOutput->m_Value.vecVal[2]);

	return 1;
}

cell_t FindOutput(IPluginContext *pContext, const cell_t *params)
{
	char *pOutput;
	pContext->LocalToString(params[2], &pOutput);

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(gamehelpers->IndexToReference(params[1]));
	if(!pEntity)
		return -1;

	CBaseEntityOutput *pEntityOutput = GetOutput(pEntity, pOutput);
	if(pEntityOutput == NULL || pEntityOutput->m_ActionList == NULL)
		return -1;

	char *piTarget;
	pContext->LocalToStringNULL(params[4], &piTarget);
	char *piTargetInput;
	pContext->LocalToStringNULL(params[5], &piTargetInput);
	char *piParameter;
	pContext->LocalToStringNULL(params[6], &piParameter);
	float flDelay = *(float *)&params[7];
	cell_t nTimesToFire = params[8];

	int StartCount = params[3];
	int Count = 0;
	for(CEventAction *ev = pEntityOutput->m_ActionList; ev != NULL; ev = ev->m_pNext)
	{
		Count++;
		if(StartCount > 0)
		{
			StartCount--;
			continue;
		}

		if(piTarget != NULL && strcmp(ev->m_iTarget.ToCStr(), piTarget) != 0)
			continue;

		if(piTargetInput != NULL && strcmp(ev->m_iTargetInput.ToCStr(), piTargetInput) != 0)
			continue;

		if(piParameter != NULL && strcmp(ev->m_iParameter.ToCStr(), piParameter) != 0)
			continue;

		if(flDelay >= 0 && flDelay != ev->m_flDelay)
			continue;

		if(nTimesToFire != 0 && nTimesToFire != ev->m_nTimesToFire)
			continue;

		return Count - 1;
	}

	return -1;
}

cell_t DeleteOutput(IPluginContext *pContext, const cell_t *params)
{
	char *pOutput;
	pContext->LocalToString(params[2], &pOutput);

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(gamehelpers->IndexToReference(params[1]));
	if(!pEntity)
		return -1;

	CBaseEntityOutput *pEntityOutput = GetOutput(pEntity, pOutput);
	if(pEntityOutput == NULL || pEntityOutput->m_ActionList == NULL)
		return -1;

	return pEntityOutput->DeleteElement(params[3]);
}

cell_t DeleteAllOutputs(IPluginContext *pContext, const cell_t *params)
{
	char *pOutput;
	pContext->LocalToString(params[2], &pOutput);

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(gamehelpers->IndexToReference(params[1]));
	if(!pEntity)
		return -1;

	CBaseEntityOutput *pEntityOutput = GetOutput(pEntity, pOutput);
	if(pEntityOutput == NULL || pEntityOutput->m_ActionList == NULL)
		return -1;

	return pEntityOutput->DeleteAllElements();
}

cell_t GetOutputNames(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(gamehelpers->IndexToReference(params[1]));
	if(!pEntity)
		return -1;

	datamap_t *pMap = gamehelpers->GetDataMap(pEntity);
	if(!pMap)
		return -1;

	for(int count = 0; pMap != NULL; pMap = pMap->baseMap)
	{
		for(int i = 0; i < pMap->dataNumFields; i++)
		{
			typedescription_t *pTypeDesc = &pMap->dataDesc[i];

			if(pTypeDesc->fieldType != FIELD_CUSTOM)
				continue;

			if(!(pTypeDesc->flags & FTYPEDESC_OUTPUT))
				continue;

			if(params[2] == count)
			{
				size_t len;
				pContext->StringToLocalUTF8(params[3], params[4], pTypeDesc->fieldName, &len);
				return len;
			}

			count++;
		}
	}

	return -1;
}

const sp_nativeinfo_t MyNatives[] =
{
	{ "GetOutputCount", GetOutputCount },
	{ "GetOutputTarget", GetOutputTarget },
	{ "GetOutputTargetInput", GetOutputTargetInput },
	{ "GetOutputParameter", GetOutputParameter },
	{ "GetOutputDelay", GetOutputDelay },
	{ "GetOutputFormatted", GetOutputFormatted },
	{ "GetOutputValue", GetOutputValue },
	{ "GetOutputValueFloat", GetOutputValueFloat },
	{ "GetOutputValueString", GetOutputValueString },
	{ "GetOutputValueVector", GetOutputValueVector },
	{ "FindOutput", FindOutput },
	{ "DeleteOutput", DeleteOutput },
	{ "DeleteAllOutputs", DeleteAllOutputs },
	{ "GetOutputNames", GetOutputNames },
	{ NULL, NULL },
};

bool Outputinfo::SDK_OnLoad(char *error, size_t maxlen, bool late)
{
	char conf_error[255] = "";
	if(!gameconfs->LoadGameConfigFile("outputinfo", &g_pGameConf, conf_error, sizeof(conf_error)))
	{
		if(conf_error[0])
		{
			snprintf(error, maxlen, "Could not read outputinfo.txt: %s\n", conf_error);
		}
		return false;
	}

#ifdef PLATFORM_WINDOWS
	char path[PLATFORM_MAX_PATH];
	g_pSM->BuildPath(Path_Game, path, sizeof(path), "bin/server.dll");

	HMODULE hModule = GetModuleHandle(path);

	uintptr_t pCode = (uintptr_t)memutils->FindPattern(hModule,
		"\x8B\x4C\x24\x28\x89\x41\x14\x8B\x4F\x18\xA1****\xFF\x0D****\x89\x07\x89\x3D****\x8B\xF9\xEB\x07",
		33);

	if(!pCode)
	{
		snprintf(error, maxlen, "Failed to find windows signature.\n");
		return false;
	}

	uintptr_t ppHeadOfFreeList = *(uintptr_t *)(pCode + 11);
	uintptr_t pBlocksAllocated = *(uintptr_t *)(pCode + 17);

	CEventAction::s_pBlocksAllocated = (int *)pBlocksAllocated;
	CEventAction::s_ppHeadOfFreeList = (void **)ppHeadOfFreeList;
#else
	if(!g_pGameConf->GetMemSig("CEventAction__operator_delete", (void **)(&CEventAction::s_pOperatorDeleteFunc)) || !CEventAction::s_pOperatorDeleteFunc)
	{
		snprintf(error, maxlen, "Failed to find CEventAction__operator_delete function.\n");
		return false;
	}
#endif

	return true;
}

void Outputinfo::SDK_OnUnload()
{
	gameconfs->CloseGameConfigFile(g_pGameConf);
}

void Outputinfo::SDK_OnAllLoaded()
{
	sharesys->AddNatives(myself, MyNatives);
}
