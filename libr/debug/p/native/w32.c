#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <winbase.h>
#include <psapi.h>
#include <tchar.h>

static HANDLE tid2handler(int pid, int tid);

// XXX remove
#define WIN32_PI(x) x
#if 0
// list windows.. required to get list of windows for current pid and send kill signals
BOOL CALLBACK enumWindowsProc (HWND hwnd, LPARAM lParam) {

DWORD procid;

GetWindowThreadProcessId (hwnd, &procid);

if ((HANDLE)procid == g_hProc) { staticchar module[1024];
module[0] = 0;

if (g_softPhoneTitle.Size() > 0) { int rc = GetWindowText (hwnd, module, 1023);
module[rc] = 0;

}

if (IsWindow(hwnd) && ((g_appTitle.Size() == 0) || (g_appTitle.EqualsNoCase(module)))) {
g_hWnd = hwnd;

return (false);
}

}

return (true);
}

int

findApplicationWindow (void) {
g_hWnd = NULL;

EnumWindows (enumWindowsProc, 0);

return (0);
}
#endif

#if 0

1860 typedef struct _FLOATING_SAVE_AREA {
1861         DWORD   ControlWord;
1862         DWORD   StatusWord;
1863         DWORD   TagWord;
1864         DWORD   ErrorOffset;

1865         DWORD   ErrorSelector;
1866         DWORD   DataOffset;
1867         DWORD   DataSelector;
1868         BYTE    RegArea[80];
1869         DWORD   Cr0NpxState;
1870 } FLOATING_SAVE_AREA;

1871 typedef struct _CONTEXT {
1872         DWORD   ContextFlags;
1873         DWORD   Dr0;
1874         DWORD   Dr1;
1875         DWORD   Dr2;
1876         DWORD   Dr3;
1877         DWORD   Dr6;
1878         DWORD   Dr7;
1879         FLOATING_SAVE_AREA FloatSave;
1880         DWORD   SegGs;
1881         DWORD   SegFs;
1882         DWORD   SegEs;
1883         DWORD   SegDs;
1884         DWORD   Edi;
1885         DWORD   Esi;
1886         DWORD   Ebx;
1887         DWORD   Edx;
1888         DWORD   Ecx;
1889         DWORD   Eax;
1890         DWORD   Ebp;
1891         DWORD   Eip;
1892         DWORD   SegCs;
1893         DWORD   EFlags;
1894         DWORD   Esp;
1895         DWORD   SegSs;
1896         BYTE    ExtendedRegs[MAXIMUM_SUPPORTED_EXTENSION];
1897 } CONTEXT;
#endif

//BOOL WINAPI DebugActiveProcessStop(DWORD dwProcessId);

BOOL WINAPI DebugBreakProcess(
  HANDLE Process
  //_In_  HANDLE Process
);
static void (*gmbn)(HANDLE, HMODULE, LPTSTR, int) = NULL;
static int (*gmi)(HANDLE, HMODULE, LPMODULEINFO, int) = NULL;
static BOOL WINAPI (*w32_detach)(DWORD) = NULL;
static HANDLE WINAPI (*w32_openthread)(DWORD, BOOL, DWORD) = NULL;
static BOOL WINAPI (*w32_dbgbreak)(HANDLE) = NULL;
static DWORD WINAPI (*w32_getthreadid)(HANDLE) = NULL; // Vista
static DWORD WINAPI (*w32_getprocessid)(HANDLE) = NULL; // XP
static HANDLE WINAPI (*w32_openprocess)(DWORD, BOOL, DWORD) = NULL;
static BOOL WINAPI (*w32_queryfullprocessimagename)(HANDLE, DWORD, LPTSTR, PDWORD) = NULL;
static DWORD WINAPI (*psapi_getmappedfilename)(HANDLE, LPVOID, LPTSTR, DWORD) = NULL;

