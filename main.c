#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>
#include <shlwapi.h>
#include <process.h>
#include <psapi.h>
#include <stdio.h>
#include <time.h>

#define GUI //Comment this out for the console version

#ifdef GUI
#include <tlhelp32.h>
#include <commctrl.h>
#include <stdlib.h>
#endif

//CONSOLE:  -lntdll -lshlwapi
//GUI:      -lntdll -lshlwapi -lcomctl32 -mwindows

//MSVC only
#pragma comment(lib,"shlwapi.lib")
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define MAX_MODULES 128
#define MAX_SHOWN_PATCH_SIZE 16*3+1*2 //#16 -> "?? " + #1 -> "+\0" (worst case)

#ifdef GUI
#define MAX_PROCESSES 512
#define ID_SCAN_BUTTON 1001
#define ID_PROCESS_LIST 2001
#define ID_PATCH_LIST 2002
#define ID_PROGRESS_BAR_01 3001
#define ID_PROGRESS_BAR_02 3002
#define ID_MENU_PROCESS_LIST_REFRESH 4001
#endif

//Structs & Lists
typedef struct patch_list
{
	char moduleName[MAX_PATH];
	char originalBytes[MAX_SHOWN_PATCH_SIZE];
	char patchedBytes[MAX_SHOWN_PATCH_SIZE];
	DWORD64 patchedBytesOffset;
	unsigned long int patchedBytesCount;
	struct patch_list* next;
} PATCH_LIST;

typedef struct section_header_list
{
	PIMAGE_SECTION_HEADER pImageSectionHeader;
	struct section_header_list* next;
} SECTION_HEADER_LIST;

typedef struct
{
	PIMAGE_DATA_DIRECTORY pImageRelocationDataDirectory;
	PIMAGE_BASE_RELOCATION pImageBaseRelocation;
	DWORD64 relocationOffset;
} RELOC;

#ifdef GUI
typedef struct 
{
	char name[MAX_PATH];
	DWORD pid;
} WIN_PROCESS;
#endif

int busy = 0;

//Prototypes
int main();
DWORD loadFromFile(char* filename, char** buffer);
DWORD64 virtualAddressToFileAddress(DWORD64 virtualAddress, SECTION_HEADER_LIST* sectionHeaders);
void applyRelocation(void* fileBuffer, RELOC* pReloc, SECTION_HEADER_LIST* sectionHeaders);
void sectionHeaderListAddLast(SECTION_HEADER_LIST** pSectionHeaders, SECTION_HEADER_LIST* node);
void patchListAddLast(PATCH_LIST** pPatches, PATCH_LIST* node);
void sectionHeaderListFree(SECTION_HEADER_LIST** pSectionHeaders);
void patchListFree(PATCH_LIST** pPatches);
void printPatchList(PATCH_LIST* patches);

#ifdef GUI
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
void onWindowCreate(HWND);
void onScanButtonClick();
void onMenuItemRefreshClick();
void updateProcessList(WIN_PROCESS* processes);
void updatePatchList(PATCH_LIST* patches);
void getProcesses(WIN_PROCESS* processes);

HWND hwndMain = NULL;
HWND hwndScanButton = NULL;
HWND hwndProcessList = NULL;
HWND hwndPatchList = NULL;
HWND hwndScanProgressBar01 = NULL;
HWND hwndScanProgressBar02 = NULL;
HFONT hFont = NULL;

