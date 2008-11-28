extern "C" 
{
#include <ntifs.h>
#include "critsect.h"
}

typedef struct RTL_CRITICAL_SECTION_DEBUG
{
	USHORT Type;
	USHORT CreatorBackTraceIndex;
	RTL_CRITICAL_SECTION *CriticalSection;
	LIST_ENTRY ProcessLocksList;
	ULONG EntryCount;
	ULONG ContentionCount;
	ULONG Spare[2];
} *PRTL_CRITICAL_SECTION_DEBUG;

VOID _CreateCriticalSectionSemaphore(PRTL_CRITICAL_SECTION Section)
{
	NTSTATUS st;
    LARGE_INTEGER tmo = {(ULONG)   -100000, -1};         // Start at 1/100th of a second
    LONGLONG max_tmo  = (LONGLONG) -10000000 * 60 * 3; // Wait for a max of 3 minutes

    //
    // Loop trying to create the event on failure.
    //

	while (TRUE)
	{
		st = ZwCreateEvent (
			&Section->LockSemaphore, 
			EVENT_ALL_ACCESS,
			NULL,
			SynchronizationEvent,
			FALSE
			);

		if (NT_SUCCESS(st))
			break;
		else
		{
			if ( tmo.QuadPart < max_tmo ) 
			{
				KdPrint(( "NTRTL: Warning. Unable to allocate lock semaphore for Cs %p. Status %x raising\n", Section,st ));
				InterlockedDecrement(&Section->LockCount);
				ExRaiseStatus(st);
			}

			KdPrint(( "NTRTL: Warning. Unable to allocate lock semaphore for Cs %p. Status %x retrying\n", Section,st ));

			st = KeDelayExecutionThread (KernelMode, FALSE, &tmo);
			if ( !NT_SUCCESS(st) ) 
			{
				InterlockedDecrement(&Section->LockCount);
				ExRaiseStatus(st);
			}

			tmo.QuadPart *= 2;
		}
	}
}

VOID _CheckDeferedCriticalSection (PRTL_CRITICAL_SECTION Section);

BOOLEAN _CritSectInitialized = FALSE;
RTL_CRITICAL_SECTION CriticalSectionLock;
RTL_CRITICAL_SECTION DeferedCriticalSection;
LIST_ENTRY CriticalSectionList;


BOOLEAN TryEnterCriticalSection (PRTL_CRITICAL_SECTION Section)
{
	HANDLE CurrentThread = PsGetCurrentThreadId();

	if (InterlockedCompareExchange (&Section->LockCount,
									0,
									-1) == -1)
	{
		Section->OwningThread = CurrentThread;
		Section->RecursionCount = 1;
		return TRUE;
	}

	if (Section->OwningThread == CurrentThread)
	{
		InterlockedIncrement (&Section->LockCount);
		Section->RecursionCount ++;
		return TRUE;
	}

	return FALSE;
}

VOID _WaitForCriticalSection (PRTL_CRITICAL_SECTION Section)
{
	NTSTATUS st;
	LARGE_INTEGER Timeout = {-10000 * 10000, -1}; // 10 seconds
	ULONG TimeoutCount = 0;	

	if (!Section->LockSemaphore)
	{
		//
		// Section is deffered. Cannot wait for such section (semaphore should be created)
		//

		ASSERT (FALSE);
		_CheckDeferedCriticalSection (Section);
	}

	Section->DebugInfo->EntryCount ++;
	while (TRUE)
	{
		Section->DebugInfo->ContentionCount ++;

		KdPrint(("NTRTL: Waiting for CritSect: %p  owned by ThreadId: %X  Count: %u  Level: %u\n",
			Section,
			Section->OwningThread,
			Section->LockCount,
			Section->RecursionCount
			));

		st = ZwWaitForSingleObject (Section->LockSemaphore, FALSE, NULL);
		if (st == STATUS_TIMEOUT)
		{
			KdPrint(("NTRTL: Enter Critical Section Timeout (10 seconds) %d\n",
				TimeoutCount
				));

			KdPrint(("NTRTL: Pid.Tid %x.%x, owner tid %x Critical Section %p - ContentionCount == %lu\n",
				PsGetCurrentProcessId(),
				PsGetCurrentThreadId(),
				Section->OwningThread,
				Section,
				Section->DebugInfo->ContentionCount
				));
			TimeoutCount ++;
			if (TimeoutCount > 6)
			{
				EXCEPTION_RECORD Record = {0};
				Record.ExceptionAddress = (PVOID) ExRaiseException;
				Record.ExceptionCode = STATUS_POSSIBLE_DEADLOCK;
				Record.ExceptionInformation[0] = (ULONG_PTR) Section;
				Record.NumberParameters = 1;
				
				ExRaiseException (&Record);
			}
			KdPrint(("NTRTL: Re-Waiting\n"));
		}
		else
		{
			if (NT_SUCCESS(st))
			{
				ASSERT (Section->OwningThread == NULL);
				return;
			}
			else
			{
				ExRaiseStatus (st);
			}
		}
	}
}

VOID _UnwaitCriticalSection (PRTL_CRITICAL_SECTION Section)
{
	NTSTATUS st;

	KdPrint(("NTRTL: Releasing CritSect: %p  ThreadId: %X\n",
		Section, Section->OwningThread
		));

	if (!Section->LockSemaphore)
	{
		//
		// Section is deffered. Cannot unwait such section (semaphore should be created)
		//

		ASSERT (FALSE);
		_CheckDeferedCriticalSection (Section);
	}

	st = ZwSetEvent (Section->LockSemaphore, NULL);

	if (NT_SUCCESS(st))
		return;

	ExRaiseStatus (st);
}

