#include <windows.h>
#include "VEHtoSEH.h"

EXCEPTION_DISPOSITION test_handler(EXCEPTION_RECORD *ExceptionRecord, PLONG pEstablisherFrame, CONTEXT *ContextRecord, PLONG pDispatcherContext)
{
	ContextRecord->Eip += 6;
	return ExceptionContinueExecution;
}

void test()
{
	__asm
	{
		push	test_handler
		push	dword ptr fs:[0]
		mov		dword ptr fs:[0], esp
	}

	int *i  =0;
	*i = 0;
	MessageBox(0,"Test",0,0);

	__asm
	{
		mov		esp, dword ptr fs:[0]
		pop		dword ptr fs:[0]  
		add		esp, 4
	}
}

int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	EnableSEHoverVEH();
	test();
	return 0;
}