static int w32dbg_SeDebugPrivilege() {
	/////////////////////////////////////////////////////////
	//   Note: Enabling SeDebugPrivilege adapted from sample
	//     MSDN @ http://msdn.microsoft.com/en-us/library/aa446619%28VS.85%29.aspx
	// Enable SeDebugPrivilege
	int ret = R_TRUE;
	TOKEN_PRIVILEGES tokenPriv;
	HANDLE hToken = NULL;
	LUID luidDebug;
	if (!OpenProcessToken (GetCurrentProcess (),
			TOKEN_ADJUST_PRIVILEGES, &hToken))
		return R_FALSE;
		
	if (!LookupPrivilegeValue (NULL, SE_DEBUG_NAME, &luidDebug)) {
		CloseHandle (hToken);
		return R_FALSE;
	}

	tokenPriv.PrivilegeCount = 1;
	tokenPriv.Privileges[0].Luid = luidDebug;
	tokenPriv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (AdjustTokenPrivileges (hToken, FALSE, &tokenPriv, 0, NULL, NULL) != FALSE) {
		if (tokenPriv.Privileges[0].Attributes == SE_PRIVILEGE_ENABLED) {
		//	eprintf ("PRIV ENABLED\n");
		}
		// Always successful, even in the cases which lead to OpenProcess failure
		//	eprintf ("Successfully changed token privileges.\n");
		// XXX if we cant get the token nobody tells?? wtf
	} else {
		eprintf ("Failed to change token privileges 0x%x\n", (int)GetLastError());
		ret = R_FALSE;
	}
	CloseHandle (hToken);
	return ret;
}

static void print_lasterr (const char *caller, char *cause) {
	WCHAR buffer[200];
	char cbuffer[100];
	if (!FormatMessageA (FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_ARGUMENT_ARRAY,
				NULL,
				GetLastError(),
				LANG_SYSTEM_DEFAULT,
				&cbuffer,
				sizeof (cbuffer)-1,
				NULL)) {
		eprintf ("Format message failed with 0x%d\n", (ut32)GetLastError ());
		return;
	}
	eprintf ("Error detected in %s/%s: %s\n", r_str_get (caller), r_str_get (cause), r_str_get (cbuffer));
}


static int w32_dbg_init() {
	HANDLE lib;

	/* escalate privs (required for win7/vista) */
	w32dbg_SeDebugPrivilege ();
	/* lookup function pointers for portability */
	w32_detach = (BOOL WINAPI (*)(DWORD))
		GetProcAddress (GetModuleHandle ("kernel32"),
				"DebugActiveProcessStop");
	w32_openthread = (HANDLE WINAPI (*)(DWORD, BOOL, DWORD))
		GetProcAddress (GetModuleHandle ("kernel32"), "OpenThread");
	w32_openprocess = (HANDLE WINAPI (*)(DWORD, BOOL, DWORD))
		GetProcAddress (GetModuleHandle ("kernel32"), "OpenProcess");
	w32_dbgbreak = (BOOL WINAPI (*)(HANDLE))
		GetProcAddress (GetModuleHandle ("kernel32"),
				"DebugBreakProcess");
	// only windows vista :(
	w32_getthreadid = (DWORD WINAPI (*)(HANDLE))
		GetProcAddress (GetModuleHandle ("kernel32"), "GetThreadId");
	// from xp1
	w32_getprocessid = (DWORD WINAPI (*)(HANDLE))  
		GetProcAddress (GetModuleHandle ("kernel32"), "GetProcessId");
	w32_queryfullprocessimagename = (BOOL WINAPI (*)(HANDLE, DWORD, LPTSTR, PDWORD))
		GetProcAddress (GetModuleHandle ("kernel32"), "QueryFullProcessImageNameA");

	lib = LoadLibrary ("psapi.dll");
	if(lib == NULL) {
		eprintf ("Cannot load psapi.dll. Aborting\n");
		return R_FALSE;
	}
	psapi_getmappedfilename = (DWORD WINAPI (*)(HANDLE, LPVOID, LPTSTR, DWORD))
		GetProcAddress (lib, "GetMappedFileNameA");
	gmbn = (void (*)(HANDLE, HMODULE, LPTSTR, int))
		GetProcAddress (lib, "GetModuleBaseNameA");
	gmi = (int (*)(HANDLE, HMODULE, LPMODULEINFO, int))
		GetProcAddress (lib, "GetModuleInformation");
	if (w32_detach == NULL || w32_openthread == NULL || w32_dbgbreak == NULL || 
	   gmbn == NULL || gmi == NULL) {
		// OOPS!
		eprintf ("debug_init_calls:\n"
			"DebugActiveProcessStop: 0x%p\n"
			"OpenThread: 0x%p\n"
			"DebugBreakProcess: 0x%p\n"
			"GetThreadId: 0x%p\n",
			w32_detach, w32_openthread, w32_dbgbreak, w32_getthreadid);
		return R_FALSE;
	}
	return R_TRUE;
}

