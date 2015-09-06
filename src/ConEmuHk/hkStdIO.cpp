﻿
/*
Copyright (c) 2015 Maximus5
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef _DEBUG
	#define DebugString(x) //OutputDebugString(x)
#else
	#define DebugString(x) //OutputDebugString(x)
#endif

#include "../common/common.hpp"
#include "../common/ConsoleAnnotation.h"

#include "Ansi.h"
#include "ConEmuHooks.h"
#include "hkConsoleInput.h"
#include "hkConsoleOutput.h"
#include "hkStdIO.h"
#include "MainThread.h"
#include "SetHook.h"

/* **************** */

extern HANDLE ghConsoleCursorChanged;

/* **************** */

#ifdef _DEBUG
// Only for input_bug search purposes in Debug builds
const LONG gn_LogReadCharsMax = 4096; // must be power of 2
wchar_t gs_LogReadChars[gn_LogReadCharsMax*2+1] = L""; // "+1" для ASCIIZ
LONG gn_LogReadChars = -1;
#endif

HANDLE ghLastConInHandle = NULL;
HANDLE ghLastNotConInHandle = NULL;

/* **************** */


HANDLE WINAPI OnCreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
	//typedef HANDLE (WINAPI* OnCreateFileW_t)(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
	ORIGINALFAST(CreateFileW);
	HANDLE h;

	h = F(CreateFileW)(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
	DWORD nLastErr = GetLastError();

	DebugString(L"OnCreateFileW executed\n");

	// Just a check for "string" validity
	if (lpFileName && (((DWORD_PTR)lpFileName) & ~0xFFFF)
		// CON output is opening with following flags
		&& (dwDesiredAccess & GENERIC_WRITE)
		&& ((dwShareMode & (FILE_SHARE_READ|FILE_SHARE_WRITE)) == (FILE_SHARE_READ|FILE_SHARE_WRITE))
		)
	{
		DEBUGTEST(HANDLE hStd = GetStdHandle(STD_OUTPUT_HANDLE));
		if (lstrcmpi(lpFileName, L"CON") == 0)
		{
			CEAnsi::ghLastConOut = h;
		}
		DebugString(L"OnCreateFileW checked\n");
	}

	SetLastError(nLastErr);
	return h;
}


BOOL WINAPI OnCloseHandle(HANDLE hObject)
{
	//typedef BOOL (WINAPI* OnCloseHandle_t)(HANDLE hObject);
	ORIGINALFAST(CloseHandle);
	BOOL lbRc = FALSE;

	LPHANDLE hh[] = {
		&CEAnsi::ghLastAnsiCapable,
		&CEAnsi::ghLastAnsiNotCapable,
		&CEAnsi::ghLastConOut,
		&ghLastConInHandle,
		&ghLastNotConInHandle,
		NULL
	};

	if (hObject)
	{
		for (INT_PTR i = 0; hh[i]; i++)
		{
			if (hh[i] && (*(hh[i]) == hObject))
			{
				*(hh[i]) = NULL;
			}
		}
	}

	if (gpAnnotationHeader && (hObject == (HANDLE)gpAnnotationHeader))
	{
		lbRc = TRUE;
	}
	else
	{
		lbRc = F(CloseHandle)(hObject);
	}

	if (ghSkipSetThreadContextForThread == hObject)
		ghSkipSetThreadContextForThread = NULL;

	return lbRc;
}


#ifdef _DEBUG
HANDLE WINAPI OnCreateNamedPipeW(LPCWSTR lpName, DWORD dwOpenMode, DWORD dwPipeMode, DWORD nMaxInstances,DWORD nOutBufferSize, DWORD nInBufferSize, DWORD nDefaultTimeOut,LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
	//typedef HANDLE(WINAPI* OnCreateNamedPipeW_t)(LPCWSTR lpName, DWORD dwOpenMode, DWORD dwPipeMode, DWORD nMaxInstances,DWORD nOutBufferSize, DWORD nInBufferSize, DWORD nDefaultTimeOut,LPSECURITY_ATTRIBUTES lpSecurityAttributes);
	ORIGINALFAST(CreateNamedPipeW);

	#ifdef _DEBUG
	if (!lpName || !*lpName)
	{
		_ASSERTE(lpName && *lpName);
	}
	else
	{
		int nLen = lstrlen(lpName)+64;
		wchar_t* psz = (wchar_t*)malloc(nLen*sizeof(*psz));
		if (psz)
		{
			msprintf(psz, nLen, L"CreateNamedPipeW(%s)\n", lpName);
			DebugString(psz);
			free(psz);
		}
	}
	#endif

	HANDLE hPipe = F(CreateNamedPipeW)(lpName, dwOpenMode, dwPipeMode, nMaxInstances, nOutBufferSize, nInBufferSize, nDefaultTimeOut, lpSecurityAttributes);
	return hPipe;
} // OnCreateNamedPipeW
#endif


