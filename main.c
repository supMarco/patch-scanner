#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>
#include <shlwapi.h>
#include <psapi.h>
#include <stdio.h>
#include <time.h>

#pragma comment(lib,"shlwapi.lib") //MSVC only
#pragma comment(lib,"ntdll.lib") //MSVC only

#define MAX_MODULES 128
#define MAX_SHOWN_PATCH_SIZE 16*3+1*2 //#16 -> "?? " + #1 -> "+\0" (worst case)

//#define GUI

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

//Prototypes
DWORD loadFromFile(char* filename, char** buffer);
DWORD64 virtualAddressToFileAddress(DWORD64 virtualAddress, SECTION_HEADER_LIST* sectionHeaders);
void applyRelocation(void* fileBuffer, RELOC* pReloc, SECTION_HEADER_LIST* sectionHeaders);
void sectionHeaderListAddLast(SECTION_HEADER_LIST** pSectionHeaders, SECTION_HEADER_LIST* node);
void patchListAddLast(PATCH_LIST** pPatches, PATCH_LIST* node);
void sectionHeaderListFree(SECTION_HEADER_LIST** pSectionHeaders);
void patchListFree(PATCH_LIST** pPatches);
void printPatchList(PATCH_LIST* patches);

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
	size_t patchesCount = 0;
	void* filePositionA = NULL;
	void* filePositionB = NULL;
	PIMAGE_DOS_HEADER pImageDosHeader = NULL;
	PIMAGE_FILE_HEADER pImageFileHeader = NULL;
	PIMAGE_SECTION_HEADER pImageSectionHeader = NULL;
	PIMAGE_OPTIONAL_HEADER32 pImageOptionalHeader32 = NULL;
	PIMAGE_OPTIONAL_HEADER64 pImageOptionalHeader64 = NULL;


	setvbuf(stdout, NULL, _IONBF, 0);

	//Get process PID as user input
	fprintf(stdout, "Process ID: >");
	if (fscanf(stdin, "%lu", &PID) <= 0) return 0;

	//Clear shell
	system("@cls||clear");

	//Execution time
	clock_t start = clock(), stop = 0;

	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PID);

	//Verify OpenProcess success
	if (!hProcess) return 0;

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
		fprintf(stdout, "Use 64bit version\r\n");
		return 0;
	}

	//Allocate some buffers and zero-out the allocated memory (calloc)
	fileBuffer = (void**)calloc((cbNeeded / sizeof(HMODULE)) + 1, sizeof(void*)); //void * fileBuffer[PROCESS_MODULES_COUNT] = { NULL };
	pReloc = (RELOC*)calloc(1, sizeof(RELOC));

	//Load modules (files)
	for (int m = 0; hModules[m]; m++)
	{
		loadFromFile(moduleFileName + m * MAX_PATH, (char**)(fileBuffer + m));
	}

	for (int m = 0; fileBuffer[m]; m++)
	{
		filePositionA = fileBuffer[m];
		pImageDosHeader = (PIMAGE_DOS_HEADER)filePositionA;

		//Make sure it's a PE
		if (pImageDosHeader->e_magic == IMAGE_DOS_SIGNATURE)
		{
			//Gather needed PE info
			filePositionA = (BYTE*)filePositionA + pImageDosHeader->e_lfanew + sizeof(DWORD32);
			pImageFileHeader = (PIMAGE_FILE_HEADER)filePositionA;
			filePositionA = (BYTE*)filePositionA + sizeof(IMAGE_FILE_HEADER);
			if (pImageFileHeader->Machine == IMAGE_FILE_MACHINE_AMD64)
			{
				pImageOptionalHeader64 = (PIMAGE_OPTIONAL_HEADER64)filePositionA;
				pReloc->relocationOffset = (DWORD64)((BYTE*)hModules[m] - pImageOptionalHeader64->ImageBase);
				pReloc->pImageRelocationDataDirectory = (PIMAGE_DATA_DIRECTORY)(((BYTE*)filePositionA + 0x70) + sizeof(IMAGE_DATA_DIRECTORY) * 5);
			}
			else
			{
				pImageOptionalHeader32 = (PIMAGE_OPTIONAL_HEADER32)filePositionA;
				pReloc->relocationOffset = (DWORD64)((BYTE*)hModules[m] - pImageOptionalHeader32->ImageBase);
				pReloc->pImageRelocationDataDirectory = (PIMAGE_DATA_DIRECTORY)(((BYTE*)filePositionA + 0x60) + sizeof(IMAGE_DATA_DIRECTORY) * 5);
			}
			filePositionA = (BYTE*)filePositionA + pImageFileHeader->SizeOfOptionalHeader;

			//Filter executable sections (& .reloc)
			for (int i = 0; i < pImageFileHeader->NumberOfSections; i++, filePositionA = (BYTE*)filePositionA + sizeof(IMAGE_SECTION_HEADER))
			{
				pImageSectionHeader = (PIMAGE_SECTION_HEADER)filePositionA;
				if ((pImageSectionHeader->Characteristics & IMAGE_SCN_CNT_CODE) || !strcmp((char*)pImageSectionHeader->Name, ".reloc"))
				{
					SECTION_HEADER_LIST* node = calloc(1, sizeof(SECTION_HEADER_LIST));
					node->pImageSectionHeader = (PIMAGE_SECTION_HEADER)filePositionA;
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
					size_t position = 0;
					ReadProcessMemory(hProcess, (BYTE*)hModules[m] + sectionHeaders->pImageSectionHeader->VirtualAddress, vmBuffer, sectionHeaders->pImageSectionHeader->SizeOfRawData, &byteRead);

					//Verify ReadProcessMemory success
					if (byteRead)
					{
						filePositionB = (BYTE*)fileBuffer[m] + sectionHeaders->pImageSectionHeader->PointerToRawData;
						position += RtlCompareMemory(vmBuffer, filePositionB, sectionHeaders->pImageSectionHeader->SizeOfRawData);

						//For each patch found a node is allocated, filled with the needed info and added to the patch list
						int k = 0;
						while (position < sectionHeaders->pImageSectionHeader->SizeOfRawData)
						{
							//Allocate node
							PATCH_LIST* node = calloc(1, sizeof(PATCH_LIST));
							//Fill node
							node->patchedBytesCount = 0;
							strcpy(node->moduleName, moduleFileName + m * MAX_PATH);
							//Remove path for a 'better' output
							PathStripPathA(node->moduleName);
							node->patchedBytesOffset = position;

							while ((position <= sectionHeaders->pImageSectionHeader->SizeOfRawData) && (*((BYTE*)(vmBuffer)+position) != *((BYTE*)(filePositionB)+position)))
							{
								if ((k + 1) * 3 < MAX_SHOWN_PATCH_SIZE && k != -1)
								{
									snprintf(node->originalBytes + k * 3, 4, "%.2X ", *((BYTE*)filePositionB + node->patchedBytesOffset + k));
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

							position += RtlCompareMemory((BYTE*)vmBuffer + position, (BYTE*)filePositionB + position, sectionHeaders->pImageSectionHeader->SizeOfRawData);
							patchesCount++;
							k = 0;
						}
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
	}

	//Print results
	printPatchList(patches);

	//Free list
	patchListFree(&patches);

	//Zero-out moduleFileName
	RtlZeroMemory(moduleFileName, MAX_MODULES * MAX_PATH);

	//Show execution time
	stop = clock();
	fprintf(stdout, "\n\n(Execution time: %f seconds)\n", (double)(stop - start) / CLOCKS_PER_SEC);

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