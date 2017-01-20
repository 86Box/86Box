/* Copyright holders: Riley
   see COPYING for more details
   
   win-crashdump.c : Windows exception handler to make a crash dump just before a crash happens.
*/
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <psapi.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "86box.h"
#include "win-crashdump.h"

PVOID hExceptionHandler;
char* ExceptionHandlerBuffer;
#define ExceptionHandlerBufferSize (10240)


LONG CALLBACK MakeCrashDump(PEXCEPTION_POINTERS ExceptionInfo) {
	// Win32-specific functions will be used wherever possible, just in case the C stdlib-equivalents try to allocate memory.
	// (The Win32-specific functions are generally just wrappers over NT system calls anyway.)
	
	if ((ExceptionInfo->ExceptionRecord->ExceptionCode >> 28) != 0xC) {
		// The exception code is not a fatal exception (highest 4 bits of ntstatus = 0xC)
		// Not going to crash, let's not make a crash dump
		return EXCEPTION_CONTINUE_SEARCH;
	}
	
	// So, the program is about to crash. Oh no what do?
	// Let's create a crash dump file as a debugging-aid.
	
	// First, get the path to 86Box.exe.
	char* CurrentBufferPointer;
	GetModuleFileName(NULL,ExceptionHandlerBuffer,ExceptionHandlerBufferSize);
	if (GetLastError() != ERROR_SUCCESS) {
		// Could not get the full path of 86Box.exe. Just create the file in the current directory.
		CurrentBufferPointer = ExceptionHandlerBuffer;
	} else {
		// Walk through the string backwards looking for the last backslash, so as to remove the "86Box.exe" filename from the string.
		CurrentBufferPointer = &ExceptionHandlerBuffer[strlen(ExceptionHandlerBuffer)];
		for (; CurrentBufferPointer > ExceptionHandlerBuffer; CurrentBufferPointer--) {
			if (CurrentBufferPointer[0] == '\\') {
				// Found the backslash, null terminate the string after it.
				CurrentBufferPointer[1] = 0;
				break;
			}
		}
		
		CurrentBufferPointer = &ExceptionHandlerBuffer[strlen(ExceptionHandlerBuffer)];
	}
	
	// What would a good filename be? It should contain the current date and time so as to be (hopefully!) unique.
	SYSTEMTIME SystemTime;
	GetSystemTime(&SystemTime);
	sprintf(CurrentBufferPointer,
		"86box-%d%02d%02d-%02d-%02d-%02d-%03d.dmp",
		SystemTime.wYear,
		SystemTime.wMonth,
		SystemTime.wDay,
		SystemTime.wHour,
		SystemTime.wMinute,
		SystemTime.wSecond,
		SystemTime.wMilliseconds);
	
	DWORD Error;
	
	// Now the filename is in the buffer, the file can be created.
	HANDLE hDumpFile = CreateFile(
		ExceptionHandlerBuffer, // The filename of the file to open.
		GENERIC_WRITE, // The permissions to request.
		0, // Make sure other processes can't touch the crash dump at all while it's open.
		NULL, // Leave the security descriptor undefined, it doesn't matter.
		OPEN_ALWAYS, // Opens the file if it exists, creates a new file if it doesn't.
		FILE_ATTRIBUTE_NORMAL, // File attributes / etc don't matter.
		NULL); // A template file is not being used.
	
	// Check to make sure the file was actually created.
	if (hDumpFile == INVALID_HANDLE_VALUE) {
		// CreateFile() failed, so just do nothing more.
		return EXCEPTION_CONTINUE_SEARCH;
	}
	
	// Now the file is open, let's write the data we were passed out in a human-readable format.
	
	// Let's get the name of the module where the exception occured.
	HMODULE hMods[1024];
	MODULEINFO modInfo;
	HMODULE ipModule = 0;
	DWORD cbNeeded;
	// Try to get a list of all loaded modules.
	if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded)) {
		// The list was obtained, walk through each of the modules.
		for (DWORD i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
			// For each module, get the module information (base address, size, entry point)
			GetModuleInformation(GetCurrentProcess(), hMods[i], &modInfo, sizeof(MODULEINFO));
			// If the exception address is located in the range of where this module is loaded...
			if (
				(ExceptionInfo->ExceptionRecord->ExceptionAddress >= modInfo.lpBaseOfDll) &&
				(ExceptionInfo->ExceptionRecord->ExceptionAddress < (modInfo.lpBaseOfDll + modInfo.SizeOfImage))
			) {
				// ...this is the module we're looking for!
				ipModule = hMods[i];
				break;
			}
		}
	}
	
	// Start to put the crash-dump string into the buffer.
	
	sprintf(ExceptionHandlerBuffer,
		"86Box version %s crashed on %d-%02d-%02d %02d:%02d:%02d.%03d\r\n\r\n"
		""
		"Exception details:\r\n"
		"Exception NTSTATUS code: 0x%08x\r\n"
		"Occured at address: 0x%p",
		emulator_version, SystemTime.wYear, SystemTime.wMonth, SystemTime.wDay, SystemTime.wHour, SystemTime.wMinute, SystemTime.wSecond, SystemTime.wMilliseconds,
		
		ExceptionInfo->ExceptionRecord->ExceptionCode,
		ExceptionInfo->ExceptionRecord->ExceptionAddress);
		
	
	// If we found the module that the exception occured in, get the full path to the module the exception occured at and include it.
	CurrentBufferPointer = &ExceptionHandlerBuffer[strlen(ExceptionHandlerBuffer)];
	if (ipModule != 0) {
		sprintf(CurrentBufferPointer," [");
		GetModuleFileName(ipModule,&CurrentBufferPointer[2],ExceptionHandlerBufferSize - strlen(ExceptionHandlerBuffer));
		if (GetLastError() == ERROR_SUCCESS) {
			CurrentBufferPointer = &ExceptionHandlerBuffer[strlen(ExceptionHandlerBuffer)];
			sprintf(CurrentBufferPointer,"]");
			CurrentBufferPointer += 1;
		}
	}
	
	// Continue to create the crash-dump string.
	sprintf(CurrentBufferPointer,
		"\r\n"
		"Number of parameters: %d\r\n"
		"Exception parameters: ",
		ExceptionInfo->ExceptionRecord->NumberParameters);
	
	// Add the exception parameters to the crash-dump string.
	for (int i = 0; i < ExceptionInfo->ExceptionRecord->NumberParameters; i++) {
		CurrentBufferPointer = &ExceptionHandlerBuffer[strlen(ExceptionHandlerBuffer)];
		sprintf(CurrentBufferPointer,"0x%p ",ExceptionInfo->ExceptionRecord->ExceptionInformation[i]);
	}
	
	CurrentBufferPointer = &ExceptionHandlerBuffer[strlen(ExceptionHandlerBuffer) - 1];
	
	PCONTEXT Registers = ExceptionInfo->ContextRecord;
	
	#if defined(__i386__) && !defined(__x86_64)
	// This binary is being compiled for x86, include a register dump.
	sprintf(CurrentBufferPointer,
		"\r\n"
		"Register dump:\r\n"
		"eax=0x%08x ebx=0x%08x ecx=0x%08x edx=0x%08x ebp=0x%08x esp=0x%08x esi=0x%08x edi=0x%08x eip=0x%08x\r\n"
		"\r\n",
		Registers->Eax, Registers->Ebx, Registers->Ecx, Registers->Edx, Registers->Ebp, Registers->Esp, Registers->Esi, Registers->Edi, Registers->Eip);
	#else
	// Register dump is supported by no other architectures right now. MinGW headers seem to lack the x64 CONTEXT structure definition.
	sprintf(CurrentBufferPointer,"\r\n");
	#endif
	
	// The crash-dump string has been created, write it to disk.
	WriteFile(hDumpFile,
		ExceptionHandlerBuffer,
		strlen(ExceptionHandlerBuffer),
		NULL,
		NULL);
	
	// Finally, close the file.
	CloseHandle(hDumpFile);
	
	// And return, therefore causing the crash, but only after the crash dump has been created.
	
	return EXCEPTION_CONTINUE_SEARCH;
}

void InitCrashDump() {
	// An exception handler should not allocate memory, so allocate 10kb for it to use if it gets called, an amount which should be more than enough.
	ExceptionHandlerBuffer = malloc(ExceptionHandlerBufferSize);
	// Register the exception handler. Zero first argument means this exception handler gets called last, therefore, crash dump is only made, when a crash is going to happen.
	hExceptionHandler = AddVectoredExceptionHandler(0,MakeCrashDump);
}