NTSTATUS EnterCriticalSection (PRTL_CRITICAL_SECTION Section)
{
	HANDLE CurrentThread = PsGetCurrentThreadId();

	if (Section->SpinCount)
	{
		if (Section->OwningThread == CurrentThread)
		{
			// Section owned by current thread.
			// Increment lock count and recursion count
			InterlockedIncrement (&Section->LockCount);
			Section->RecursionCount ++;
			return STATUS_SUCCESS;
		}

		// Section owned by someone else. Wait

		while (TRUE)
		{
			if (InterlockedCompareExchange (&Section->LockCount,
											0,
											-1) == -1)
			{
				// Got lock.
				Section->OwningThread = CurrentThread;
				Section->RecursionCount = 1;
				return STATUS_SUCCESS;
			}

			//
			// The critical section is currently owned. Spin until it is either unowned
			// or the spin count has reached zero.
			//
			// If waiters are present, don't spin on the lock since we will never see it go free			if (Section->LockCount >= 1)
			//
			if (Section->LockCount >= 1)
				break; // Ent76

			ULONG SpinCount = Section->SpinCount;

			while (TRUE)
			{
				ZwYieldExecution (); // YIELD = REP: NOP
				if (Section->LockCount == -1)
					break;
				SpinCount --;
				if (!SpinCount)
					goto _END_WHILE;
			}
		}
	}

_END_WHILE:

	// Attempt to acquire critical section
	if (InterlockedIncrement (&Section->LockCount) == 0)
	{
		Section->OwningThread = CurrentThread;
		Section->RecursionCount = 1;
		return STATUS_SUCCESS;
	}

	// The critical section is already owned, but may be owned by the current thread.
	if (Section->OwningThread != CurrentThread)
	{
		_WaitForCriticalSection (Section);
		Section->OwningThread = CurrentThread;
		Section->RecursionCount = 1;
		return STATUS_SUCCESS;
	}

	Section->RecursionCount ++;
	return STATUS_SUCCESS;
}

NTSTATUS LeaveCriticalSection (PRTL_CRITICAL_SECTION Section)
{
	if (!(-- Section->RecursionCount))
	{
		Section->OwningThread = NULL;
		if (InterlockedDecrement (&Section->LockCount) >= 0)
			_UnwaitCriticalSection (Section);
	}
	else
	{
		InterlockedDecrement (&Section->LockCount);
	}
	return STATUS_SUCCESS;
}

VOID _CheckDeferedCriticalSection (PRTL_CRITICAL_SECTION Section)
{
	if (!_CritSectInitialized)
	{
		ASSERT (FALSE);
		return;
	}

	EnterCriticalSection (&DeferedCriticalSection);
	__try
	{
		if (!Section->LockSemaphore)
			_CreateCriticalSectionSemaphore (Section);
	}
	__finally
	{
		LeaveCriticalSection (&DeferedCriticalSection);
	}
}


NTSTATUS InitializeCriticalSection (PRTL_CRITICAL_SECTION Section, ULONG SpinCount)
{
	Section->LockCount = -1;
	Section->OwningThread = NULL;
	Section->RecursionCount = 0;
	Section->SpinCount = SpinCount;

	_CreateCriticalSectionSemaphore (Section);

	Section->DebugInfo = (PRTL_CRITICAL_SECTION_DEBUG) 
		ExAllocatePool (PagedPool, sizeof(RTL_CRITICAL_SECTION_DEBUG));

	if (Section->DebugInfo == NULL)
	{
		if (Section->LockSemaphore)
		{
			ZwClose (Section->LockSemaphore);
			Section->LockSemaphore = NULL;
		}
		return STATUS_NO_MEMORY;
	}

	Section->DebugInfo->Type = 0;
	Section->DebugInfo->ContentionCount = 0;
	Section->DebugInfo->EntryCount = 0;
	Section->DebugInfo->CriticalSection = Section;

	if ((Section != &CriticalSectionLock) &&
		(_CritSectInitialized != FALSE))
	{
		EnterCriticalSection (&CriticalSectionLock);
		InsertTailList (&CriticalSectionList, &Section->DebugInfo->ProcessLocksList);
		LeaveCriticalSection (&CriticalSectionLock);
	}
	else
	{
		InsertTailList (&CriticalSectionList, &Section->DebugInfo->ProcessLocksList);
	}

	return STATUS_SUCCESS;
}

VOID InitDeferedCriticalSection ()
{
	InitializeListHead (&CriticalSectionList);

	InitializeCriticalSection (&CriticalSectionLock, 1000);
	InitializeCriticalSection (&DeferedCriticalSection, 1000);

	_CreateCriticalSectionSemaphore (&DeferedCriticalSection);

	_CritSectInitialized = TRUE;
}

VOID DeleteCriticalSection (PRTL_CRITICAL_SECTION Section)
{
	NTSTATUS st;

	if (Section->LockSemaphore)
	{
		st = ZwClose (Section->LockSemaphore);
		if (!NT_SUCCESS(st))
			ExRaiseStatus (st);
	}

	EnterCriticalSection (&CriticalSectionLock);
	__try
	{
		RemoveEntryList (&Section->DebugInfo->ProcessLocksList);
	}
	__finally
	{
		LeaveCriticalSection (&CriticalSectionLock);
	}

	if (Section->DebugInfo)
		ExFreePool (Section->DebugInfo);

	memset (Section, 0, sizeof(RTL_CRITICAL_SECTION));
}
