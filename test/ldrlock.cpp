//
// Kernel driver for Windows NT
//
// (C) Great, 2006-2008
//

extern "C" {
#include <ntifs.h>
#include "critsect.h"

PVOID MyGetProcAddress (PVOID ImageBase, PCHAR RoutineName);
}

#define NTDLL_BASE	((PVOID)0x7c900000)

PRTL_CRITICAL_SECTION LdrpLoaderLock = NULL;

void FindLoaderLock ()
{
	PVOID RtlpWaitForCriticalSection = NULL;

	RtlpWaitForCriticalSection = MyGetProcAddress (NTDLL_BASE, "RtlpWaitForCriticalSection");
	KdPrint(("Got RtlpWaitForCriticalSection = %X\n", RtlpWaitForCriticalSection));

	if (!RtlpWaitForCriticalSection)
		return;

	for (UCHAR* p = (UCHAR*)RtlpWaitForCriticalSection; p < (UCHAR*)RtlpWaitForCriticalSection + PAGE_SIZE; p ++)
	{
		if (*(USHORT*)p == 0xFE81) // CMP ESI, offset XXXXXXXX
		{
			p += 2;
			LdrpLoaderLock = *(PRTL_CRITICAL_SECTION*)p;
			break;
		}
	}

	KdPrint(("Got LdrpLoaderLock = %p\n", LdrpLoaderLock));

	if(!LdrpLoaderLock)
		return;

	KdPrint(("Section->LockCount = %d\n", LdrpLoaderLock->LockCount));
	KdPrint(("Section->LockSemaphore = %p\n", LdrpLoaderLock->LockSemaphore));
}

void TestLoaderLock()
{
	FindLoaderLock();
	if (!LdrpLoaderLock)
	{
		KdPrint(("Could not find loader lock\n"));
		return;
	}

	EnterCriticalSection (LdrpLoaderLock);

	KdPrint(("\n!!!!!!ENTERED CRITICAL SECTION!!!!!!!\n\n"));
	//wait  30 secs
	LARGE_INTEGER Timeout = { -10000 * 1000, -1 }; 

	for (ULONG i=0; i<30; i++)
	{
		KeDelayExecutionThread (KernelMode, FALSE, &Timeout);
		KdPrint(("%d seconds remaining...\r", 29-i));
	}

	KdPrint(("Unlocking\n"));

	LeaveCriticalSection (LdrpLoaderLock);
}
 
// Driver entry point
NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath)
{
	KdPrint(("[~] DriverEntry()\n"));

	PEPROCESS Process, System = Process = PsGetCurrentProcess();

	do
	{
		if(!_stricmp((char*)Process->ImageFileName, "explorer.exe"))
		{
			KAPC_STATE State;

			KeStackAttachProcess (&Process->Pcb, &State);

			TestLoaderLock();

			KeUnstackDetachProcess (&State);

			break;
		}

		Process = CONTAINING_RECORD (Process->ActiveProcessLinks.Flink, EPROCESS, ActiveProcessLinks);
	}
	while (TRUE);

	KdPrint(("[+] Driver initialization successful\n"));
	return STATUS_UNSUCCESSFUL;
}
