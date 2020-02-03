#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifdef UNICODE
#undef UNICODE
#endif

#include <windows.h>
#include <shlwapi.h>
#include <process.h>
#include <psapi.h>
#include <stdio.h>
#include <time.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <stdlib.h>

#ifndef MSYS2
#pragma comment(lib,"shlwapi.lib")
#ifdef GUI
#pragma comment(linker,"/subsystem:windows")
#else
#pragma comment(linker,"/subsystem:console")
#endif
#endif

#define BUFFER_SIZE 512
#define MODULE_PLACEHOLDER -1

#define MAX_MODULES 1024
#define MAX_PROCESSES 1024
#define MAX_SHOWN_PATCH_SIZE 16*3+1*2 //16 -> "?? "   +   1 -> "+\0" (worst case)

#define DATA_DIRECTORIES_OFFSET_X64 0x70
#define DATA_DIRECTORIES_OFFSET_X86 0x60
#define RELOCATION_DATA_DIRECTORY_INDEX 5

#define ID_SCAN_BUTTON 1001
#define ID_PROCESS_LIST 2001
#define ID_PATCH_LIST 2002
#define ID_LOG_LIST 2003
#define ID_PROGRESS_BAR_01 3001
#define ID_PROGRESS_BAR_02 3002
#define ID_MENU_PROCESS_LISTVIEW_REFRESH 4001

int busy = 0;

//Structs + Lists
typedef struct patch_list
{
	char modulename[MAX_PATH];
	char originalbytes[MAX_SHOWN_PATCH_SIZE];
	char patchedbytes[MAX_SHOWN_PATCH_SIZE];
	DWORD64 patchedbytesoffset;
	unsigned long int patchedbytescount;
	struct patch_list* next;
} PATCH_LIST;

typedef struct section_header_list
{
	PIMAGE_SECTION_HEADER pimagesectionheader;
	struct section_header_list* next;
} SECTION_HEADER_LIST;

typedef struct reloc
{
	PIMAGE_DATA_DIRECTORY pimagerelocationdatadirectory;
	PIMAGE_BASE_RELOCATION pimagebaserelocation;
	DWORD64 relocationoffset;
} RELOC;

typedef struct win_process
{
	char name[MAX_PATH];
	DWORD pid;
} WIN_PROCESS;

//Prototypes
#ifdef GUI
int scan();
#else
int main();
#endif
DWORD loadFromFile(char* filename, char** buffer);
DWORD64 virtualaddressToFileAddress(DWORD64 virtualaddress, SECTION_HEADER_LIST* sectionheaders);
void applyRelocation(void* filebuffer, RELOC* preloc, SECTION_HEADER_LIST* sectionheaders);
void sectionHeaderListAddLast(SECTION_HEADER_LIST** psectionheaders, SECTION_HEADER_LIST* shlnode);
void patchListAddLast(PATCH_LIST** ppatches, PATCH_LIST* plnode);
void sectionHeaderListFree(SECTION_HEADER_LIST** psectionheaders);
void patchListFree(PATCH_LIST** ppatches);
#ifndef GUI
void printPatchList(PATCH_LIST* patches);
#endif

#ifdef GUI
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
void onWindowCreate(HWND);
void onWindowClose();
void onScanButtonClick();
void onMenuItemRefreshClick();
void onProcessListViewRightClick();
void updateProcessListView(WIN_PROCESS* processes);
void updatePatchListView(PATCH_LIST* patches);
void getProcesses(WIN_PROCESS* processes);
void concatenateLogListView(char* str, int itemindex);
void appendLogListView(char* log);

HWND hwndMain = NULL;
HWND hwndScanButton = NULL;
HWND hwndProcessListView = NULL;
HWND hwndPatchListView = NULL;
HWND hwndLogListView = NULL;
HWND hwndScanProgressBar01 = NULL;
HWND hwndScanProgressBar02 = NULL;

HFONT hFontMain = NULL;

