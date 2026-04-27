/* test: directly invoke INT 2Fh AH=0x11 AL=0x1B and report */
#include <stdio.h>
int main(void) {
  unsigned short ax_in = 0x111B;
  unsigned short ax_out;
  _asm {
    push ds
    push es
    mov  ax, ax_in
    mov  bx, 0
    mov  cx, 0
    mov  dx, 0
    int  2Fh
    mov  ax_out, ax
    pop  es
    pop  ds
  }
  printf("INT 2Fh AX=111Bh -> AX=%04X\n", (unsigned)ax_out);
  return 0;
}