#if 0
static HANDLE w32_t2h(pid_t tid) {
	TH_INFO *th = get_th (tid);
	if(th == NULL) {
		/* refresh thread list */
		w32_dbg_threads (tid);

		/* try to search thread */
		if((th = get_th (tid)) == NULL)
			return NULL;
	}
	return th->ht;
}
#endif

inline static int w32_h2t(HANDLE h) {
	if (w32_getthreadid != NULL) // >= Windows Vista
		return w32_getthreadid (h);
	if (w32_getprocessid != NULL) // >= Windows XP1
		return w32_getprocessid (h);
	return (int)(size_t)h; // XXX broken
}

static inline int w32_h2p(HANDLE h) {
	return w32_getprocessid (h);
}

static int w32_first_thread(int pid) {
	HANDLE th; 
	HANDLE thid; 
	THREADENTRY32 te32;
	te32.dwSize = sizeof (THREADENTRY32);

	if (w32_openthread == NULL) {
		eprintf("w32_thread_list: no w32_openthread?\n");
		return -1;
	}
	th = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, pid); 
	if (th == INVALID_HANDLE_VALUE) {
		eprintf ("w32_thread_list: invalid handle\n");
		return -1;
	}
	if (!Thread32First (th, &te32)) {
		CloseHandle (th);
		eprintf ("w32_thread_list: no thread first\n");
		return -1;
	}
	do {
		/* get all threads of process */
		if (te32.th32OwnerProcessID == pid) {
			thid = w32_openthread (THREAD_ALL_ACCESS, 0, te32.th32ThreadID);
			if (thid == NULL) {
				print_lasterr ((char *)__FUNCTION__, "OpenThread");
				goto err_load_th;
			}
			CloseHandle (th);
			return te32.th32ThreadID;
		}
	} while (Thread32Next (th, &te32));
err_load_th:    
	eprintf ("Could not find an active thread for pid %d\n", pid);
	CloseHandle (th);
	return pid;
}

static int debug_exception_event (unsigned long code) {
	switch (code) {
	case EXCEPTION_BREAKPOINT:
		break;
	case EXCEPTION_SINGLE_STEP:
		break;
	/* fatal exceptions */
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_STACK_OVERFLOW:
		eprintf ("fatal exception\n");
		break;
#if __MINGW64__
	case 0x4000001f: //STATUS_WX86_BREAKPOINT
		eprintf("WOW64 Loaded.\n");
		return 1;
		break;
#endif
	default:
		eprintf ("unknown exception\n");
		break;
	}
	return 0;
}