WIN_PROCESS processes[MAX_PROCESSES];

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow)
{
	BYTE className[] = "mainWindowClass";
	MSG msg;

	WNDCLASS wndClass = { 0 };
	wndClass.hInstance = hInst;
	wndClass.lpszClassName = (LPCSTR)className;
	wndClass.lpfnWndProc = WindowProc;
	wndClass.hbrBackground = (HBRUSH)GetSysColorBrush(COLOR_3DFACE);
	wndClass.style = CS_HREDRAW | CS_VREDRAW;
	wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);

	RegisterClass(&wndClass);

	/*
	[CreateWindow]:
	lpClassName
	lpWindowName
	dwStyle
	x
	y
	nWidth
	nHeight
	hWndParent
	hMenu
	hInstance
	lpParam
	*/
	hwndMain = CreateWindow((LPCSTR)className,
		"Patch Scanner",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		1112,
		360,
		NULL,
		NULL,
		hInst,
		NULL);

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
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_SCAN_BUTTON:
			onScanButtonClick();
			return 0;
		case ID_MENU_PROCESS_LIST_REFRESH:
			onMenuItemRefreshClick();
			return 0;
		}
	case WM_NOTIFY:
		switch (wParam)
		{
		case ID_PROCESS_LIST:
			if (((NMHDR*)lParam)->code == NM_RCLICK)
			{
				POINT p;
				if (GetCursorPos(&p))
				{
					HMENU hPopupMenu = CreatePopupMenu();
					InsertMenu(hPopupMenu, 0, MF_BYPOSITION | MF_STRING, ID_MENU_PROCESS_LIST_REFRESH, "Refresh");
					SetForegroundWindow(hwndMain);
					TrackPopupMenu(hPopupMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, p.x, p.y, 0, hwndMain, NULL);
				}
			}
			return 0;
		}
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void onWindowCreate(HWND hwnd)
{
	//[CreateWindow]: lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam
	hwndScanButton = CreateWindow("Button", "Scan", WS_CHILD | WS_VISIBLE, 15, 275, 350, 32, hwnd, (HMENU)ID_SCAN_BUTTON, NULL, NULL);
	hwndProcessList = CreateWindow("SysListView32", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS, 15, 15, 350, 250, hwnd, (HMENU)ID_PROCESS_LIST, NULL, NULL);
	hwndPatchList = CreateWindow("SysListView32", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS, 380, 15, 700, 250, hwnd, (HMENU)ID_PATCH_LIST, NULL, NULL);
	hwndScanProgressBar01 = CreateWindow("msctls_progress32", NULL, WS_CHILD | WS_VISIBLE, 380, 276, 700, 14, hwnd, (HMENU)ID_PROGRESS_BAR_01, NULL, NULL);
	hwndScanProgressBar02 = CreateWindow("msctls_progress32", NULL, WS_CHILD | WS_VISIBLE, 380, 292, 700, 14, hwnd, (HMENU)ID_PROGRESS_BAR_02, NULL, NULL);

	//Set Font
	hFont = CreateFont(19, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, TEXT("Segoe UI"));
	SendMessage(hwndScanButton, WM_SETFONT, (WPARAM)hFont, (LPARAM)0);
	SendMessage(hwndProcessList, WM_SETFONT, (WPARAM)hFont, (LPARAM)0);
	SendMessage(hwndPatchList, WM_SETFONT, (WPARAM)hFont, (LPARAM)0);

	//Init Lists
	LVCOLUMN lvc;
	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	//Process list
	lvc.iSubItem = 1;
	lvc.pszText = "Process name";
	lvc.cx = 300;
	ListView_InsertColumn(hwndProcessList, 0, &lvc);
	lvc.iSubItem = 0;
	lvc.pszText = "PID";
	lvc.cx = 100;
	ListView_InsertColumn(hwndProcessList, 0, &lvc);
	//Patch list
	lvc.iSubItem = 2;
	lvc.pszText = "Patched bytes";
	lvc.cx = 300;
	ListView_InsertColumn(hwndPatchList, 0, &lvc);
	lvc.iSubItem = 1;
	lvc.pszText = "Original bytes";
	lvc.cx = 300;
	ListView_InsertColumn(hwndPatchList, 0, &lvc);
	lvc.iSubItem = 0;
	lvc.pszText = "Address";
	lvc.cx = 180;
	ListView_InsertColumn(hwndPatchList, 0, &lvc);

	ListView_SetExtendedListViewStyle(hwndProcessList, LVS_EX_FULLROWSELECT);
	ListView_SetExtendedListViewStyle(hwndPatchList, LVS_EX_FULLROWSELECT);

	updateProcessList(processes);
}

