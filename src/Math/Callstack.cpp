#include "Callstack.h"
#include <string>

#if defined(WIN32) && defined(_MSC_VER) && !defined(WIN8RT)

#include <Windows.h>
#include <DbgHelp.h>

#pragma comment(lib, "dbghelp.lib")

static std::string GetSymbolName(void *address, HANDLE hProcess)
{
	// http://msdn.microsoft.com/en-us/library/ms680578(v=vs.85).aspx
	DWORD64 dwDisplacement = 0;
	DWORD64 dwAddress = (DWORD64)address;

	static char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
	PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;

	pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	pSymbol->MaxNameLen = MAX_SYM_NAME;

	if (SymFromAddr(hProcess, dwAddress, &dwDisplacement, pSymbol))
	{
		// SymFromAddr returned success
		IMAGEHLP_LINE64 line = {};
		line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
		if (SymGetLineFromAddr64(hProcess, dwAddress, (PDWORD)&dwDisplacement, &line))
		{
			// SymGetLineFromAddr64 returned success
			char str[128];
			sprintf(str, ":%d: ", line.LineNumber);
			return std::string(line.FileName) + str + pSymbol->Name;
		}
		else
		{
			// SymGetLineFromAddr64 failed
//			DWORD error = GetLastError();
//			printf("SymGetLineFromAddr64 returned error : %d\n", error);
			return pSymbol->Name;
		}
	}
	else
	{
		// SymFromAddr failed
//		DWORD error = GetLastError();
//		printf("SymFromAddr returned error : %d\n", error);
		return std::string();
	}
}

#ifdef _M_IX86
#pragma warning(push)
#pragma warning(disable : 4740) // warning C4740: flow in or out of inline asm code suppresses global optimization
#endif

std::string NOINLINE GetCallstack(const char *indent, const char *ignoreFilter)
{
	static bool symInitialized = false;

	HANDLE currentProcess = GetCurrentProcess();
	HANDLE currentThread = GetCurrentThread();

	if (!symInitialized)
	{
		SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
		SymInitialize(currentProcess, NULL, TRUE);
		symInitialized = true;
	}

	CONTEXT context = {};
	STACKFRAME64 stack = {};

#ifdef _M_IX86
	context.ContextFlags = CONTEXT_CONTROL;
	_asm {
		call x
		x: pop eax
		mov context.Eip, eax
		mov context.Ebp, ebp
		mov context.Esp, esp
	}
#else
	RtlCaptureContext(&context);
#endif

	stack.AddrPC.Mode         = AddrModeFlat;
	stack.AddrStack.Mode      = AddrModeFlat;
	stack.AddrFrame.Mode      = AddrModeFlat;

#ifdef _M_X64
	// http://msdn.microsoft.com/en-us/library/windows/desktop/ms680646(v=vs.85).aspx
	stack.AddrPC.Offset       = context.Rip;
	stack.AddrStack.Offset    = context.Rsp;
	stack.AddrFrame.Offset    = context.Rbp;
	const DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
	const PVOID contextRecord = &context;
#else
	stack.AddrPC.Offset       = context.Eip;
	stack.AddrStack.Offset    = context.Esp;
	stack.AddrFrame.Offset    = context.Ebp;
	const DWORD machineType = IMAGE_FILE_MACHINE_I386;
	const PVOID contextRecord = NULL;
#endif

	std::string callstack;
	for(int i = 0; i < 128; ++i)
	{
		BOOL result = StackWalk64(machineType, currentProcess, currentThread, &stack, contextRecord, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL);
		if (!result)
			break;

		std::string symbolName = GetSymbolName((void*)stack.AddrPC.Offset, currentProcess);
		if (symbolName.find(" GetCallstack") == symbolName.length() - strlen(" GetCallstack"))
			continue;
		if (!ignoreFilter || symbolName.find(ignoreFilter) == symbolName.npos)
		{
			if (!symbolName.empty())
			{
				callstack += indent;
				callstack += symbolName;
				callstack += '\n';
			}
			ignoreFilter = 0;
		}
		if (symbolName.find(" main") == symbolName.length() - strlen(" main"))
			break;
		if (stack.AddrReturn.Offset == 0)
			break;
	}
	return callstack;
}

#ifdef _M_IX86
#pragma warning(pop)
#endif

#elif defined(__APPLE__) || defined(LINUX)

#include <execinfo.h>

std::string NOINLINE GetCallstack(const char *indent, const char *ignoreFilter)
{
	const int N = 128;
	void *callstack[N];
	int n = backtrace(callstack, N);
	char **strs = backtrace_symbols(callstack, n);
	std::string stack;
	for(int i = 0; i < n; ++i)
	{
		stack += indent;
		stack += strs[i];
		stack += '\n';
	}
	return stack;
}

#else

std::string GetCallstack(const char *indent, const char *ignoreFilter)
{
	// Not implemented on this platform.
	return std::string();
}

#endif
