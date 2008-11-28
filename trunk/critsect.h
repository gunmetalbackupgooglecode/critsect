#ifndef _CRITSECT_H_
#define _CRITSECT_H_

extern "C" NTSTATUS NTAPI ExRaiseException (PEXCEPTION_RECORD);

struct RTL_CRITICAL_SECTION_DEBUG;

typedef struct RTL_CRITICAL_SECTION
{
   RTL_CRITICAL_SECTION_DEBUG* DebugInfo;
   LONG LockCount;
   ULONG RecursionCount;
   HANDLE OwningThread;
   HANDLE LockSemaphore;
   ULONG SpinCount;
} *PRTL_CRITICAL_SECTION;


BOOLEAN TryEnterCriticalSection (PRTL_CRITICAL_SECTION Section);
NTSTATUS EnterCriticalSection (PRTL_CRITICAL_SECTION Section);
NTSTATUS LeaveCriticalSection (PRTL_CRITICAL_SECTION Section);
NTSTATUS InitializeCriticalSection (PRTL_CRITICAL_SECTION Section, ULONG SpinCount);
VOID InitDeferedCriticalSection ();
VOID DeleteCriticalSection (PRTL_CRITICAL_SECTION Section);

VOID _WaitForCriticalSection (PRTL_CRITICAL_SECTION Section);
VOID _UnwaitCriticalSection (PRTL_CRITICAL_SECTION Section);

#endif