void onScanButtonClick()
{
	if (!busy)
	{
		_beginthread((_beginthread_proc_type)main, 0, NULL);
	}
	else
	{
		MessageBoxA(hwndMain, "Scan in progress...", NULL, MB_ICONEXCLAMATION);
	}
}

void onMenuItemRefreshClick()
{
	updateProcessList(processes);
}

void updatePatchList(PATCH_LIST* patches)
{
	char temp[32] = { 0 };
	LVITEM lvi;
	lvi.mask = LVIF_TEXT;

	ListView_DeleteAllItems(hwndPatchList);

	for (int i = 0; patches; i++)
	{
		_itoa(patches->patchedBytesOffset, temp, 16);
		_strupr(temp);
		strcat(patches->moduleName, "+");
		strcat(patches->moduleName, temp);

		lvi.iItem = i;
		lvi.iSubItem = 0;
		lvi.pszText = patches->moduleName;
		ListView_InsertItem(hwndPatchList, &lvi);
		lvi.iSubItem = 1;
		lvi.pszText = patches->originalBytes;
		ListView_SetItem(hwndPatchList, &lvi);
		lvi.iSubItem = 2;
		lvi.pszText = patches->patchedBytes;
		ListView_SetItem(hwndPatchList, &lvi);

		patches = patches->next;
	}
}

void updateProcessList(WIN_PROCESS* processes)
{
	LVITEM lvi;
	DWORD currPos = 0;
	char pid[MAX_PATH] = { 0 };

	currPos = ListView_GetNextItem(hwndProcessList, -1, LVNI_SELECTED);

	lvi.mask = LVIF_TEXT;

	RtlZeroMemory(processes, sizeof(WIN_PROCESS) * MAX_PROCESSES);
	ListView_DeleteAllItems(hwndProcessList);

	getProcesses(processes);

	for (int i = 0; processes[i].pid | !i; i++)
	{
		lvi.iItem = i;
		lvi.iSubItem = 0;
		_itoa(processes[i].pid, pid, 10);
		lvi.pszText = pid;
		ListView_InsertItem(hwndProcessList, &lvi);
		lvi.iSubItem = 1;
		lvi.pszText = processes[i].name;
		ListView_SetItem(hwndProcessList, &lvi);
	}
	ListView_EnsureVisible(hwndProcessList, currPos, TRUE);
}

void getProcesses(WIN_PROCESS* processes)
{
	HANDLE hProcess = NULL;
	PROCESSENTRY32 processEntry;
	processEntry.dwSize = sizeof(PROCESSENTRY32);
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	if (hSnapshot != INVALID_HANDLE_VALUE && Process32First(hSnapshot, &processEntry))
	{
		int i = 0;
		do
		{
			strcpy(processes[i].name, processEntry.szExeFile);
			processes[i].pid = processEntry.th32ProcessID;
			i++;
		} while (Process32Next(hSnapshot, &processEntry));
	}
	CloseHandle(hSnapshot);
}
#endif

