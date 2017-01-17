/* Copyright holders: Riley
   see COPYING for more details
   
   win-crashdump.c : Windows exception handler to make a crash dump just before a crash happens.
*/
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "86box.h"
#include "win-crashdump.h"

PVOID hExceptionHandler;
char* ExceptionHandlerBuffer;


LONG CALLBACK MakeCrashDump(PEXCEPTION_POINTERS ExceptionInfo) {
	// Win32-specific functions will be used wherever possible, just in case the C stdlib-equivalents try to allocate memory.
	// (The Win32-specific functions are generally just wrappers over NT system calls anyway.)
	
	// So, the program is about to crash. Oh no what do?
	// Let's create a crash dump file as a debugging-aid.
	
	// First, what would a good filename be? It should contain the current date and time so as to be (hopefully!) unique.
	SYSTEMTIME SystemTime;
	GetSystemTime(&SystemTime);
	sprintf(ExceptionHandlerBuffer,
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
	
	sprintf(ExceptionHandlerBuffer,
		"86Box version %s crashed on %d-%02d-%02d %02d:%02d:%02d.%03d\r\n\r\n"
		""
		"Exception details:\r\n"
		"Exception NTSTATUS code: 0x%08x\r\n"
		"Occured at address: 0x%p\r\n"
		"Number of parameters: %d\r\n"
		"Exception parameters: ",
		emulator_version, SystemTime.wYear, SystemTime.wMonth, SystemTime.wDay, SystemTime.wHour, SystemTime.wMinute, SystemTime.wSecond, SystemTime.wMilliseconds,
		
		ExceptionInfo->ExceptionRecord->ExceptionCode,
		ExceptionInfo->ExceptionRecord->ExceptionAddress,
		ExceptionInfo->ExceptionRecord->NumberParameters);
	
	char* CurrentBufferPointer;
	
	for (int i = 0; i < ExceptionInfo->ExceptionRecord->NumberParameters; i++) {
		CurrentBufferPointer = &ExceptionHandlerBuffer[strlen(ExceptionHandlerBuffer)];
		sprintf(CurrentBufferPointer,"0x%p ",ExceptionInfo->ExceptionRecord->ExceptionInformation[i]);
	}
	
	CurrentBufferPointer = &ExceptionHandlerBuffer[strlen(ExceptionHandlerBuffer) - 1];
	
	PCONTEXT Registers = ExceptionInfo->ContextRecord;
	
	#if defined(__i386__) && !defined(__x86_64)
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
	ExceptionHandlerBuffer = malloc(10240);
	// Register the exception handler. Zero first argument means this exception handler gets called last, therefore, crash dump is only made, when a crash is going to happen.
	hExceptionHandler = AddVectoredExceptionHandler(0,MakeCrashDump);
}