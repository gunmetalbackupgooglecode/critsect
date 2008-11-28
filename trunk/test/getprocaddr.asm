format MS COFF

include 'win32ax.inc'

public MyGetProcAddress as '_MyGetProcAddress@8'

proc MyGetProcAddress, ImageBase, RoutineName
     push ebx
     mov eax, [RoutineName]
     mov ebx, [ImageBase]
     call _MyGetProcAddress
     pop ebx
     ret
endp

;
; MyGetProcAddress routine
; Uses: EBP,ECX
;
;  EAX = routine name
;  EBX = image base
;  ECX = routine name length
;
; Return value:
;  EAX = proc address
;
_MyGetProcAddress:
    push esi
    push edi
    push ebp

    call _mystrlen
    push ecx

    push eax
    push ebx
    mov ebp, esp

    ; Get export descriptor

    ; Get IMAGE_OPTIONAL_HEADER
    add ebx, [ebx+0x3c]     ; ebx = image_nt_headers
    add ebx, 4 + 0x14  ; skip PE00 (4 bytes), IMAGE_FILE_HEADER (0x14 bytes), ebx = image_optional_header
    ; Get directory IMAGE_DIRECTORY_ENTRY_EXPORT v.a.
IMAGE_DIRECTORY_ENTRY_EXPORT = 0
    mov edx, [ebp] ; image base
    add edx, [ebx + 0x60 + IMAGE_DIRECTORY_ENTRY_EXPORT * 0x08] ; skip 0x60 (DataDirectory offset) and get export directory VA
    ; edx points to export directory. Save addresses of functions and names

    mov eax, [ebp]
    add eax, [edx+0x24]
    push eax ; AddressOfNameOrdinals

    mov eax, [ebp] ; image base
    add eax, [edx+0x1c]
    push eax ; AddressOfFunctions

    mov ebx, [ebp] ; image base
    add ebx, [edx+0x20]
    push ebx ; AddressOfNames
    ; ebx,[esp] points to names; eax,[esp+4] points to functions, [esp+8] points to name ordinals

    mov ecx, [edx+0x18] ; number of names
_scan:
       push ecx
       dec ecx

       mov esi, [ebp]
       add esi, [ebx + ecx*4]  ; esi points to function name

       mov edi, [ebp+4] ; routine name
       mov ecx, [ebp+8] ; routine name length
       inc ecx
       repe cmpsb

       test ecx,ecx
       jz _found

       pop ecx
       loop _scan
    ; not found
    xor eax, eax
    jmp @quit

_found:
    pop ecx
    dec ecx
    mov eax, [esp+4]	     ; address of functions

    mov edx, [esp+8]  ; address of name ordinals
    movzx ecx, word [edx + ecx*2]  ; get name ordinal

    mov eax, [eax + ecx*4]
    add eax, [ebp]

@quit:
    mov esp, ebp
    pop ebx
    add esp, 4	 ; pop eax
    pop ecx

    pop ebp
    pop edi
    pop esi
    ret

;
; mystrlen routine
;
;  EAX = string
;
; Return value:
;  ECX = string length
;
_mystrlen:
    push edi
    push eax

    mov edi, eax
    xor eax, eax
    mov ecx, -1
    repne scasb

    sub edi, [esp]
    mov ecx, edi
    dec ecx

    pop eax
    pop edi
    ret