int main()
{
	//Heap
	void** fileBuffer = NULL; //Array - Loaded files
	void* vmBuffer = NULL; 
	char* moduleFileName = NULL; //2D Array - Module names + paths
	SECTION_HEADER_LIST* executableAndRelocSectionHeaders = NULL; //Singly Linked List - Executable sections + Relocation section (if available)
	PATCH_LIST* patches = NULL; //Singly Linked List
	RELOC* pReloc = NULL; //Struct

	//Stack
	HMODULE hModules[512] = { NULL };
	DWORD PID = 0;
	unsigned long int patchesCount = 0;
	unsigned long int modulesCount = 0;
	void* filePosition = NULL;
	PIMAGE_DOS_HEADER pImageDosHeader = NULL;
	PIMAGE_FILE_HEADER pImageFileHeader = NULL;
	PIMAGE_SECTION_HEADER pImageSectionHeader = NULL;
	PIMAGE_OPTIONAL_HEADER32 pImageOptionalHeader32 = NULL;
	PIMAGE_OPTIONAL_HEADER64 pImageOptionalHeader64 = NULL;

	busy = 1;

	//Collect input
#ifndef GUI
	setvbuf(stdout, NULL, _IONBF, 0);

	//Get process PID as user input
	fprintf(stdout, "Process ID: >");
	if (fscanf(stdin, "%lu", &PID) <= 0) return 0;

	//Clear shell
	system("@cls||clear");
#else
	char temp[16] = { 0 };
	ListView_GetItemText(hwndProcessList, ListView_GetNextItem(hwndProcessList, -1, LVNI_SELECTED), 0, temp, 16);
	PID = _atoi64(temp);
#endif

	//Execution time
	clock_t start = clock(), stop = 0;

	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PID);

	//Verify OpenProcess success
	if (!hProcess)
	{
		busy = 0;
		return 0;
	}

	//Allocate some buffers and zero-out the allocated memory (calloc)
	moduleFileName = (char*)calloc(MAX_MODULES + 1, MAX_PATH); //char moduleFileName[MAX_MODULES][MAX_PATH] = { 0 };

	//Collect process modules and paths
	DWORD cbNeeded = 0;
	if (EnumProcessModules(hProcess, hModules, sizeof(hModules), &cbNeeded))
	{
		for (int m = 0; hModules[m]; m++)
		{
			GetModuleFileNameEx(hProcess, hModules[m], moduleFileName+m*MAX_PATH, MAX_PATH);
		}
	}
	else
	{
#ifndef GUI
		fprintf(stdout, "Use 64bit version\r\n");
#else
		MessageBoxA(NULL, "Use 64bit version", "Error", MB_ICONERROR);
#endif
		busy = 0;
		return 0;
	}

	//Allocate some buffers and zero-out the allocated memory (calloc)
	fileBuffer = (void**)calloc((cbNeeded / sizeof(HMODULE)) + 1, sizeof(void*)); //void * fileBuffer[PROCESS_MODULES_COUNT] = { NULL };
	pReloc = (RELOC*)calloc(1, sizeof(RELOC));

	//Load modules (files)
	for (int m = 0; hModules[m]; m++, modulesCount++)
	{
		loadFromFile(moduleFileName + m * MAX_PATH, (char**)(fileBuffer + m));
	}

	//Set progressbar range and increment + empty patch listview
#ifdef GUI
	SendMessage(hwndScanProgressBar02, PBM_SETRANGE, 0, MAKELPARAM(0, modulesCount));
	SendMessage(hwndScanProgressBar02, PBM_SETSTEP, (WPARAM)1, 0);
	SendMessage(hwndScanProgressBar02, PBM_SETPOS, 0, 0);
	ListView_DeleteAllItems(hwndPatchList);
