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

#define MAX_SECTIONS 64
#define MAX_MODULES 128
#define MAX_PATCHES 512
#define MAX_SHOWN_PATCH_SIZE 16*3+2

typedef struct
{
	char moduleName[MAX_PATH];
	char originalBytes[MAX_SHOWN_PATCH_SIZE];
	char patchedBytes[MAX_SHOWN_PATCH_SIZE];
	DWORD patchedBytesOffset;
	size_t patchedBytesCount;
} patch;

typedef struct
{
	PIMAGE_DATA_DIRECTORY pImageRelocationDataDirectory;
	PIMAGE_BASE_RELOCATION pImageBaseRelocation;
	DWORD64 relocationOffset;
} reloc;

DWORD loadFromFile(char* filename, char** buffer);
void applyRelocation(void* fileBuffer, reloc* pRelocStruct, PIMAGE_SECTION_HEADER* pImageExecutableSectionHeaders);
DWORD64 virtualAddressToFileAddress(DWORD64 virtualAddress, PIMAGE_SECTION_HEADER* pImageSectionHeaders);
void printScanResult(patch** patches);

int main()
{
	//===========================================================================================

	//Heap
	void** fileBuffer = NULL; //Array - Loaded files
	void* vmBuffer = NULL; 
	char* moduleFileName = NULL; //2D Array - Module names + paths
	PIMAGE_SECTION_HEADER* pImageExecutableSectionHeaders = NULL; //Array - Executable sections + Relocation section (if available)
	patch** patches = NULL; //Array - patch structs pointers
	reloc* pRelocStruct = NULL;

	//Stack
	HMODULE hModules[512] = { NULL };
	int PID = 0;
	size_t patchesCount = 0;
	void* filePositionA = NULL;
	void* filePositionB = NULL;
	PIMAGE_DOS_HEADER pImageDosHeader = NULL;
	PIMAGE_FILE_HEADER pImageFileHeader = NULL;
	PIMAGE_SECTION_HEADER pImageSectionHeader = NULL;
	PIMAGE_OPTIONAL_HEADER32 pImageOptionalHeader32 = NULL;
	PIMAGE_OPTIONAL_HEADER64 pImageOptionalHeader64 = NULL;

	//===========================================================================================
	setvbuf(stdout, NULL, _IONBF, 0);

	//Get process PID as user input
	fprintf(stdout, "Process ID: >");
	if (fscanf(stdin, "%d", &PID) <= 0) return 0;

	//Clear shell
	system("@cls||clear");

	//Execution time
	clock_t start = clock(), stop = 0;

	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PID);

	//Verify OpenProcess success
	if (!hProcess) return 0;

	//Allocate some buffers and zero-out the allocated memory (calloc)
	moduleFileName = (char*)calloc(MAX_MODULES + 1, MAX_PATH); //char moduleFileName[MAX_MODULES][MAX_PATH] = { 0 };
	pImageExecutableSectionHeaders = (PIMAGE_SECTION_HEADER*)calloc(MAX_SECTIONS + 1, sizeof(PIMAGE_SECTION_HEADER)); //PIMAGE_SECTION_HEADER pImageExecutableSectionHeaders[MAX_SECTIONS] = { NULL };
	patches = (patch**)calloc(MAX_PATCHES + 1, sizeof(patch*)); //patch *patches[MAX_PATCHES] = { NULL };
	pRelocStruct = (reloc*)calloc(1, sizeof(reloc));

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

	//Allocate memory for each module
	fileBuffer = (void**)calloc((cbNeeded / sizeof(HMODULE)) + 1, sizeof(void*)); //void * fileBuffer[PROCESS_MODULES_COUNT] = { NULL };

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
				pRelocStruct->relocationOffset = (DWORD64)(hModules[m] - pImageOptionalHeader64->ImageBase);
				pRelocStruct->pImageRelocationDataDirectory = (PIMAGE_DATA_DIRECTORY)(((BYTE*)filePositionA + 0x70) + sizeof(IMAGE_DATA_DIRECTORY) * 5);
			}
			else
			{
				pImageOptionalHeader32 = (PIMAGE_OPTIONAL_HEADER32)filePositionA;
				pRelocStruct->relocationOffset = (DWORD64)(hModules[m] - pImageOptionalHeader32->ImageBase);
				pRelocStruct->pImageRelocationDataDirectory = (PIMAGE_DATA_DIRECTORY)(((BYTE*)filePositionA + 0x60) + sizeof(IMAGE_DATA_DIRECTORY) * 5);
			}
			filePositionA = (BYTE*)filePositionA + pImageFileHeader->SizeOfOptionalHeader;

			//Filter executable sections (& .reloc)
			for (int i = 0, j = 0; i < pImageFileHeader->NumberOfSections; i++, filePositionA = (BYTE*)filePositionA + sizeof(IMAGE_SECTION_HEADER))
			{
				pImageSectionHeader = (PIMAGE_SECTION_HEADER)filePositionA;
				if ((pImageSectionHeader->Characteristics & IMAGE_SCN_CNT_CODE) || !strcmp((char*)pImageSectionHeader->Name, ".reloc"))
				{
					pImageExecutableSectionHeaders[j++] = (PIMAGE_SECTION_HEADER)filePositionA;
				}
			}

			//Apply relocation
			if (pRelocStruct->relocationOffset)
			{
				applyRelocation(fileBuffer[m], pRelocStruct, pImageExecutableSectionHeaders);
			}

			//Scan for current module's patches
			for (int n = 0; pImageExecutableSectionHeaders[n]; n++)
			{
				//Skip .reloc
				if (!strcmp((char*)pImageExecutableSectionHeaders[n]->Name, ".reloc"))
					continue;

				vmBuffer = malloc(pImageExecutableSectionHeaders[n]->SizeOfRawData);
				if (vmBuffer)
				{
#ifdef _WIN64
					long long unsigned int byteRead = 0;
#else
					long unsigned int byteRead = 0;
#endif
					size_t position = 0;
					ReadProcessMemory(hProcess, (BYTE*)hModules[m] + pImageExecutableSectionHeaders[n]->VirtualAddress, vmBuffer, pImageExecutableSectionHeaders[n]->SizeOfRawData, &byteRead);
					filePositionB = (BYTE*)fileBuffer[m] + pImageExecutableSectionHeaders[n]->PointerToRawData;
					position += RtlCompareMemory(vmBuffer, filePositionB, pImageExecutableSectionHeaders[n]->SizeOfRawData);

					//For each patch found a patch struct is allocated and filled with the needed info
					for (int l = patchesCount, k = 0; (position < pImageExecutableSectionHeaders[n]->SizeOfRawData) && l < MAX_PATCHES; l++)
					{
						//Allocate a patch struct and fill it
						patches[l] = (patch*)malloc(sizeof(patch));
						patches[l]->patchedBytesCount = 0;

						strcpy(patches[l]->moduleName, moduleFileName+m*MAX_PATH);
						PathStripPathA(patches[l]->moduleName);

						patches[l]->patchedBytesOffset = position;

						while ((position <= pImageExecutableSectionHeaders[n]->SizeOfRawData) && (*((BYTE*)(vmBuffer)+position) != *((BYTE*)(filePositionB)+position)))
						{
							if ((k + 1) * 3 < MAX_SHOWN_PATCH_SIZE && k != -1)
							{
								snprintf(patches[l]->originalBytes + k * 3, 4, "%.2X ", *((BYTE*)filePositionB + patches[l]->patchedBytesOffset + k));
								snprintf(patches[l]->patchedBytes + k * 3, 4, "%.2X ", *((BYTE*)vmBuffer + patches[l]->patchedBytesOffset + k));
								k++;
							}
							else if (k != -1)
							{
								snprintf(patches[l]->originalBytes + k * 3, 4, "+");
								snprintf(patches[l]->patchedBytes + k * 3, 4, "+");
								k = -1;
							}
							patches[l]->patchedBytesCount++;
							position++;
						}
						patches[l]->patchedBytesOffset += pImageExecutableSectionHeaders[n]->VirtualAddress;
						position += RtlCompareMemory((BYTE*)vmBuffer + position, (BYTE*)filePositionB + position, pImageExecutableSectionHeaders[n]->SizeOfRawData);
						patchesCount++;
						k = 0;
					}
					free(vmBuffer);
					vmBuffer = NULL;
				}
			}
			for (int p = 0; pImageExecutableSectionHeaders[p]; p++)
			{
				pImageExecutableSectionHeaders[p] = NULL;
			}
		}
		free(fileBuffer[m]);
		fileBuffer[m] = NULL;
		hModules[m] = NULL;
	}

	//Print results
	printScanResult(patches);

	//Free remaining buffers
	for (int k = 0; patches[k]; k++)
	{
		free(patches[k]);
		patches[k] = NULL;
	}

	//Zero-out moduleFileName
	for (int k = 0; k < (MAX_MODULES * MAX_PATH); k++)
	{
		moduleFileName[k] = 0;
	}

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

