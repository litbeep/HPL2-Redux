// GPLv3 or later.
#include "CrashLog.h"

#ifdef _WIN32
#include "system/LowLevelSystem.h"
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

namespace {
	LONG WINAPI OnCrash(EXCEPTION_POINTERS* apInfo)
	{
		const HANDLE process = GetCurrentProcess(), thread = GetCurrentThread();
		hpl::Error("Penumbra crashed: exception 0x%08lx at %p\n",
			apInfo->ExceptionRecord->ExceptionCode, apInfo->ExceptionRecord->ExceptionAddress);
		SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
		if(!SymInitialize(process, nullptr, TRUE)) return EXCEPTION_CONTINUE_SEARCH;

		CONTEXT context = *apInfo->ContextRecord;
		STACKFRAME64 frame = {};
		frame.AddrPC.Offset = context.Rip;    frame.AddrPC.Mode = AddrModeFlat;
		frame.AddrFrame.Offset = context.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
		frame.AddrStack.Offset = context.Rsp; frame.AddrStack.Mode = AddrModeFlat;

		// Collect every return address first (static: there may be no stack to
		// spare). A stack overflow repeats the same frames thousands of times,
		// so log the innermost frames, which show the loop, and the outermost,
		// which show how it was entered.
		static DWORD64 s_vAddresses[200000];
		int lCount = 0;
		while(lCount < 200000 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context,
			nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
			s_vAddresses[lCount++] = frame.AddrPC.Offset;

		alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 256];
		auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
		const int lInner = 16, lOuter = 32;
		for(int i = 0; i < lCount; ++i)
		{
			if(i == lInner && lCount > lInner + lOuter)
			{
				hpl::Error("  ... %d frames ...\n", lCount - lInner - lOuter);
				i = lCount - lOuter;
			}
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = 255;
			DWORD64 displacement = 0;
			const char* name = SymFromAddr(process, s_vAddresses[i], &displacement, symbol) ? symbol->Name : "?";
			IMAGEHLP_LINE64 line = {};
			line.SizeOfStruct = sizeof(line);
			DWORD lineDisplacement = 0;
			if(SymGetLineFromAddr64(process, s_vAddresses[i], &lineDisplacement, &line))
				hpl::Error("  #%d %s (%s:%lu)\n", i, name, line.FileName, line.LineNumber);
			else
				hpl::Error("  #%d %s (%p)\n", i, name, reinterpret_cast<void*>(s_vAddresses[i]));
		}
		SymCleanup(process);
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

void penumbra::InstallCrashLog()
{
	// Keep enough stack to run the handler after a stack overflow.
	ULONG guarantee = 128 * 1024;
	SetThreadStackGuarantee(&guarantee);
	SetUnhandledExceptionFilter(OnCrash);
}
#else
void penumbra::InstallCrashLog() {}
#endif