static char *get_file_name_from_handle (HANDLE handle_file) {
	HANDLE handle_file_map;
	TCHAR *filename = NULL;

	DWORD file_size_high = 0;
	DWORD file_size_low = GetFileSize (handle_file, &file_size_high); 
	if (file_size_low == 0 && file_size_high == 0) {
		return NULL;
	}

	handle_file_map = CreateFileMapping (handle_file, NULL, PAGE_READONLY, 0, 1, NULL);

	if (!handle_file_map) {
		return NULL;
	}
	filename = malloc(MAX_PATH+1);

	/* Create a file mapping to get the file name. */
	void* map = MapViewOfFile (handle_file_map, FILE_MAP_READ, 0, 0, 1);

	if (!map) { 
		CloseHandle (handle_file_map);
		return NULL;
	}

	if (!psapi_getmappedfilename (GetCurrentProcess (), 
		map, 
		filename,
		MAX_PATH)) {

		free(filename);
		UnmapViewOfFile (map);
		CloseHandle (handle_file_map);
		return NULL;
	}

	/* Translate path with device name to drive letters. */
	int temp_size = 512;
	TCHAR temp_buffer[temp_size];
	temp_buffer[0] = '\0';

	if (!GetLogicalDriveStrings (temp_size-1, temp_buffer)) {
		free (filename);
		UnmapViewOfFile (map);
		CloseHandle (handle_file_map);
		return NULL;
	}
		
	TCHAR name[MAX_PATH];
	TCHAR drive[3] = TEXT (" :");
	BOOL found = FALSE;
	TCHAR *p = temp_buffer;
	do {
		/* Look up each device name */
		*drive = *p;
		if (QueryDosDevice (drive, name, MAX_PATH)) {
			size_t name_length = strlen (name);

			if (name_length < MAX_PATH) {
				found = strncmp (filename, name, name_length) == 0
					&& *(filename + name_length) == _T ('\\');

				if (found) {
					TCHAR temp_filename[MAX_PATH];
					snprintf (temp_filename, MAX_PATH-1, "%s%s", 
						drive, filename+name_length);
					strncpy (filename, temp_filename, MAX_PATH-1);
				}
			}
		}
		while (*p++);
	} while (!found && *p);

	UnmapViewOfFile (map);
	CloseHandle (handle_file_map);
	return filename;
}

