/* diaglol.c - Dumps List of Lists fields for LASTDRIVE/CDS diagnosis */
#include <stdio.h>

void main(void) {
    static unsigned short lol_seg, lol_off;
    static unsigned char  lastdrv;
    static unsigned short cds_seg, cds_off;
    static unsigned short flags0;

    _asm {
        push si
        push es
        mov  ah, 52h
        int  21h
        mov  lol_off, bx
        mov  lol_seg, es
        mov  si, 21h
        mov  al, byte ptr es:[bx+si]
        mov  lastdrv, al
        mov  si, 16h
        les  si, es:[bx+si]
        mov  cds_off, si
        mov  cds_seg, es
        mov  si, 0
        mov  ax, word ptr es:[si]
        mov  flags0, ax
        pop  es
        pop  si
    }

    printf("LoL  = %04X:%04X\n", lol_seg, lol_off);
    printf("lastdrv byte (LoL+21h) = 0x%02X ('%c')\n",
           (unsigned)lastdrv,
           (lastdrv >= 0 && lastdrv <= 25) ? ('A' + lastdrv) : '?');
    printf("CDS  = %04X:%04X\n", cds_seg, cds_off);
    if (cds_seg == 0xFFFF && cds_off == 0xFFFF)
        printf("CDS pointer is FFFF:FFFF (invalid!)\n");
    else
        printf("CDS[0].flags = 0x%04X\n", flags0);
}