#endif

	for (int m = 0; fileBuffer[m]; m++)
	{
		filePosition = fileBuffer[m];
		pImageDosHeader = (PIMAGE_DOS_HEADER)filePosition;

		//Make sure it's a PE
		if (pImageDosHeader->e_magic == IMAGE_DOS_SIGNATURE)
		{
			//Gather needed PE info
			filePosition = (BYTE*)filePosition + pImageDosHeader->e_lfanew + sizeof(DWORD32);
			pImageFileHeader = (PIMAGE_FILE_HEADER)filePosition;
			filePosition = (BYTE*)filePosition + sizeof(IMAGE_FILE_HEADER);
			if (pImageFileHeader->Machine == IMAGE_FILE_MACHINE_AMD64)
			{
				pImageOptionalHeader64 = (PIMAGE_OPTIONAL_HEADER64)filePosition;
				pReloc->relocationOffset = (DWORD64)((BYTE*)hModules[m] - pImageOptionalHeader64->ImageBase);
				pReloc->pImageRelocationDataDirectory = (PIMAGE_DATA_DIRECTORY)(((BYTE*)filePosition + 0x70) + sizeof(IMAGE_DATA_DIRECTORY) * 5);
			}
			else
			{
				pImageOptionalHeader32 = (PIMAGE_OPTIONAL_HEADER32)filePosition;
				pReloc->relocationOffset = (DWORD64)((BYTE*)hModules[m] - pImageOptionalHeader32->ImageBase);
				pReloc->pImageRelocationDataDirectory = (PIMAGE_DATA_DIRECTORY)(((BYTE*)filePosition + 0x60) + sizeof(IMAGE_DATA_DIRECTORY) * 5);
			}
			filePosition = (BYTE*)filePosition + pImageFileHeader->SizeOfOptionalHeader;

			//Filter executable sections (& .reloc)
			for (int i = 0; i < pImageFileHeader->NumberOfSections; i++, filePosition = (BYTE*)filePosition + sizeof(IMAGE_SECTION_HEADER))
			{
				pImageSectionHeader = (PIMAGE_SECTION_HEADER)filePosition;
				if ((pImageSectionHeader->Characteristics & IMAGE_SCN_CNT_CODE) || !strcmp((char*)pImageSectionHeader->Name, ".reloc"))
				{
					SECTION_HEADER_LIST* node = (SECTION_HEADER_LIST*)calloc(1, sizeof(SECTION_HEADER_LIST));
					node->pImageSectionHeader = (PIMAGE_SECTION_HEADER)filePosition;
					node->next = NULL;
					sectionHeaderListAddLast(&executableAndRelocSectionHeaders, node);
				}
			}

			//Apply relocation
			if (pReloc->relocationOffset)
			{
				applyRelocation(fileBuffer[m], pReloc, executableAndRelocSectionHeaders);
			}

			//Scan for current module's patches
			SECTION_HEADER_LIST* sectionHeaders = executableAndRelocSectionHeaders;
			for (;sectionHeaders; sectionHeaders = sectionHeaders->next)
			{
				//Skip .reloc
				if (!strcmp((char*)sectionHeaders->pImageSectionHeader->Name, ".reloc"))
					continue;

				vmBuffer = malloc(sectionHeaders->pImageSectionHeader->SizeOfRawData);
				if (vmBuffer)
				{
#ifdef _WIN64
					long long unsigned int byteRead = 0;
#else
					long unsigned int byteRead = 0;
#endif
					long unsigned int position = 0;
					ReadProcessMemory(hProcess, (BYTE*)hModules[m] + sectionHeaders->pImageSectionHeader->VirtualAddress, vmBuffer, sectionHeaders->pImageSectionHeader->SizeOfRawData, &byteRead);

					//Verify ReadProcessMemory success
					if (byteRead)
					{
						filePosition = (BYTE*)fileBuffer[m] + sectionHeaders->pImageSectionHeader->PointerToRawData;
						position += RtlCompareMemory(vmBuffer, filePosition, sectionHeaders->pImageSectionHeader->SizeOfRawData);

						//Set progressbar range and increment
#ifdef GUI
						SendMessage(hwndScanProgressBar01, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
						SendMessage(hwndScanProgressBar01, PBM_SETSTEP, (WPARAM)1, 0);
						SendMessage(hwndScanProgressBar01, PBM_SETPOS, 0, 0);
#endif

						//For each patch found a node is allocated, filled with the needed info and added to the patch list
						int k = 0;
						while (position < sectionHeaders->pImageSectionHeader->SizeOfRawData)
						{
							//Allocate node
							PATCH_LIST* node = (PATCH_LIST*)calloc(1, sizeof(PATCH_LIST));
							//Fill node
							node->patchedBytesCount = 0;
							strcpy(node->moduleName, moduleFileName + m * MAX_PATH);
							//Remove path for a 'better' output
							PathStripPathA(node->moduleName);
							node->patchedBytesOffset = position;

							while ((position <= sectionHeaders->pImageSectionHeader->SizeOfRawData) && (*((BYTE*)(vmBuffer)+position) != *((BYTE*)(filePosition)+position)))
							{
								if ((k + 1) * 3 < MAX_SHOWN_PATCH_SIZE && k != -1)
								{
									snprintf(node->originalBytes + k * 3, 4, "%.2X ", *((BYTE*)filePosition + node->patchedBytesOffset + k));
									snprintf(node->patchedBytes + k * 3, 4, "%.2X ", *((BYTE*)vmBuffer + node->patchedBytesOffset + k));
									k++;
								}
								else if (k != -1)
								{
									snprintf(node->originalBytes + k * 3, 4, "+");
									snprintf(node->patchedBytes + k * 3, 4, "+");
									k = -1;
								}
								node->patchedBytesCount++;
								position++;
							}
							node->patchedBytesOffset += sectionHeaders->pImageSectionHeader->VirtualAddress;
							node->next = NULL;
							//Add node
							patchListAddLast(&patches, node);

							position += RtlCompareMemory((BYTE*)vmBuffer + position, (BYTE*)filePosition + position, sectionHeaders->pImageSectionHeader->SizeOfRawData);
							patchesCount++;
							k = 0;
#ifdef GUI
							SendMessage(hwndScanProgressBar01, PBM_SETPOS, (WPARAM)((long long unsigned int)position * 100 / sectionHeaders->pImageSectionHeader->SizeOfRawData), 0);
#endif
						}
#ifdef GUI
						SendMessage(hwndScanProgressBar01, PBM_SETPOS, (WPARAM)((long long unsigned int)position * 100 / sectionHeaders->pImageSectionHeader->SizeOfRawData), 0);
#endif
					}
					free(vmBuffer);
					vmBuffer = NULL;
				}
			}
			sectionHeaderListFree(&executableAndRelocSectionHeaders);
		}
		free(fileBuffer[m]);
		fileBuffer[m] = NULL;
		hModules[m] = NULL;

		//Progressbar step
#ifdef GUI
		SendMessage(hwndScanProgressBar02, PBM_STEPIT, 0, 0);
#endif
	}

#ifndef GUI
	//Print results
	printPatchList(patches);
#else
	updatePatchList(patches);
#endif

	//Free list
	patchListFree(&patches);

	//Zero-out moduleFileName
	RtlZeroMemory(moduleFileName, MAX_MODULES * MAX_PATH);

	//Show execution time
	stop = clock();

#ifndef GUI
	fprintf(stdout, "\n\n(Execution time: %f seconds)\n", (double)(stop - start) / CLOCKS_PER_SEC);
#else
	//not now
#endif
	busy = 0;
	return 0;
}

