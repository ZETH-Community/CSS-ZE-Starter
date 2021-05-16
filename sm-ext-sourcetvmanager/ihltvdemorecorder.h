#ifndef _INCLUDE_DEMORECORDER_H
#define _INCLUDE_DEMORECORDER_H


class CDemoFile;
class bf_read;
class ServerClass;

class IDemoRecorder
{
public:

	virtual CDemoFile *GetDemoFile() = 0;
	virtual int		GetRecordingTick(void) = 0;

	virtual void	StartRecording(const char *filename, bool bContinuously) = 0;
	virtual void	SetSignonState(int state) = 0;
	virtual bool	IsRecording(void) = 0;
	virtual void	PauseRecording(void) = 0;
	virtual void	ResumeRecording(void) = 0;
	virtual void	StopRecording(void) = 0;

	virtual void	RecordCommand(const char *cmdstring) = 0; 
	virtual void	RecordUserInput(int cmdnumber) = 0;
	virtual void	RecordMessages(bf_read &data, int bits) = 0;
	virtual void	RecordPacket(void) = 0;
	virtual void	RecordServerClasses(ServerClass *pClasses) = 0;
	virtual void	RecordStringTables(void) = 0;
#if SOURCE_ENGINE == SE_LEFT4DEAD || SOURCE_ENGINE == SE_LEFT4DEAD2
	virtual void	RecordCustomData(int, void const *, unsigned int) = 0;
#endif

	virtual void	ResetDemoInterpolation(void) = 0;
};
#endif
