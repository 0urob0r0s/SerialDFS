/* Minimal SS switch test */
#include <i86.h>

int main(void) {
  unsigned short seg = 0;
  /* Print 'A' before test */
  _asm { mov ah, 2; mov dl, 41h; int 21h }
  /* Get current DS into seg */
  _asm { push ds; pop seg }
  /* Push DS twice, pop DS, pop SS — minimal SS switch */
  _asm {
    push ds
    pop ds    /* DS = DS (no-op) */
    push ds
    pop ss    /* SS = DS */
    nop
  }
  /* Print 'B' after test */
  _asm { mov ah, 2; mov dl, 42h; int 21h }
  return 0;
}
