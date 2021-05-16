#include "hltvdirectorwrapper.h"

HLTVDirectorWrapper g_HLTVDirectorWrapper;

void HLTVDirectorWrapper::SetPVSEntity(int index)
{
	static int offset = -1;
	if (offset == -1 && !g_pGameConf->GetOffset("CHLTVDirector::m_iPVSEntity", &offset))
	{
		smutils->LogError(myself, "Failed to get CHLTVDirector::m_iPVSEntity offset.");
		return;
	}

	*(int *)((intptr_t)hltvdirector + offset) = index;
}

void HLTVDirectorWrapper::SetPVSOrigin(Vector pos)
{
	static int offset = -1;
	if (offset == -1 && !g_pGameConf->GetOffset("CHLTVDirector::m_vPVSOrigin", &offset))
	{
		smutils->LogError(myself, "Failed to get CHLTVDirector::m_vPVSOrigin offset.");
		return;
	}

	Vector *m_vPVSOrigin = (Vector *)((intptr_t)hltvdirector + offset);
	*m_vPVSOrigin = pos;
}

void HLTVDirectorWrapper::SetNextThinkTick(int tick)
{
	static int offset = -1;
	if (offset == -1 && !g_pGameConf->GetOffset("CHLTVDirector::m_nNextShotTick", &offset))
	{
		smutils->LogError(myself, "Failed to get CHLTVDirector::m_nNextShotTick offset.");
		return;
	}

	*(int *)((intptr_t)hltvdirector + offset) = tick;
}