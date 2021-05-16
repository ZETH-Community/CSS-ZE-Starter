#if defined _OutputInfo_Included
 #endinput
#endif
#define _OutputInfo_Included

native int GetOutputCount(int Entity, const char[] sOutput);

native int GetOutputTarget(int Entity, const char[] sOutput, int Index, char[] sTarget, int MaxLen);
native int GetOutputTargetInput(int Entity, const char[] sOutput, int Index, char[] sTargetInput, int MaxLen);
native int GetOutputParameter(int Entity, const char[] sOutput, int Index, char[] sParameter, int MaxLen);
native float GetOutputDelay(int Entity, const char[] sOutput, int Index);

native int GetOutputFormatted(int Entity, const char[] sOutput, int Index, char[] sFormatted, int MaxLen);

native int GetOutputValue(int Entity, const char[] sOutput);
native float GetOutputValueFloat(int Entity, const char[] sOutput);
native int GetOutputValueString(int Entity, const char[] sOutput, char[] sValue, int MaxLen);
native bool GetOutputValueVector(int Entity, const char[] sOutput, float afVec[3]);

native int FindOutput(int Entity, const char[] sOutput, int StartIndex,
					  const char[] sTarget = NULL_STRING, // or NULL_STRING to ignore
					  const char[] sTargetInput = NULL_STRING, // or NULL_STRING to ignore
					  const char[] sParameter = NULL_STRING, // or NULL_STRING to ignore
					  float fDelay = -1.0, // or -1.0 to ignore
					  int TimesToFire = 0 // or 0 to ignore
					  );

native int DeleteOutput(int Entity, const char[] sOutput, int Index);
native int DeleteAllOutputs(int Entity, const char[] sOutput);

native int GetOutputNames(int Entity, int Index, const char[] sOutput, int MaxLen);

/**
 * Do not edit below this line!
 */
public Extension __ext_outputinfo =
{
	name = "OutputInfo",
	file = "outputinfo.ext",
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
public __ext_outputinfo_SetNTVOptional()
{
	MarkNativeAsOptional("GetOutputCount");
	MarkNativeAsOptional("GetOutputTarget");
	MarkNativeAsOptional("GetOutputTargetInput");
	MarkNativeAsOptional("GetOutputParameter");
	MarkNativeAsOptional("GetOutputDelay");
	MarkNativeAsOptional("GetOutputFormatted");
	MarkNativeAsOptional("FindOutput");
	MarkNativeAsOptional("DeleteOutput");
	MarkNativeAsOptional("DeleteAllOutputs");
	MarkNativeAsOptional("GetOutputNames");
}
#endif