static int w32_dbg_wait(RDebug *dbg, int pid) {
	DEBUG_EVENT de;
	int tid, next_event = 0;
	unsigned int code;
	char *dllname = NULL;
	int ret = R_DBG_REASON_UNKNOWN;
	static int exited_already = 0;

	/* handle debug events */
	do {
		/* do not continue when already exited but still open for examination */
		if (exited_already) {
			return -1;
		}
		if (WaitForDebugEvent (&de, INFINITE) == 0) {
			print_lasterr ((char *)__FUNCTION__, "WaitForDebugEvent");
			return -1;
		}
		/* save thread id */
		tid = de.dwThreadId;
		//pid = de.dwProcessId;
		dbg->tid=tid;
		code = de.dwDebugEventCode;
		//eprintf("code: %x pid=%08x tid=%08x\n",code,pid,tid);
		/* Ctrl-C? */
		/* get kind of event */
		switch (code) {
		case CREATE_PROCESS_DEBUG_EVENT:
			eprintf ("(%d) created process (%d:%p)\n", 
				pid, w32_h2t (de.u.CreateProcessInfo.hProcess), 
				de.u.CreateProcessInfo.lpStartAddress);
			r_debug_native_continue (dbg, pid, tid, -1);
			next_event = 1;
			ret = R_DBG_REASON_NEW_PID;
			break;
		case EXIT_PROCESS_DEBUG_EVENT:
			eprintf ("(%d) Process %d exited with exit code %d\n",
				de.dwProcessId, de.dwProcessId,
				de.u.ExitProcess.dwExitCode);
			//debug_load();
			next_event = 0;
			exited_already = 1;
			ret = R_DBG_REASON_EXIT_PID;
			break;
		case CREATE_THREAD_DEBUG_EVENT:
			eprintf ("(%d) Created thread %d (start @ %p)\n",
				pid, tid, de.u.CreateThread.lpStartAddress);
			r_debug_native_continue (dbg, pid, tid, -1);
			ret = R_DBG_REASON_NEW_TID;
			next_event = 1;
			break;
		case EXIT_THREAD_DEBUG_EVENT:
			eprintf ("(%d) Finished thread %d\n", pid, tid);
			r_debug_native_continue (dbg, pid, tid, -1);
			next_event = 1;
			ret = R_DBG_REASON_EXIT_TID;
			break;
		case LOAD_DLL_DEBUG_EVENT:
			dllname = get_file_name_from_handle(de.u.LoadDll.hFile);	
			eprintf ("(%d) Loading library at %p (%s)\n",
				pid, de.u.LoadDll.lpBaseOfDll, 
				dllname ? dllname : "no name");
			if (dllname) {
				free(dllname);
			}
			r_debug_native_continue (dbg, pid, tid, -1);
			next_event = 1;
			ret = R_DBG_REASON_NEW_LIB;
			break;
		case UNLOAD_DLL_DEBUG_EVENT:
			eprintf ("(%d) Unloading library at %p\n", pid, de.u.UnloadDll.lpBaseOfDll);
			r_debug_native_continue (dbg, pid, tid, -1);
			next_event = 1;
			ret = R_DBG_REASON_EXIT_LIB;
			break;
		case OUTPUT_DEBUG_STRING_EVENT:
			eprintf ("(%d) Debug string\n", pid);
			r_debug_native_continue (dbg, pid, tid, -1);
			next_event = 1;
			break;
		case RIP_EVENT:
			eprintf ("(%d) RIP event\n", pid);
			r_debug_native_continue (dbg, pid, tid, -1);
			next_event = 1;
			// XXX unknown ret = R_DBG_REASON_TRAP;
			break;
		case EXCEPTION_DEBUG_EVENT:
			next_event = debug_exception_event (de.u.Exception.ExceptionRecord.ExceptionCode);
			if (!next_event)
				return R_DBG_REASON_TRAP;
			else 
				r_debug_native_continue (dbg, pid, tid, -1);
			break;
		default:
			eprintf ("(%d) unknown event: %d\n", pid, code);
			return -1;
		}
	} while (next_event);

	return ret;
}

static inline int CheckValidPE(unsigned char * PeHeader) {
	IMAGE_DOS_HEADER *dos_header = (IMAGE_DOS_HEADER *)PeHeader;
	IMAGE_NT_HEADERS *nt_headers;

	if (dos_header->e_magic==IMAGE_DOS_SIGNATURE) {
		nt_headers = (IMAGE_NT_HEADERS *)((char *)dos_header
				+ dos_header->e_lfanew);
		if (nt_headers->Signature==IMAGE_NT_SIGNATURE)
			return 1;
	}
	return 0;
}

static HANDLE tid2handler(int pid, int tid) {
        HANDLE th = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, pid);
        THREADENTRY32 te32 = { .dwSize = sizeof (THREADENTRY32) };
        if (th == INVALID_HANDLE_VALUE)
		return NULL;
	if (!Thread32First (th, &te32)) {
		CloseHandle (th);
                return NULL;
	}
        do {
                if (te32.th32OwnerProcessID == pid && te32.th32ThreadID == tid) {
			CloseHandle (th);
			return w32_openthread (THREAD_ALL_ACCESS, 0,
					te32.th32ThreadID);
		}
        } while (Thread32Next (th, &te32));
	CloseHandle (th);
        return NULL;
}