//__[calloc'd]________________
WIN_PROCESS* processes = NULL;
//____________________________

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow)
{
	BYTE className[] = "mainWindowClass";
	#ifdef _WIN64
	BYTE formName[] = "Patch Scanner x64";
	#else
	BYTE formName[] = "Patch Scanner x86";
	#endif
	MSG msg = { 0 };

	WNDCLASS wndClass = { 0 };
	wndClass.hInstance = (HINSTANCE)hInst;
	wndClass.lpszClassName = (LPCSTR)className;
	wndClass.lpfnWndProc = (WNDPROC)WindowProc;
	wndClass.hbrBackground = (HBRUSH)GetSysColorBrush(COLOR_3DFACE);
	wndClass.style = (UINT)(CS_HREDRAW | CS_VREDRAW);
	wndClass.hCursor = (HCURSOR)LoadCursor(NULL, IDC_ARROW);

	RegisterClass(&wndClass);

	//[CreateWindowEx]: dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam
    hwndMain = CreateWindow((LPCSTR)className, (LPCSTR)formName, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 1112, 565, NULL, NULL, hInst, NULL);

	if (hwndMain)
	{ 
		ShowWindow(hwndMain, nCmdShow);
		while (GetMessage(&msg, NULL, 0, 0))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CREATE:
		onWindowCreate(hwnd);
		return 0;
	case WM_CLOSE:
		onWindowClose();
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_SCAN_BUTTON:
			onScanButtonClick();
			return 0;
		case ID_MENU_PROCESS_LISTVIEW_REFRESH:
			onMenuItemRefreshClick();
			return 0;
		}
	case WM_NOTIFY:
		switch (wParam)
		{
		case ID_PROCESS_LIST:
			if (((NMHDR*)lParam)->code == NM_RCLICK)
			{
				onProcessListViewRightClick();
			}
			return 0;
		}
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void onWindowCreate(HWND hwnd)
{
	//[CreateWindow]: lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam
	hwndScanButton = CreateWindow("Button", "Scan", WS_CHILD | WS_VISIBLE, 15, 480, 350, 32, hwnd, (HMENU)ID_SCAN_BUTTON, NULL, NULL);
	hwndProcessListView = CreateWindow("SysListView32", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS, 15, 15, 350, 450, hwnd, (HMENU)ID_PROCESS_LIST, NULL, NULL);
	hwndPatchListView = CreateWindow("SysListView32", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS, 380, 15, 700, 250, hwnd, (HMENU)ID_PATCH_LIST, NULL, NULL);
	hwndLogListView = CreateWindow("SysListView32", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS, 380, 280, 700, 185, hwnd, (HMENU)ID_LOG_LIST, NULL, NULL);
	hwndScanProgressBar01 = CreateWindow("msctls_progress32", NULL, WS_CHILD | WS_VISIBLE, 380, 481, 700, 14, hwnd, (HMENU)ID_PROGRESS_BAR_01, NULL, NULL);
	hwndScanProgressBar02 = CreateWindow("msctls_progress32", NULL, WS_CHILD | WS_VISIBLE, 380, 497, 700, 14, hwnd, (HMENU)ID_PROGRESS_BAR_02, NULL, NULL);

	//Set Font
	hFontMain = CreateFont(19, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, TEXT("Segoe UI"));
	SendMessage(hwndScanButton, WM_SETFONT, (WPARAM)hFontMain, (LPARAM)0);
	SendMessage(hwndProcessListView, WM_SETFONT, (WPARAM)hFontMain, (LPARAM)0);
	SendMessage(hwndPatchListView, WM_SETFONT, (WPARAM)hFontMain, (LPARAM)0);
	SendMessage(hwndLogListView, WM_SETFONT, (WPARAM)hFontMain, (LPARAM)0);

	//Init Lists
	LVCOLUMN lvc = { 0 };
	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

	//Process list
	lvc.iSubItem = 1;
	lvc.pszText = "Process name";
	lvc.cx = 300;
	ListView_InsertColumn(hwndProcessListView, 0, &lvc);
	lvc.iSubItem = 0;
	lvc.pszText = "PID";
	lvc.cx = 100;
	ListView_InsertColumn(hwndProcessListView, 0, &lvc);

	//Patch list
	lvc.iSubItem = 2;
	lvc.pszText = "Patched bytes";
	lvc.cx = 300;
	ListView_InsertColumn(hwndPatchListView, 0, &lvc);
	lvc.iSubItem = 1;
	lvc.pszText = "Original bytes";
	lvc.cx = 300;
	ListView_InsertColumn(hwndPatchListView, 0, &lvc);
	lvc.iSubItem = 0;
	lvc.pszText = "Address";
	lvc.cx = 180;
	ListView_InsertColumn(hwndPatchListView, 0, &lvc);

	//Log list
	lvc.iSubItem = 0;
	lvc.pszText = "Logs";
	lvc.cx = 700;
	ListView_InsertColumn(hwndLogListView, 0, &lvc);

	//Highlight full row
	ListView_SetExtendedListViewStyle(hwndProcessListView, LVS_EX_FULLROWSELECT);
	ListView_SetExtendedListViewStyle(hwndPatchListView, LVS_EX_FULLROWSELECT);
	ListView_SetExtendedListViewStyle(hwndLogListView, LVS_EX_FULLROWSELECT);

    //Allocate
	processes = (WIN_PROCESS*)calloc(MAX_PROCESSES, sizeof(WIN_PROCESS));

	//Get processes
	updateProcessListView(processes);
}

void onWindowClose()
{
	//Free buffers
	if (processes)
	{
		free(processes);
		processes = NULL;
	}
}

void onScanButtonClick()
{
	if (!busy)
	{
		_beginthread((_beginthread_proc_type)scan, 0, NULL);
	}
	else
	{
		MessageBoxA(hwndMain, "Scan in progress...", NULL, MB_ICONEXCLAMATION);
	}
}

void onMenuItemRefreshClick()
{
	updateProcessListView(processes);
}

void onProcessListViewRightClick()
{
	HMENU hpopupmenu = NULL;
	POINT p = { 0 };

	if (GetCursorPos(&p))
	{
		hpopupmenu = CreatePopupMenu();
		InsertMenu(hpopupmenu, 0, MF_BYPOSITION | MF_STRING, ID_MENU_PROCESS_LISTVIEW_REFRESH, "Refresh");
		SetForegroundWindow(hwndMain);
		TrackPopupMenu(hpopupmenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, p.x, p.y, 0, hwndMain, NULL);
	}
}

void updatePatchListView(PATCH_LIST* patches)
{
	LVITEM lvi = { 0 };
	char* str = NULL;

	if ((str = (char*)calloc(MAX_PATH, 1)))
	{
		lvi.mask = LVIF_TEXT;
		ListView_DeleteAllItems(hwndPatchListView);

		for (unsigned long int i = 0; patches; i++)
		{
			_itoa(patches->patchedbytesoffset, str, 16);
			_strupr(str);
			strcat(patches->modulename, "+");
			strcat(patches->modulename, str);
			lvi.iItem = i;
			lvi.iSubItem = 0;
			lvi.pszText = patches->modulename;
			ListView_InsertItem(hwndPatchListView, &lvi);
			lvi.iSubItem = 1;
			lvi.pszText = patches->originalbytes;
			ListView_SetItem(hwndPatchListView, &lvi);
			lvi.iSubItem = 2;
			lvi.pszText = patches->patchedbytes;
			ListView_SetItem(hwndPatchListView, &lvi);
			patches = patches->next;
		}
		free(str);
		str = NULL;
	}
}

void updateProcessListView(WIN_PROCESS* processes)
{
	LVITEM lvi = { 0 };
	DWORD currpos = 0;
	char* pid = NULL;

	if ((pid = (char*)calloc(MAX_PATH, 1)))
	{
		currpos = ListView_GetNextItem(hwndProcessListView, -1, LVNI_SELECTED);
		lvi.mask = LVIF_TEXT;
		RtlZeroMemory(processes, sizeof(WIN_PROCESS) * MAX_PROCESSES);
		ListView_DeleteAllItems(hwndProcessListView);

		getProcesses(processes);

		for (unsigned long int i = 0; processes[i].pid | !i; i++)
		{
			lvi.iItem = i;
			lvi.iSubItem = 0;
			_itoa(processes[i].pid, pid, 10);
			lvi.pszText = pid;
			ListView_InsertItem(hwndProcessListView, &lvi);
			lvi.iSubItem = 1;
			lvi.pszText = processes[i].name;
			ListView_SetItem(hwndProcessListView, &lvi);
		}
		ListView_EnsureVisible(hwndProcessListView, currpos, TRUE);
		free(pid);
		pid = NULL;
	}
}

void appendLogListView(char* log)
{
	LVITEM lvi = { 0 };
	int itemcount = 0;

	itemcount = ListView_GetItemCount(hwndLogListView);
	lvi.mask = LVIF_TEXT;
	lvi.iItem = itemcount;
	lvi.iSubItem = 0;
	lvi.pszText = log;
	ListView_InsertItem(hwndLogListView, &lvi);
	ListView_EnsureVisible(hwndLogListView, itemcount, FALSE);
}

void concatenateLogListView(char* str, int itemindex)
{
	LVITEM lvi = { 0 };
	char* newstr = NULL;

	if ( (newstr = (char*)calloc(MAX_PATH, 1)) )
	{
		lvi.mask = LVIF_TEXT;
		lvi.iItem = itemindex;
		lvi.iSubItem = 0;
		lvi.cchTextMax = MAX_PATH;
		lvi.pszText = newstr;

		ListView_GetItem(hwndLogListView, &lvi);

		strcat(newstr, str);
		lvi.pszText = newstr;

		ListView_SetItem(hwndLogListView, &lvi);
		ListView_EnsureVisible(hwndLogListView, itemindex, FALSE);
		free(newstr);
		newstr = NULL;
	}
}

void getProcesses(WIN_PROCESS* processes)
{
	HANDLE hprocess = NULL;
	HANDLE hsnapshot = NULL;
	PROCESSENTRY32 processEntry = { 0 };
	int i = 0;

	processEntry.dwSize = sizeof(PROCESSENTRY32);

	if ( ((hsnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)) != INVALID_HANDLE_VALUE) && Process32First(hsnapshot, &processEntry) )
	{
		do
		{
			strcpy(processes[i].name, processEntry.szExeFile);
			processes[i].pid = processEntry.th32ProcessID;
			i++;
		} while ( Process32Next(hsnapshot, &processEntry) && (i < MAX_PROCESSES) );
	}
	CloseHandle(hsnapshot);
}
#endif