void applyRelocation(void *fileBuffer, reloc *pRelocStruct, PIMAGE_SECTION_HEADER* pImageSectionHeaders)
{
	void* sectionBase = NULL;
	void* fileBase = NULL;
	void* endOfRelocationDir = 0;

	fileBase = fileBuffer;
	fileBuffer = (BYTE*)fileBuffer + virtualAddressToFileAddress(pRelocStruct->pImageRelocationDataDirectory->VirtualAddress, pImageSectionHeaders);
	endOfRelocationDir = (BYTE*)fileBuffer + pRelocStruct->pImageRelocationDataDirectory->Size;

	while (fileBuffer < endOfRelocationDir)
	{
		pRelocStruct->pImageBaseRelocation = (PIMAGE_BASE_RELOCATION)fileBuffer;
		size_t relocationItems = (pRelocStruct->pImageBaseRelocation->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD); //To determine the number of relocations in this block, subtract the size of an IMAGE_BASE_RELOCATION (8 bytes) from the value of this field, and then divide by 2 (the size of a WORD)
		fileBuffer = (BYTE*)fileBuffer + sizeof(IMAGE_BASE_RELOCATION);
		if (sectionBase = (void*)virtualAddressToFileAddress(pRelocStruct->pImageBaseRelocation->VirtualAddress, pImageSectionHeaders))
			sectionBase = (BYTE*)sectionBase + (DWORD64)fileBase;

		for (int i = 0; i < relocationItems; i++)
		{
			if (sectionBase)
			{
				DWORD offset = (*(WORD*)fileBuffer) & 0xfff; //The bottom 12 bits of each WORD are a relocation offset
				DWORD relocationType = (*(WORD*)fileBuffer) >> 12; //The high 4 bits of each WORD are a relocation type
				
				if (relocationType == IMAGE_REL_BASED_HIGHLOW)
				{
					DWORD32 val = *(DWORD32*)((BYTE*)sectionBase + offset) + pRelocStruct->relocationOffset;
					memcpy((BYTE*)sectionBase + offset, &val, sizeof(val));
				}
				else if (relocationType == IMAGE_REL_BASED_DIR64)
				{
					DWORD64 val = *(DWORD64*)((BYTE*)sectionBase + offset) + pRelocStruct->relocationOffset;
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

DWORD64 virtualAddressToFileAddress(DWORD64 virtualAddress, PIMAGE_SECTION_HEADER* pImageSectionHeaders)
{
	for (int i = 0; pImageSectionHeaders[i]; i++)
	{
		if (virtualAddress >= pImageSectionHeaders[i]->VirtualAddress && virtualAddress < pImageSectionHeaders[i]->SizeOfRawData + pImageSectionHeaders[i]->VirtualAddress)
			return virtualAddress - pImageSectionHeaders[i]->VirtualAddress + pImageSectionHeaders[i]->PointerToRawData;
	}
	return 0;
}

void printScanResult(patch **patches)
{
	char temp[256];
	fprintf(stdout, "%-52s %-52s %s\n\n", "[Module+Offset]", "[Original bytes]", "[Patched bytes]");
	for (int k = 0; patches[k]; k++)
	{
		_itoa(patches[k]->patchedBytesOffset, temp, 16);
		_strupr(temp);
		strcat(patches[k]->moduleName, "+");
		strcat(patches[k]->moduleName, temp);
		fprintf(stdout, "%-52s %-52s %s\n", patches[k]->moduleName, patches[k]->originalBytes, patches[k]->patchedBytes);
	}
}