RList *w32_thread_list (int pid, RList *list) {
        HANDLE th; 
        HANDLE thid; 
        THREADENTRY32 te32;

        te32.dwSize = sizeof(THREADENTRY32);

	if (w32_openthread == NULL) {
		eprintf("w32_thread_list: no w32_openthread?\n");
		return list;
	}
        th = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, pid); 
        if(th == INVALID_HANDLE_VALUE || !Thread32First (th, &te32))
                goto err_load_th;
        do {
                /* get all threads of process */
                if (te32.th32OwnerProcessID == pid) {
			//te32.dwFlags);
                        /* open a new handler */
			// XXX: fd leak?
#if 0
 75 typedef struct tagTHREADENTRY32 {
 76         DWORD dwSize;
 77         DWORD cntUsage;
 78         DWORD th32ThreadID;
 79         DWORD th32OwnerProcessID;
 80         LONG tpBasePri;
 81         LONG tpDeltaPri;
 82         DWORD dwFlags;
#endif
			thid = w32_openthread (THREAD_ALL_ACCESS, 0, te32.th32ThreadID);
			if (thid == NULL) {
				print_lasterr((char *)__FUNCTION__, "OpenThread");
                                goto err_load_th;
			}
			r_list_append (list, r_debug_pid_new ("???", te32.th32ThreadID, 's', 0));
                }
        } while (Thread32Next (th, &te32));
err_load_th:    
        if(th != INVALID_HANDLE_VALUE)
                CloseHandle (th);
	return list;
}

static RDebugPid *build_debug_pid(PROCESSENTRY32 *pe) {
	HANDLE process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION,
		FALSE, pe->th32ProcessID);

	if (process == INVALID_HANDLE_VALUE || w32_queryfullprocessimagename == NULL) {
		return r_debug_pid_new (pe->szExeFile, pe->th32ProcessID, 's', 0);
	}

	char image_name[MAX_PATH+1];
	image_name[0] = '\0';
	DWORD length = MAX_PATH;

	if (w32_queryfullprocessimagename (process, 0, 
		image_name, (PDWORD)&length)) {
		CloseHandle(process);
		return r_debug_pid_new (image_name, pe->th32ProcessID, 's', 0);
	}

	CloseHandle(process);
	return r_debug_pid_new (pe->szExeFile, pe->th32ProcessID, 's', 0);
}

RList *w32_pids (int pid, RList *list) {
	HANDLE process_snapshot;
	PROCESSENTRY32 pe;
	pe.dwSize = sizeof (PROCESSENTRY32);
	int show_all_pids = pid == 0;

	process_snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, pid); 
	if (process_snapshot == INVALID_HANDLE_VALUE) {
		print_lasterr ((char *)__FUNCTION__, "CreateToolhelp32Snapshot");
		return list;
	}
	if (!Process32First (process_snapshot, &pe)) {
		print_lasterr ((char *)__FUNCTION__, "Process32First");
		CloseHandle (process_snapshot);
		return list;
	}
	do {
		if (show_all_pids || 
			pe.th32ProcessID == pid || 
			pe.th32ParentProcessID == pid) {
	
			RDebugPid *debug_pid = build_debug_pid (&pe);
			if (debug_pid) {
				r_list_append (list, debug_pid);
			}
		}
	} while (Process32Next (process_snapshot, &pe));

	CloseHandle (process_snapshot);
	return list;
}

int w32_terminate_process (RDebug *dbg, int pid) {
	HANDLE process = dbg->process_handle;
	if (process == INVALID_HANDLE_VALUE) {
		return R_FALSE;
	}

	/* stop debugging if we are still attached */
	DebugActiveProcessStop (pid);
	if (TerminateProcess (process, 1) == 0) {
		print_lasterr ((char *)__FUNCTION__, "TerminateProcess");
		return R_FALSE;

	}
	DWORD ret_wait;
	/* wait up to one second to give the process some time to exit */
	ret_wait = WaitForSingleObject (process, 1000);
	if (ret_wait == WAIT_FAILED) {
		print_lasterr ((char *)__FUNCTION__, "WaitForSingleObject");
		return R_FALSE;
	}
	if (ret_wait == WAIT_TIMEOUT) {
		eprintf ("(%d) Waiting for process to terminate timed out.\n", pid);
		return R_FALSE;
	}

	return R_TRUE;
}

#include "maps/windows.c"
