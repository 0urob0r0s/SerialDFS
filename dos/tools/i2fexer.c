/*
 * i2fexer.c — INT 2Fh AH=11h direct exerciser for SerialDFS TSR testing.
 *
 * Why this exists: DOSBox-X bypasses INT 2Fh AH=11h for normal file ops
 * (it dispatches to its internal Drives[] table directly).  This program
 * sets up the SDA fields the way DOS would, then invokes INT 2Fh AH=11h
 * directly so the SerialDFS TSR's redirector path is actually exercised
 * and forwards to the daemon.
 *
 * Subcommand:
 *   i2fexer GETATTR <drive>:\<path>   -- e.g. "i2fexer GETATTR X:\README.TXT"
 *
 * Output: prints AX returned, CF, and reply notes.  Exits 0 on success.
 */
#include <stdio.h>
#include <string.h>

/* Minimal SDA layout — only fields we need (DOS 4+/MSDOS 5+ offsets). */
struct sda_min {
  unsigned char  f0[0x9E];
  unsigned char  fn1[128];     /* 0x9E */
};

static struct sda_min far *get_sda(void) {
  unsigned short rds = 0, rsi = 0;
  _asm {
    mov ax, 5D06h
    push ds
    push si
    int 21h
    mov bx, ds
    mov cx, si
    pop si
    pop ds
    mov rds, bx
    mov rsi, cx
  }
  return (struct sda_min far *)(((unsigned long)rds << 16) | rsi);
}

static void copy_path_to_sda(struct sda_min far *sda, const char *path) {
  int i;
  for (i = 0; path[i] != 0 && i < 127; i++) sda->fn1[i] = (unsigned char)path[i];
  sda->fn1[i] = 0;
}

static int do_getattr(const char *path) {
  struct sda_min far *sda = get_sda();
  unsigned short ax_out, flags_out;
  unsigned int es_out, di_out;

  if (sda == NULL) {
    printf("ERROR: getsda() returned NULL — INT 21h AH=5D06h failed.\n");
    return 1;
  }
  copy_path_to_sda(sda, path);

  /* Read back to confirm */
  printf("SDA fn1 set to: ");
  {
    int i;
    for (i = 0; sda->fn1[i] && i < 80; i++) putchar(sda->fn1[i]);
    putchar('\n');
  }

  /* Invoke INT 2Fh AH=11h AL=0Fh (GETATTR).  ES:DI = CDS for drive (DOS
   * convention).  We pass ES=DS and DI=0 — the TSR uses fn1[0] for the
   * drive number, not ES:DI for GETATTR. */
  _asm {
    mov ax, 110Fh
    xor di, di
    push es
    push ds
    pop  es                  /* es=ds (placeholder) */
    int 2Fh
    pushf
    mov ax_out, ax
    mov es_out, es
    mov di_out, di
    pop flags_out
    pop es
  }

  printf("INT 2Fh AX=110Fh -> AX=%04X CF=%d ES:DI=%04X:%04X\n",
         (unsigned)ax_out, (int)(flags_out & 1),
         (unsigned)es_out, (unsigned)di_out);

  /* AX==0x110F means our handler chained without modifying AX (TSR not
   * intercepting), or the call wasn't recognized.  AX==0 (CF clear) means
   * success.  Anything else = DOS error code. */
  if (ax_out == 0x110F) {
    printf("RESULT: TSR did not handle the call (AX unchanged).\n");
    return 2;
  }
  if (flags_out & 1) {
    printf("RESULT: GETATTR failed with DOS error %u.\n", (unsigned)ax_out);
    return 3;
  }
  printf("RESULT: GETATTR ok.\n");
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    printf("Usage: i2fexer GETATTR <drive>:\\<path>\n");
    return 1;
  }
  if (strcmp(argv[1], "GETATTR") == 0) {
    return do_getattr(argv[2]);
  }
  printf("Unknown subcommand: %s\n", argv[1]);
  return 1;
}
