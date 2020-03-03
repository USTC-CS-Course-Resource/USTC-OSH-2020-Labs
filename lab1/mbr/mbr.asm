[BITS 16]                                   ; 16 bits program
[ORG 0x7C00]                                ; starts from 0x7c00, where MBR lies in memory

main:
    mov ah, 0x0F
    int 0x10
    mov ah, 0x00
    int 0x10                                ; clear the screen
    sti
    .print_loop:                            ; the loop for print string
        mov ah, [0x046c]
        and ecx, 0x0000
        .count:                             ; count to 18, which is about 1 second
            mov al, ah
            .compare:                       ; compare the al and [0x046c]
                mov ah, [0x046c]
                cmp al, ah
                je .compare
            add ecx, 1
            cmp ecx, 18
            jl .count

        mov si, OSH                         ; si points to string OSH
        .print_next_char:                   ; print the string
            lodsb                           ; load char to al
            cmp al, 0                       ; is it the end of the string?
            je .print_loop                  ; if true, then halt the system
            mov ah, 0x0e                    ; if false, then set AH = 0x0e 
            int 0x10                        ; call BIOS interrupt procedure, print a char to screen
            jmp .print_next_char            ; loop over to print all chars


    .hlt:
        hlt

OSH db 'Hello, OSH 2020 Lab1!', 13, 10, 0   ; our string, null-terminated

TIMES 510 - ($ - $$) db 0                   ; the size of MBR is 512 bytes, fill remaining bytes to 0
DW 0xAA55                                   ; magic number, mark it as a valid bootloader to BIOS
