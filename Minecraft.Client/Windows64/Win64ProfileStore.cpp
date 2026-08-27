#include "stdafx.h"

#ifdef _WINDOWS64

#include "Win64ProfileStore.h"
#include <stdio.h>

namespace Win64ProfileStore
{
	static const char	*PROFILE_FILE	= "profile.dat";
	static const DWORD	 PROFILE_MAGIC	= 0x504A344D;	// 'M4JP'
	static const DWORD	 PROFILE_VERSION= 1;
	static const int	 PROFILE_PADS	= 4;

	struct HEADER
	{
		DWORD dwMagic;
		DWORD dwVersion;
		DWORD dwPads;
		DWORD dwBlobSize;
	};

	static void	*s_pvData[PROFILE_PADS]	= { NULL, NULL, NULL, NULL };
	static int	 s_iBlobSize			= 0;
	static bool	 s_bLoaded[PROFILE_PADS]= { false, false, false, false };

	void Load(void **ppvData, int iBlobSize)
	{
		// Remember the buffers so Save() can write them without the caller
		// having to hand them over again.
		for(int i=0;i<PROFILE_PADS;i++)
		{
			s_pvData[i]=ppvData[i];
			s_bLoaded[i]=false;
		}
		s_iBlobSize=iBlobSize;

		FILE *pIn=NULL;
		if(fopen_s(&pIn,PROFILE_FILE,"rb")!=0 || pIn==NULL) return;

		HEADER header={0};
		if(fread(&header,sizeof(header),1,pIn)!=1)
		{
			fclose(pIn);
			return;
		}

		// A blob size mismatch means GAME_SETTINGS changed shape since the file
		// was written - the contents would be misinterpreted, so drop it and
		// keep the defaults. The next Save() overwrites it.
		if(header.dwMagic!=PROFILE_MAGIC ||
		   header.dwVersion!=PROFILE_VERSION ||
		   header.dwPads!=(DWORD)PROFILE_PADS ||
		   header.dwBlobSize!=(DWORD)iBlobSize)
		{
			fclose(pIn);
			return;
		}

		for(int i=0;i<PROFILE_PADS;i++)
		{
			if(s_pvData[i]==NULL) break;
			if(fread(s_pvData[i],iBlobSize,1,pIn)!=1) break;
			s_bLoaded[i]=true;
		}

		fclose(pIn);

		// bSettingsChanged is the first field of GAME_SETTINGS and is a
		// transient dirty flag, not a setting - a stale "true" from the file
		// would provoke a pointless write on the next check.
		for(int i=0;i<PROFILE_PADS;i++)
		{
			if(s_bLoaded[i]) *(bool *)s_pvData[i]=false;
		}
	}

	bool HasSavedProfile(int iPad)
	{
		if(iPad<0 || iPad>=PROFILE_PADS) return false;
		return s_bLoaded[iPad];
	}

	void Save()
	{
		if(s_iBlobSize<=0 || s_pvData[0]==NULL) return;

		FILE *pOut=NULL;
		if(fopen_s(&pOut,PROFILE_FILE,"wb")!=0 || pOut==NULL) return;

		HEADER header;
		header.dwMagic=PROFILE_MAGIC;
		header.dwVersion=PROFILE_VERSION;
		header.dwPads=PROFILE_PADS;
		header.dwBlobSize=(DWORD)s_iBlobSize;
		fwrite(&header,sizeof(header),1,pOut);

		for(int i=0;i<PROFILE_PADS;i++)
		{
			if(s_pvData[i]==NULL) break;
			fwrite(s_pvData[i],s_iBlobSize,1,pOut);
		}

		fclose(pOut);

		// Everything on disk is now current, so a later launch may use it.
		for(int i=0;i<PROFILE_PADS;i++)
		{
			if(s_pvData[i]!=NULL) s_bLoaded[i]=true;
		}
	}
}

#endif // _WINDOWS64