#ifdef GUI
int scan()
#else
int main()
#endif
{
	//__[calloc'd]______________________________________________________________________________________________________________________________
	void** filebuffer = NULL; //Array of loaded files
	void* vmbuffer = NULL; 
	char* tmpbuffer = NULL;
	char* modulefilename = NULL; //2D Array of (Module names + paths)
	SECTION_HEADER_LIST* executableandrelocsectionheaders = NULL; //Singly Linked List (Executable sections + Relocation section (if available))
	PATCH_LIST* patches = NULL; //Singly Linked List (Patches)
	RELOC* preloc = NULL;
	HMODULE* hmodules = NULL;
	//__________________________________________________________________________________________________________________________________________

	//Stack
	DWORD pid = 0;
	HANDLE hprocess = NULL;
	unsigned long int patchescount = 0;
	unsigned long int currentmodulepatchescount = 0;
	unsigned long int modulescount = 0;
	unsigned long int notloadedmodulescount = 0;
	unsigned long int currentlybeingscannedmodulelwindex = 0;
	PIMAGE_DOS_HEADER pimagedosheader = NULL;
	PIMAGE_FILE_HEADER pimagefileheader = NULL;
	PIMAGE_SECTION_HEADER pimagesectionheader = NULL;
	PIMAGE_OPTIONAL_HEADER32 pimageoptionalheader32 = NULL;
	PIMAGE_OPTIONAL_HEADER64 pimageoptionalheader64 = NULL;
	void* fileposition = NULL;
	clock_t stop = 0;
	clock_t start = 0;

	busy = 1;

	//Allocate some buffers and zero-out the allocated memory
	hmodules = (HMODULE*)calloc(MAX_MODULES+1, sizeof(HMODULE));
	modulefilename = (char*)calloc(MAX_MODULES+1, MAX_PATH);
	tmpbuffer = (char*)calloc(BUFFER_SIZE, 1);
	preloc = (RELOC*)calloc(1, sizeof(RELOC));

	//Collect input
#ifndef GUI
	setvbuf(stdout, NULL, _IONBF, 0);
	fprintf(stdout, "Process ID: >");

	if (fscanf(stdin, "%lu", &pid) <= 0)
	{
		goto free_and_quit;
	}
	system("@cls||clear");
#else
	ListView_DeleteAllItems(hwndLogListView); //Empty log listview

	ListView_GetItemText(hwndProcessListView, ListView_GetNextItem(hwndProcessListView, -1, LVNI_SELECTED), 0, tmpbuffer, 16);
	pid = _atoi64(tmpbuffer);
#endif
	start = clock(); //Execution time

	//Verify OpenProcess success
	if ( !(hprocess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid)) )
	{
#ifdef GUI
		appendLogListView("[Error] Couldn't attach to this process."); //Insert error in the log listview
#endif
		goto free_and_quit;
	}

	//Collect process modules and paths
	DWORD cbNeeded = 0;
	if (EnumProcessModules(hprocess, hmodules, MAX_MODULES, &cbNeeded))
	{
		for (unsigned long int m = 0; hmodules[m]; m++)
		{
			GetModuleFileNameEx(hprocess, hmodules[m], modulefilename+m*MAX_PATH, MAX_PATH);
		}
	}
	else
	{
#ifndef GUI
		fprintf(stdout, "Use x64 version\r\n");
#else
		//insert error in the log listview
		appendLogListView("[Error] Use x64 version.");
#endif
		goto free_and_quit;
	}

	//Allocate some buffers and zero-out the allocated memory
	filebuffer = (void**)calloc((cbNeeded / sizeof(HMODULE)) + 1, sizeof(void*));

	//Load modules (files)
	for (unsigned long int m = 0; hmodules[m]; m++, modulescount++)
	{
		if (!loadFromFile(modulefilename+m*MAX_PATH, (char**)(filebuffer + m)))
		{
#ifdef GUI
			strcpy(tmpbuffer, "[Error] ");
			PathStripPathA(modulefilename+m*MAX_PATH);
			strcat(tmpbuffer, modulefilename+ m*MAX_PATH);
			strcat(tmpbuffer, ": couldn't load this file.");
			appendLogListView(tmpbuffer);		
			filebuffer[m] = hmodules[m] = (void*)MODULE_PLACEHOLDER;
			notloadedmodulescount++;
			continue;
#endif
		}
	}

