#ifndef _INCLUDE_DEMORECORDER_H
#define _INCLUDE_DEMORECORDER_H


class bf_read;
class ServerClass;
class CGameInfo;

class IDemoRecorder
{
public:

	virtual void	SetSignonState(int state) = 0;
	virtual void	RecordServerClasses(ServerClass *pClasses) = 0;
	virtual void	RecordMessages(bf_read &data, int bits) = 0;
	virtual void	RecordPacket(void) = 0;
	virtual void	RecordCommand(const char *cmdstring) = 0; 
	virtual void	RecordUserInput(int cmdnumber) = 0;
	virtual void	RecordCustomData(int, void const *, unsigned int) = 0;
	virtual void	ResetDemoInterpolation(void) = 0;
	virtual void	StartRecording(const char *filename, bool bContinuously) = 0;
	virtual void	StopRecording(CGameInfo const *info) = 0;
	virtual bool	IsRecording(void) = 0;
	virtual int		GetRecordingTick(void) = 0;
};
#endif