DWORD loadFromFile(char* filename, char** buffer)
{
	HANDLE hFile = NULL;
	DWORD bufferSize = 0;
	DWORD bytesRead = 0;

	if ((hFile = CreateFile(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) != INVALID_HANDLE_VALUE)
	{
		if ((bufferSize = GetFileSize(hFile, NULL)) > 0)
		{
			*buffer = (char*)malloc(bufferSize);
			if (*buffer)
			{
				if (!ReadFile(hFile, *buffer, bufferSize, &bytesRead, NULL))
				{
					free(*buffer);
				}
			}
		}
		CloseHandle(hFile);
	}
	return bufferSize;
}

void applyRelocation(void *fileBuffer, RELOC *pReloc, SECTION_HEADER_LIST* sectionHeaders)
{
	void* sectionBase = NULL;
	void* fileBase = NULL;
	void* endOfRelocationDir = NULL;

	fileBase = fileBuffer;
	fileBuffer = (BYTE*)fileBuffer + virtualAddressToFileAddress(pReloc->pImageRelocationDataDirectory->VirtualAddress, sectionHeaders);
	endOfRelocationDir = (BYTE*)fileBuffer + pReloc->pImageRelocationDataDirectory->Size;

	while (fileBuffer < endOfRelocationDir)
	{
		pReloc->pImageBaseRelocation = (PIMAGE_BASE_RELOCATION)fileBuffer;
		size_t relocationItems = (pReloc->pImageBaseRelocation->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD); //To determine the number of relocations in this block, subtract the size of an IMAGE_BASE_RELOCATION (8 bytes) from the value of this field, and then divide by 2 (the size of a WORD)
		fileBuffer = (BYTE*)fileBuffer + sizeof(IMAGE_BASE_RELOCATION);
		if (sectionBase = (void*)virtualAddressToFileAddress(pReloc->pImageBaseRelocation->VirtualAddress, sectionHeaders))
			sectionBase = (BYTE*)sectionBase + (DWORD64)fileBase;

		for (int i = 0; i < relocationItems; i++)
		{
			if (sectionBase)
			{
				DWORD offset = (*(WORD*)fileBuffer) & 0xfff; //The bottom 12 bits of each WORD are a relocation offset
				DWORD relocationType = (*(WORD*)fileBuffer) >> 12; //The high 4 bits of each WORD are a relocation type
				
				if (relocationType == IMAGE_REL_BASED_HIGHLOW)
				{
					DWORD32 val = *(DWORD32*)((BYTE*)sectionBase + offset) + pReloc->relocationOffset;
					memcpy((BYTE*)sectionBase + offset, &val, sizeof(val));
				}
				else if (relocationType == IMAGE_REL_BASED_DIR64)
				{
					DWORD64 val = *(DWORD64*)((BYTE*)sectionBase + offset) + pReloc->relocationOffset;
					memcpy((BYTE*)sectionBase + offset, &val, sizeof(val));
				}
				fileBuffer = (WORD*)fileBuffer + 1;
			}
			else
			{
				//Skip block (-10ms)
				i = relocationItems;
				fileBuffer = (WORD*)fileBuffer + i;
			}
		}
	}
}

DWORD64 virtualAddressToFileAddress(DWORD64 virtualAddress, SECTION_HEADER_LIST* sectionHeaders)
{
	while (sectionHeaders)
	{
		if (virtualAddress >= (DWORD64)sectionHeaders->pImageSectionHeader->VirtualAddress && virtualAddress < (DWORD64)(sectionHeaders->pImageSectionHeader->SizeOfRawData + sectionHeaders->pImageSectionHeader->VirtualAddress))
		{
			return virtualAddress - (DWORD64)sectionHeaders->pImageSectionHeader->VirtualAddress + (DWORD64)sectionHeaders->pImageSectionHeader->PointerToRawData;
		}
		sectionHeaders = sectionHeaders->next;
	}
	return 0;
}

void sectionHeaderListAddLast(SECTION_HEADER_LIST** pSectionHeaders, SECTION_HEADER_LIST* node)
{
	if (pSectionHeaders)
	{
		if (*pSectionHeaders)
		{
			SECTION_HEADER_LIST* temp = *pSectionHeaders;
			while (temp->next)
			{
				temp = temp->next;
			}
			temp->next = node;
		}
		else
		{
			*pSectionHeaders = node;
		}
	}
}

void patchListAddLast(PATCH_LIST** pPatches, PATCH_LIST* node)
{
	if (pPatches)
	{
		if (*pPatches)
		{
			PATCH_LIST* temp = *pPatches;
			while (temp->next)
			{
				temp = temp->next;
			}
			temp->next = node;
		}
		else
		{
			*pPatches = node;
		}
	}
}

void sectionHeaderListFree(SECTION_HEADER_LIST** pSectionHeaders)
{
	if (pSectionHeaders)
	{
		SECTION_HEADER_LIST* temp = NULL;
		while (*pSectionHeaders)
		{
			temp = *pSectionHeaders;
			*pSectionHeaders = (*pSectionHeaders)->next;
			free(temp);
		}

	}
}

void patchListFree(PATCH_LIST** pPatches)
{
	if (pPatches)
	{
		PATCH_LIST* temp = NULL;
		while (*pPatches)
		{
			temp = *pPatches;
			*pPatches = (*pPatches)->next;
			free(temp);
		}

	}
}

#ifndef GUI
void printPatchList(PATCH_LIST* patches)
{
	char temp[32] = { 0 };

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
		_itoa(patches->patchedBytesOffset, temp, 16);
		_strupr(temp);
		strcat(patches->moduleName, "+");
		strcat(patches->moduleName, temp);
		fprintf(stdout, "%-52s %-52s %s\n", patches->moduleName, patches->originalBytes, patches->patchedBytes);
		patches = patches->next;
	}
}
#endif