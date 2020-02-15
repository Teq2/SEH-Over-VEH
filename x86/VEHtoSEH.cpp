/*
* Implementation of Structured Exceptions Handling inside a Vectored Exception Handler
* For x86-platforms
* Usage: 
* 1. Include this file
* 2. Call `EnableSEHoverVEH()` (once)
*
* Code fully based on reversed kernel and opened WRK sources.
* Teq 01.2015-02.2020
*/

#include <windows.h>
#include <WinNT.h>
#include <excpt.h>

// from \crt\src\eh\i386\trnsctrl.cpp
#define EXCEPTION_UNWINDING 0x2         /* Unwind is in progress */
#define EXCEPTION_EXIT_UNWIND 0x4       /* Exit unwind is in progress */
#define EXCEPTION_STACK_INVALID 0x8     /* Stack out of limits or unaligned */
#define EXCEPTION_NESTED_CALL 0x10      /* Nested exception handler call */
#define EXCEPTION_TARGET_UNWIND 0x20    /* Target unwind in progress */
#define EXCEPTION_COLLIDED_UNWIND 0x40  /* Collided exception handler call */

extern "C" NTSYSAPI VOID NTAPI RtlRaiseException(PEXCEPTION_RECORD ExceptionRecord);
extern "C" NTSYSAPI NTSTATUS NTAPI NtContinue(IN PCONTEXT ThreadContext, IN BOOLEAN RaiseAlert );
extern "C" NTSYSAPI NTSTATUS NTAPI NtRaiseException(IN PEXCEPTION_RECORD ExceptionRecord, IN PCONTEXT ThreadContext, IN BOOLEAN HandleException);

typedef struct EXCEPTION_REGISTRATION
{
	EXCEPTION_REGISTRATION* nextframe;
	PEXCEPTION_ROUTINE handler;
} *PEXCEPTION_REGISTRATION;

__declspec(naked) EXCEPTION_REGISTRATION* GetRegistrationHead()
{
	__asm mov eax, dword ptr fs:[0]
	__asm retn
}

EXCEPTION_DISPOSITION NestedExceptionHandler(EXCEPTION_RECORD *ExceptionRecord, PLONG pEstablisherFrame, CONTEXT *ContextRecord, PLONG pDispatcherContext)
{
	if (ExceptionRecord->ExceptionFlags& (EXCEPTION_UNWINDING|EXCEPTION_EXIT_UNWIND))
		return ExceptionContinueSearch;
	else 
	{
		*pDispatcherContext = *(pEstablisherFrame+2); /* move previously saved EstablisherFrame from [pEstablisherFrame+8h] */
		return ExceptionNestedException;
	}
} 

EXCEPTION_DISPOSITION SafeExecuteHandler(EXCEPTION_RECORD *ExceptionRecord, PVOID EstablisherFrame, CONTEXT *ContextRecord, PVOID DispatcherContext, PEXCEPTION_ROUTINE pHandler)
{
	__asm  {
		/*
            An additional frame to catch nested exception, also for running __cdecl/__stdcall handlers correctly.

            If you use C++ exceptions - leave this as is, when C++ exception will be catched
            unwinding will be done in MSVCR._UnwindNestedFrames (check developer's commentaries
            in \crt\src\eh\i386\trnsctrl.cpp and view _JumpToContinuation)
		*/
		push	EstablisherFrame        /* save EstablisherFrame for nested exception */
		push	NestedExceptionHandler
		push	dword ptr fs:[0]
		mov		dword ptr fs:[0], esp
	}
	EXCEPTION_DISPOSITION Disposition = pHandler(ExceptionRecord, EstablisherFrame, ContextRecord, DispatcherContext);

	__asm {
		mov		esp, dword ptr fs:[0]
		pop		dword ptr fs:[0] 
        add     esp, 8                  /* restore stack to prevent esi/edi corruption */
	}
	return Disposition;
}

// VEH as a dispatcher of structured exceptions
_declspec(noreturn) VOID CALLBACK DispatchStructuredException(PEXCEPTION_POINTERS ExceptionInfo)
{
	PCONTEXT ctx = ExceptionInfo->ContextRecord;
	PEXCEPTION_RECORD ex = ExceptionInfo->ExceptionRecord;
	PEXCEPTION_REGISTRATION Registration = GetRegistrationHead();
	PEXCEPTION_REGISTRATION NestedRegistration = 0, DispatcherContext = 0; 

	while ((LONG)Registration != -1)    /* -1 means end of chain */
	{
		EXCEPTION_DISPOSITION  Disposition = SafeExecuteHandler(ex, Registration, ctx, &DispatcherContext, Registration->handler);

		if (NestedRegistration == Registration) 
		{
			ex->ExceptionFlags &= (~EXCEPTION_NESTED_CALL);
			NestedRegistration = 0;
		}

		switch (Disposition)
		{
			EXCEPTION_RECORD nextEx;

			case ExceptionContinueExecution:
				if (!(ex->ExceptionFlags&EXCEPTION_NONCONTINUABLE))
					NtContinue(ctx, 0);
				else
				{
					nextEx.ExceptionCode = EXCEPTION_NONCONTINUABLE_EXCEPTION;
					nextEx.ExceptionFlags = 1;
					nextEx.ExceptionRecord = ex;
					nextEx.ExceptionAddress = 0;
					nextEx.NumberParameters = 0;
					RtlRaiseException(&nextEx);
				}
				break;

			case ExceptionContinueSearch:
				if (ex->ExceptionFlags&EXCEPTION_STACK_INVALID)
					NtRaiseException(ex, ctx, false);
				break;

			case ExceptionNestedException:
                ex->ExceptionFlags |= EXCEPTION_NESTED_CALL;
                // renew context
                if (DispatcherContext > NestedRegistration)
                    NestedRegistration = DispatcherContext;
				break;

			default:
                nextEx.ExceptionRecord = ex;
                nextEx.ExceptionCode = STATUS_INVALID_DISPOSITION;
                nextEx.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
                nextEx.NumberParameters = 0;
                RtlRaiseException(&nextEx);
                break;
		}
		Registration = Registration->nextframe;
	}

	/* 
		dispatcher hasn't found appropriate hander for exception in SEH chain, 
		if this handler was first in the VEH-chain - there are could be other vectored handlers, 
		those handlers needs to take a chance to handle this exception (return EXCEPTION_CONTINUE_SEARCH;).
		if this handler is first - just call: NtRaiseException(ex, ctx, false);
	*/
	NtRaiseException(ex, ctx, false);
}

void EnableSEHoverVEH()
{
	AddVectoredExceptionHandler(0, (PVECTORED_EXCEPTION_HANDLER) &DispatchStructuredException);
}