#ifdef GUI
	snprintf(tmpbuffer, BUFFER_SIZE, "[Log] Modules loaded: %d/%d", (modulescount-notloadedmodulescount), modulescount);
	appendLogListView(tmpbuffer);
#endif

	//Set progressbars range and increment and empty patch listview
#ifdef GUI
	SendMessage(hwndScanProgressBar02, PBM_SETRANGE, 0, MAKELPARAM(0, modulescount));
	SendMessage(hwndScanProgressBar02, PBM_SETSTEP, (WPARAM)1, 0);
	SendMessage(hwndScanProgressBar02, PBM_SETPOS, 0, 0);

	SendMessage(hwndScanProgressBar01, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
	SendMessage(hwndScanProgressBar01, PBM_SETSTEP, (WPARAM)1, 0);
	SendMessage(hwndScanProgressBar01, PBM_SETPOS, 0, 0);

	ListView_DeleteAllItems(hwndPatchListView);
#endif

	for (unsigned long int m = 0; filebuffer[m]; m++)
	{

		if (filebuffer[m] == (void*)MODULE_PLACEHOLDER)
		{
			continue;
		}

#ifdef GUI
		PathStripPathA(modulefilename + m * MAX_PATH);
		snprintf(tmpbuffer, BUFFER_SIZE, "[Log] Scanning: %s", modulefilename + m * MAX_PATH);
		appendLogListView(tmpbuffer);
#endif
		currentlybeingscannedmodulelwindex = ListView_GetItemCount(hwndLogListView) - 1;
		currentmodulepatchescount = 0;
		fileposition = filebuffer[m];
		pimagedosheader = (PIMAGE_DOS_HEADER)fileposition;

		//Make sure it's a PE
		if (pimagedosheader->e_magic == IMAGE_DOS_SIGNATURE)
		{
			//Gather needed PE info
			fileposition = (BYTE*)fileposition + pimagedosheader->e_lfanew + sizeof(DWORD32);
			pimagefileheader = (PIMAGE_FILE_HEADER)fileposition;
			fileposition = (BYTE*)fileposition + sizeof(IMAGE_FILE_HEADER);
			if (pimagefileheader->Machine == IMAGE_FILE_MACHINE_AMD64)
			{
				pimageoptionalheader64 = (PIMAGE_OPTIONAL_HEADER64)fileposition;
				preloc->relocationoffset = (DWORD64)((BYTE*)hmodules[m] - pimageoptionalheader64->ImageBase);
				preloc->pimagerelocationdatadirectory = (PIMAGE_DATA_DIRECTORY)(((BYTE*)fileposition + DATA_DIRECTORIES_OFFSET_X64) + sizeof(IMAGE_DATA_DIRECTORY) * RELOCATION_DATA_DIRECTORY_INDEX);
			}
			else
			{
				pimageoptionalheader32 = (PIMAGE_OPTIONAL_HEADER32)fileposition;
				preloc->relocationoffset = (DWORD64)((BYTE*)hmodules[m] - pimageoptionalheader32->ImageBase);
				preloc->pimagerelocationdatadirectory = (PIMAGE_DATA_DIRECTORY)(((BYTE*)fileposition + DATA_DIRECTORIES_OFFSET_X86) + sizeof(IMAGE_DATA_DIRECTORY) * RELOCATION_DATA_DIRECTORY_INDEX);
			}
			fileposition = (BYTE*)fileposition + pimagefileheader->SizeOfOptionalHeader;

			//Filter executable sections (& .reloc)
			for (unsigned long int i = 0; i < pimagefileheader->NumberOfSections; i++, fileposition = (BYTE*)fileposition + sizeof(IMAGE_SECTION_HEADER))
			{
				pimagesectionheader = (PIMAGE_SECTION_HEADER)fileposition;
				if ((pimagesectionheader->Characteristics & IMAGE_SCN_CNT_CODE) || !strcmp((char*)pimagesectionheader->Name, ".reloc"))
				{
					SECTION_HEADER_LIST* shlnode = (SECTION_HEADER_LIST*)calloc(1, sizeof(SECTION_HEADER_LIST));
					shlnode->pimagesectionheader = (PIMAGE_SECTION_HEADER)fileposition;
					shlnode->next = NULL;
					sectionHeaderListAddLast(&executableandrelocsectionheaders, shlnode);
				}
			}

			//Apply relocation
			if (preloc->relocationoffset)
			{
				applyRelocation(filebuffer[m], preloc, executableandrelocsectionheaders);
			}

			//Scan for current module's patches
			SECTION_HEADER_LIST* sectionheaders = executableandrelocsectionheaders;
			for (;sectionheaders; sectionheaders = sectionheaders->next)
			{
				//Skip .reloc
				if (!strcmp((char*)sectionheaders->pimagesectionheader->Name, ".reloc"))
				{
					continue;
				}

				if ( (vmbuffer = (void*)calloc(1, sectionheaders->pimagesectionheader->SizeOfRawData)) )
				{
#ifdef _WIN64
					long long unsigned int bytesread = 0;
					long long unsigned int position = 0;
#else
					long unsigned int bytesread = 0;
					long unsigned int position = 0;
#endif
					ReadProcessMemory(hprocess, (BYTE*)hmodules[m] + sectionheaders->pimagesectionheader->VirtualAddress, vmbuffer, sectionheaders->pimagesectionheader->SizeOfRawData, &bytesread);

					//Verify ReadProcessMemory success
					if (bytesread)
					{
						fileposition = (BYTE*)filebuffer[m] + sectionheaders->pimagesectionheader->PointerToRawData;
						position += RtlCompareMemory(vmbuffer, fileposition, sectionheaders->pimagesectionheader->SizeOfRawData);

						//For each patch found a node is allocated, filled with the needed info and added to the patch list
						unsigned long int k = 0;
						while (position < sectionheaders->pimagesectionheader->SizeOfRawData)
						{
							PATCH_LIST* plnode = NULL;
							if ( (plnode = (PATCH_LIST*)calloc(1, sizeof(PATCH_LIST))) )
							{
								plnode->patchedbytescount = 0;
								strcpy(plnode->modulename, modulefilename + m * MAX_PATH);
								PathStripPathA(plnode->modulename); //Remove path for a 'better' output
								plnode->patchedbytesoffset = position;

								while ((position <= sectionheaders->pimagesectionheader->SizeOfRawData) && (*((BYTE*)(vmbuffer)+position) != *((BYTE*)(fileposition)+position)))
								{
									if ((k + 1) * 3 < MAX_SHOWN_PATCH_SIZE && k != -1)
									{
										snprintf(plnode->originalbytes + k * 3, 4, "%.2X ", *((BYTE*)fileposition + plnode->patchedbytesoffset + k));
										snprintf(plnode->patchedbytes + k * 3, 4, "%.2X ", *((BYTE*)vmbuffer + plnode->patchedbytesoffset + k));
										k++;
									}
									else if (k != -1)
									{
										snprintf(plnode->originalbytes + k * 3, 4, "+");
										snprintf(plnode->patchedbytes + k * 3, 4, "+");
										k = -1;
									}
									plnode->patchedbytescount++;
									position++;
								}
								plnode->patchedbytesoffset += sectionheaders->pimagesectionheader->VirtualAddress;
								plnode->next = NULL;

								patchListAddLast(&patches, plnode);

								position += RtlCompareMemory((BYTE*)vmbuffer + position, (BYTE*)fileposition + position, sectionheaders->pimagesectionheader->SizeOfRawData);
								patchescount++;
								currentmodulepatchescount++;
								k = 0;
							}
							else
							{
#ifdef GUI
								appendLogListView("[Error] Lack of memory.");
#endif
								free(vmbuffer);
								vmbuffer = NULL;
								goto free_and_quit;
						    }
#ifdef GUI
							SendMessage(hwndScanProgressBar01, PBM_SETPOS, (WPARAM)((long long unsigned int)position * 100 / sectionheaders->pimagesectionheader->SizeOfRawData), 0); //Progressbar step
#endif
						}
#ifdef GUI
						SendMessage(hwndScanProgressBar01, PBM_SETPOS, (WPARAM)((long long unsigned int)position * 100 / sectionheaders->pimagesectionheader->SizeOfRawData), 0); //Progressbar step
						snprintf(tmpbuffer, BUFFER_SIZE, " -> patches: %d", currentmodulepatchescount);
						concatenateLogListView(tmpbuffer, currentlybeingscannedmodulelwindex);
#endif
					}
					else
					{
						appendLogListView("[Error] ReadProcessMemory issue.");
					}
					free(vmbuffer);
					vmbuffer = NULL;
				}
			}
			sectionHeaderListFree(&executableandrelocsectionheaders);
		}
		free(filebuffer[m]);
		filebuffer[m] = NULL;
		hmodules[m] = NULL;
#ifdef GUI
		SendMessage(hwndScanProgressBar02, PBM_STEPIT, 0, 0); //Progressbar step
#endif
	}
	free(filebuffer);
	filebuffer = NULL;

#ifndef GUI
	printPatchList(patches);
#else
	updatePatchListView(patches);
	snprintf(tmpbuffer, BUFFER_SIZE, "[Log] Total patches: %d", patchescount);
	appendLogListView(tmpbuffer);
#endif
	
free_and_quit:
#ifdef GUI
	SendMessage(hwndScanProgressBar01, PBM_SETPOS, (WPARAM)100, 0);
	SendMessage(hwndScanProgressBar02, PBM_SETPOS, (WPARAM)modulescount, 0);
#endif

	if (filebuffer)
	{
		for (unsigned long int i = 0; filebuffer[i]; i++)
		{
			free(filebuffer[i]);
			filebuffer[i] = NULL;
		}
		free(filebuffer);
		filebuffer = NULL;
	}

	if (hmodules)
	{
		free(hmodules);
		hmodules = NULL;
	}

	if (modulefilename)
	{
		free(modulefilename);
		modulefilename = NULL;
	}

	if (preloc)
	{
		free(preloc);
		preloc = NULL;
	}


	if (vmbuffer)
	{
		free(vmbuffer);
		vmbuffer = NULL;
	}

	if (patches)
	{
		patchListFree(&patches);
	}

	if (executableandrelocsectionheaders)
	{
		sectionHeaderListFree(&executableandrelocsectionheaders);
	}

	stop = clock();
#ifndef GUI
	fprintf(stdout, "\n\n(Execution time: %f seconds)\n", ( ((double)stop - (double)start) / CLOCKS_PER_SEC) );
#else
	snprintf(tmpbuffer, BUFFER_SIZE, "[Log] Execution time: %f seconds", (double)(stop - start) / CLOCKS_PER_SEC);
	appendLogListView(tmpbuffer);
#endif
	free(tmpbuffer);
	tmpbuffer = NULL;

	busy = 0;
	return 0;
}