BOOL WINAPI OnReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
	//typedef BOOL (WINAPI* OnReadFile_t)(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
	SUPPRESSORIGINALSHOWCALL;
	ORIGINAL(ReadFile);
	BOOL lbRc = FALSE;

	bool bConIn = false;

	DWORD nPrevErr = GetLastError();

	if (hFile == ghLastConInHandle)
	{
		bConIn = true;
	}
	else if (hFile != ghLastNotConInHandle)
	{
		DWORD nMode = 0;
		BOOL lbConRc = GetConsoleMode(hFile, &nMode);
		if (lbConRc
			&& ((nMode & (ENABLE_LINE_INPUT & ENABLE_ECHO_INPUT)) == (ENABLE_LINE_INPUT & ENABLE_ECHO_INPUT)))
		{
			bConIn = true;
			ghLastConInHandle = hFile;
		}
		else
		{
			ghLastNotConInHandle = hFile;
		}
	}

	if (bConIn)
		OnReadConsoleStart(false, hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, NULL);

	SetLastError(nPrevErr);
	lbRc = F(ReadFile)(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
	DWORD nErr = GetLastError();

	if (bConIn)
	{
		OnReadConsoleEnd(lbRc, false, hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, NULL);

		#ifdef _DEBUG
		// Only for input_bug search purposes in Debug builds
		const char* pszChr = (const char*)lpBuffer;
		int cchRead = *lpNumberOfBytesRead;
		wchar_t* pszDbgCurChars = NULL;
		while ((cchRead--) > 0)
		{
			LONG idx = (InterlockedIncrement(&gn_LogReadChars) & (gn_LogReadCharsMax-1))*2;
			if (!pszDbgCurChars) pszDbgCurChars = gs_LogReadChars+idx;
			gs_LogReadChars[idx++] = L'|';
			gs_LogReadChars[idx++] = *pszChr ? *pszChr : L'.';
			gs_LogReadChars[idx] = 0;
			pszChr++;
		}
		#endif

		SetLastError(nErr);
	}

	return lbRc;
}


BOOL WINAPI OnWriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
	//typedef BOOL (WINAPI* OnWriteFile_t)(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
	ORIGINALFAST(WriteFile);
	BOOL lbRc = FALSE;
	//DWORD nDBCSCP = 0;

	FIRST_ANSI_CALL((const BYTE*)lpBuffer, nNumberOfBytesToWrite);

	//if (gpStartEnv->bIsDbcs)
	//{
	//	nDBCSCP = GetConsoleOutputCP();
	//}

	if (lpBuffer && nNumberOfBytesToWrite && hFile && CEAnsi::IsAnsiCapable(hFile))
		lbRc = OnWriteConsoleA(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, NULL);
	else
		lbRc = F(WriteFile)(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);

	return lbRc;
}


HANDLE WINAPI OnOpenFileMappingW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName)
{
	//typedef HANDLE (WINAPI* OnOpenFileMappingW_t)(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName);
	ORIGINALFAST(OpenFileMappingW);
	HANDLE hRc = FALSE;

	extern BOOL gbTrueColorServerRequested;
	if (ghConEmuWndDC && lpName && *lpName
		&& !gbTrueColorServerRequested)
	{
		/**
		* Share name to search for
		* Two replacements:
		*   %d sizeof(AnnotationInfo) - compatibility/versioning field.
		*   %d console window handle
		*/
		wchar_t szTrueColorMap[64];
		// #define  L"Console_annotationInfo_%x_%x"
		msprintf(szTrueColorMap, countof(szTrueColorMap), AnnotationShareName, (DWORD)sizeof(AnnotationInfo), ghConEmuWndDC);
		// При попытке открыть мэппинг для TrueColor - перейти в режим локального сервера
		if (lstrcmpi(lpName, szTrueColorMap) == 0)
		{
			RequestLocalServerParm Parm = {(DWORD)sizeof(Parm), slsf_RequestTrueColor|slsf_GetCursorEvent};
			if (RequestLocalServer(&Parm) == 0)
			{
				if (Parm.pAnnotation)
				{
					gpAnnotationHeader = Parm.pAnnotation;
					hRc = (HANDLE)Parm.pAnnotation;
					goto wrap;
				}
				else
				{
					WARNING("Перенести обработку AnnotationShareName в хуки");
				}

				if (ghConsoleCursorChanged && (ghConsoleCursorChanged != Parm.hCursorChangeEvent))
					CloseHandle(ghConsoleCursorChanged);
				ghConsoleCursorChanged = Parm.hCursorChangeEvent;
			}
		}
	}

	hRc = F(OpenFileMappingW)(dwDesiredAccess, bInheritHandle, lpName);

wrap:
	return hRc;
}


LPVOID WINAPI OnMapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap)
{
	//typedef LPVOID (WINAPI* OnMapViewOfFile_t)(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap);
	ORIGINALFAST(MapViewOfFile);
	LPVOID ptr = NULL;

	if (gpAnnotationHeader && (hFileMappingObject == (HANDLE)gpAnnotationHeader))
	{
		_ASSERTE(!dwFileOffsetHigh && !dwFileOffsetLow && !dwNumberOfBytesToMap);
		ptr = gpAnnotationHeader;
	}
	else
	{
		ptr = F(MapViewOfFile)(hFileMappingObject, dwDesiredAccess, dwFileOffsetHigh, dwFileOffsetLow, dwNumberOfBytesToMap);
	}

	return ptr;
}


BOOL WINAPI OnUnmapViewOfFile(LPCVOID lpBaseAddress)
{
	//typedef BOOL (WINAPI* OnUnmapViewOfFile_t)(LPCVOID lpBaseAddress);
	ORIGINALFAST(UnmapViewOfFile);
    BOOL lbRc = FALSE;

	if (gpAnnotationHeader && (lpBaseAddress == gpAnnotationHeader))
	{
		lbRc = TRUE;
	}
	else
	{
    	lbRc = F(UnmapViewOfFile)(lpBaseAddress);
	}

    return lbRc;
}