DWORD loadFromFile(char* filename, char** buffer)
{
	DWORD bufferSize = 0;
	DWORD bytesRead = 0;
	HANDLE hFile = NULL;

	if ( ((hFile = CreateFile(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) != INVALID_HANDLE_VALUE) )
	{
		if ( (bufferSize = GetFileSize(hFile, NULL)) > 0 )
		{
			if ( (*buffer = (char*)calloc(bufferSize, 1)) )
			{
				if (!ReadFile(hFile, *buffer, bufferSize, &bytesRead, NULL))
				{
					free(*buffer);
					*buffer = NULL;
				}
			}
		}
		CloseHandle(hFile);
	}
	return bufferSize;
}

void applyRelocation(void *filebuffer, RELOC *preloc, SECTION_HEADER_LIST* sectionheaders)
{
	DWORD offset = 0;
	DWORD relocationtype = 0;
	long unsigned int relocationitems = 0;
	void* sectionbase = NULL;
	void* filebase = NULL;
	void* endofrelocationdir = NULL;

	filebase = filebuffer;
	filebuffer = (BYTE*)filebuffer + virtualaddressToFileAddress(preloc->pimagerelocationdatadirectory->VirtualAddress, sectionheaders);
	endofrelocationdir = (BYTE*)filebuffer + preloc->pimagerelocationdatadirectory->Size;

	while (filebuffer < endofrelocationdir)
	{
		preloc->pimagebaserelocation = (PIMAGE_BASE_RELOCATION)filebuffer;
		relocationitems = (preloc->pimagebaserelocation->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD); //To determine the number of relocations in this block, subtract the size of an IMAGE_BASE_RELOCATION (8 bytes) from the value of this field, and then divide by 2 (the size of a WORD)
		filebuffer = (BYTE*)filebuffer + sizeof(IMAGE_BASE_RELOCATION);

		if (sectionbase = (void*)virtualaddressToFileAddress(preloc->pimagebaserelocation->VirtualAddress, sectionheaders))
		{
			sectionbase = (BYTE*)sectionbase + (DWORD64)filebase;
		}

		for (unsigned long int i = 0; i < relocationitems; i++)
		{
			if (sectionbase)
			{
				offset = (*(WORD*)filebuffer) & 0xfff; //The bottom 12 bits of each WORD are a relocation offset
				relocationtype = (*(WORD*)filebuffer) >> 12; //The high 4 bits of each WORD are a relocation type
				
				if (relocationtype == IMAGE_REL_BASED_HIGHLOW)
				{
					DWORD32 val = *(DWORD32*)((BYTE*)sectionbase + offset) + (DWORD32)preloc->relocationoffset;
					memcpy((BYTE*)sectionbase + offset, &val, sizeof(val));
				}
				else if (relocationtype == IMAGE_REL_BASED_DIR64)
				{
					DWORD64 val = *(DWORD64*)((BYTE*)sectionbase + offset) + (DWORD64)preloc->relocationoffset;
					memcpy((BYTE*)sectionbase + offset, &val, sizeof(val));
				}
				filebuffer = (WORD*)filebuffer + 1;
			}
			else
			{
				//Skip block (-10ms)
				i = relocationitems;
				filebuffer = (WORD*)filebuffer + i;
			}
		}
	}
}

DWORD64 virtualaddressToFileAddress(DWORD64 virtualaddress, SECTION_HEADER_LIST* sectionheaders)
{
	while (sectionheaders)
	{
		if ( (virtualaddress >= (DWORD64)sectionheaders->pimagesectionheader->VirtualAddress) && (virtualaddress < ((DWORD64)(sectionheaders->pimagesectionheader->SizeOfRawData) + (DWORD64)(sectionheaders->pimagesectionheader->VirtualAddress))) )
		{
			return ( virtualaddress - (DWORD64)sectionheaders->pimagesectionheader->VirtualAddress + (DWORD64)(sectionheaders->pimagesectionheader->PointerToRawData) );
		}
		sectionheaders = sectionheaders->next;
	}
	return 0;
}

void sectionHeaderListAddLast(SECTION_HEADER_LIST** psectionheaders, SECTION_HEADER_LIST* shlnode)
{
	SECTION_HEADER_LIST* temp = NULL;

	if (psectionheaders)
	{
		if ( (temp = *psectionheaders) )
		{
			while (temp->next)
			{
				temp = temp->next;
			}
			temp->next = shlnode;
		}
		else
		{
			*psectionheaders = shlnode;
		}
	}
}

void patchListAddLast(PATCH_LIST** ppatches, PATCH_LIST* plnode)
{
	PATCH_LIST* temp = NULL;

	if (ppatches)
	{
		if ( (temp = *ppatches) )
		{
			while (temp->next)
			{
				temp = temp->next;
			}
			temp->next = plnode;
		}
		else
		{
			*ppatches = plnode;
		}
	}
}

void sectionHeaderListFree(SECTION_HEADER_LIST** psectionheaders)
{
	SECTION_HEADER_LIST* temp = NULL;

	if (psectionheaders)
	{
		while (*psectionheaders)
		{
			temp = *psectionheaders;
			*psectionheaders = (*psectionheaders)->next;
			free(temp);
			temp = NULL;
		}

	}
}

void patchListFree(PATCH_LIST** ppatches)
{
	PATCH_LIST* temp = NULL;

	if (ppatches)
	{
		while (*ppatches)
		{
			temp = *ppatches;
			*ppatches = (*ppatches)->next;
			free(temp);
			temp = NULL;
		}

	}
}

#ifndef GUI
void printPatchList(PATCH_LIST* patches)
{
	char* temp = NULL;

	if ( (temp = (char*)calloc(BUFFER_SIZE, 1)) )
	{
		if (patches)
		{
			fprintf(stdout, "%-52s %-52s %s\n\n", "[Module+Offset]", "[Original bytes]", "[Patched bytes]");
		}
		else
		{
			fprintf(stdout, "No patches were found\n");
		}

		while (patches)
		{
			_ui64toa(patches->patchedbytesoffset, temp, 16);
			_strupr(temp);
			strcat(patches->modulename, "+");
			strcat(patches->modulename, temp);
			fprintf(stdout, "%-52s %-52s %s\n", patches->modulename, patches->originalbytes, patches->patchedbytes);
			patches = patches->next;
		}
		free(temp);
		temp = NULL;
	}
}
#endif