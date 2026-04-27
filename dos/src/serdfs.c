/*
 * serdfs/dos/src/serdfs.c
 * SerialDFS DOS TSR — INT 2Fh network redirector over RS-232.
 * Adapted from vendor/etherdfs-baseline/etherdfs/trunk/etherdfs.c
 * EtherDFS by Mateusz Viste, GPL-2.0.  See COPYING and NOTICES.
 *
 * REUSE: inthandler() skeleton, process2f() dispatcher shape, supportedfunctions[],
 *        getsda/getcds/updatetsrds/findfreemultiplex/allocseg/freeseg/parseargv shape,
 *        copybytes/mystrlen/len_if_no_wildcards helpers.
 * REPLACE: sendquery() -> serial_rpc(); bsdsum() -> CRC-16/CCITT-FALSE;
 *          Ethernet header -> SerialDFS 10-byte header; pktdrv_* removed;
 *          MAC/Ethernet references removed.
 * See docs/etherdfs-notes.md for the full REUSE/REPLACE/IGNORE table.
 *
 * Phase 8: read-only TSR (AL_OPEN, AL_READFIL, AL_FINDFIRST/NEXT, AL_GETATTR,
 *          AL_CHDIR, AL_DISKSPACE, AL_CLSFIL, AL_SKFMEND).
 *          Write arms (MKDIR/RMDIR/CREATE/WRITE/DELETE/RENAME/SETATTR) stub to
 *          FAILFLAG(5) (write-protected).
 */

#include <i86.h>      /* union INTPACK, MK_FP, FP_OFF */
#include <conio.h>    /* outp(), inp() — compiler intrinsics */
#include "chint.h"
#include "version.h"
#include "dosstruc.h"
#include "globals.h"

#ifndef NULL
#  define NULL ((void *)0)
#endif

#define DEBUGLEVEL 0

/* ── All resident code goes into the BEGTEXT segment ──────────────────────── */
#pragma code_seg("BEGTEXT", "CODE")

/* Inline the serial support so it lands in BEGTEXT (resident segment). */
#include "seruart.c"
#include "serframe.c"
#include "serrpc.c"

/* ── Helper macros / enums (from EtherDFS, adapted) ─────────────────────── */

#define DRIVETONUM(x) (((x)>='a'&&(x)<='z')?(x)-'a':(x)-'A')

#define SUCCESSFLAG  glob_intregs.w.ax = 0; glob_intregs.w.flags &= ~INTR_CF;
#define FAILFLAG(x)  glob_intregs.w.ax = (x); glob_intregs.w.flags |= INTR_CF;

/* SerialDFS multiplex signature (install-check return values) */
#define SDF_MUXBX   0x4453   /* 'SD' */
#define SDF_MUXCX   0x0001

/* INT 2Fh AL subfunction IDs (from EtherDFS, verbatim) */
enum AL_SUBFUNCTIONS {
  AL_INSTALLCHK = 0x00,
  AL_RMDIR      = 0x01,
  AL_MKDIR      = 0x03,
  AL_CHDIR      = 0x05,
  AL_CLSFIL     = 0x06,
  AL_CMMTFIL    = 0x07,
  AL_READFIL    = 0x08,
  AL_WRITEFIL   = 0x09,
  AL_LOCKFIL    = 0x0A,
  AL_UNLOCKFIL  = 0x0B,
  AL_DISKSPACE  = 0x0C,
  AL_SETATTR    = 0x0E,
  AL_GETATTR    = 0x0F,
  AL_RENAME     = 0x11,
  AL_DELETE     = 0x13,
  AL_OPEN       = 0x16,
  AL_CREATE     = 0x17,
  AL_FINDFIRST  = 0x1B,
  AL_FINDNEXT   = 0x1C,
  AL_SKFMEND    = 0x21,
  AL_UNKNOWN_2D = 0x2D,
  AL_SPOPNFIL   = 0x2E,
  AL_UNKNOWN    = 0xFF
};

/* Supported-function table — AL_UNKNOWN means "chain to previous handler". */
static unsigned char supportedfunctions[0x2Fu] = {
  AL_INSTALLCHK, /* 0x00 */
  AL_RMDIR,      /* 0x01 */
  AL_UNKNOWN,    /* 0x02 */
  AL_MKDIR,      /* 0x03 */
  AL_UNKNOWN,    /* 0x04 */
  AL_CHDIR,      /* 0x05 */
  AL_CLSFIL,     /* 0x06 */
  AL_CMMTFIL,    /* 0x07 */
  AL_READFIL,    /* 0x08 */
  AL_WRITEFIL,   /* 0x09 */
  AL_LOCKFIL,    /* 0x0A */
  AL_UNLOCKFIL,  /* 0x0B */
  AL_DISKSPACE,  /* 0x0C */
  AL_UNKNOWN,    /* 0x0D */
  AL_SETATTR,    /* 0x0E */
  AL_GETATTR,    /* 0x0F */
  AL_UNKNOWN,    /* 0x10 */
  AL_RENAME,     /* 0x11 */
  AL_UNKNOWN,    /* 0x12 */
  AL_DELETE,     /* 0x13 */
  AL_UNKNOWN,    /* 0x14 */
  AL_UNKNOWN,    /* 0x15 */
  AL_OPEN,       /* 0x16 */
  AL_CREATE,     /* 0x17 */
  AL_UNKNOWN,    /* 0x18 */
  AL_UNKNOWN,    /* 0x19 */
  AL_UNKNOWN,    /* 0x1A */
  AL_FINDFIRST,  /* 0x1B */
  AL_FINDNEXT,   /* 0x1C */
  AL_UNKNOWN,    /* 0x1D */
  AL_UNKNOWN,    /* 0x1E */
  AL_UNKNOWN,    /* 0x1F */
  AL_UNKNOWN,    /* 0x20 */
  AL_SKFMEND,    /* 0x21 */
  AL_UNKNOWN,    /* 0x22 */
  AL_UNKNOWN,    /* 0x23 */
  AL_UNKNOWN,    /* 0x24 */
  AL_UNKNOWN,    /* 0x25 */
  AL_UNKNOWN,    /* 0x26 */
  AL_UNKNOWN,    /* 0x27 */
  AL_UNKNOWN,    /* 0x28 */
  AL_UNKNOWN,    /* 0x29 */
  AL_UNKNOWN,    /* 0x2A */
  AL_UNKNOWN,    /* 0x2B */
  AL_UNKNOWN,    /* 0x2C */
  AL_UNKNOWN_2D, /* 0x2D */
  AL_SPOPNFIL    /* 0x2E */
};

/* ── Resident helpers ────────────────────────────────────────────────────── */

/* Byte-copy — same as EtherDFS copybytes. */
static void copybytes(void far *d, const void far *s, unsigned int l) {
  while (l != 0) {
    l--;
    *(unsigned char far *)d = *(unsigned char far *)s;
    d = (unsigned char far *)d + 1;
    s = (unsigned char far *)s + 1;
  }
}

/* String length (far pointer). */
static unsigned short mystrlen(const unsigned char far *s) {
  unsigned short r = 0;
  while (*s++) r++;
  return r;
}

/* Return string length if no wildcard chars; else -1. */
static int len_if_no_wildcards(const unsigned char far *s) {
  int r = 0;
  for (;;) {
    switch (*s) {
      case 0:   return r;
      case '?':
      case '*': return -1;
    }
    r++;
    s++;
  }
}

/* Convert 12-byte NUL-padded 8.3 name ("README.TXT\0\0") to 11-byte FCB
 * format ("README  TXT") — no dot, space-padded. */
static void name83_to_fcb(unsigned char *fcb11, const unsigned char *name12) {
  unsigned char i, j;
  for (i = 0; i < 11u; i++) fcb11[i] = ' ';
  i = 0; j = 0;
  while (i < 12u && name12[i] && name12[i] != '.') {
    if (j < 8u) fcb11[j++] = name12[i];
    i++;
  }
  if (i < 12u && name12[i] == '.') {
    i++; j = 8u;
    while (i < 12u && name12[i]) {
      if (j < 11u) fcb11[j++] = name12[i];
      i++;
    }
  }
}

/* FCB 11-byte template match; '?' is wildcard. tmpl may be a far pointer. */
static int fcb_match(const unsigned char far *tmpl, const unsigned char *name11) {
  unsigned char i;
  for (i = 0; i < 11u; i++) {
    if (tmpl[i] != '?' && tmpl[i] != name11[i]) return 0;
  }
  return 1;
}

/* Attribute filter: entry passes if its special bits (HIDDEN/SYS/DIR/VOL)
 * are all set in srch_attr, or are absent. */
static int attr_match(unsigned char entry_attr, unsigned char srch_attr) {
  return ((entry_attr & ~srch_attr & (0x02u|0x04u|0x10u|0x08u)) == 0);
}

/* Map SerialDFS status byte to DOS error code. */
static unsigned char dos_err_from_status(unsigned char st) {
  switch (st) {
    case STATUS_OK:       return 0;
    case ERR_NOT_FOUND:   return 2;
    case ERR_ACCESS:      return 5;
    case ERR_BAD_HANDLE:  return 6;
    case ERR_EXISTS:      return 80;
    case ERR_IO:          return 29;
    case ERR_CRC:         return 23;
    case ERR_TIMEOUT:     return 21;
    case ERR_UNSUPPORTED: return 1;
    case ERR_BAD_PATH:    return 3;
    default:              return 2;
  }
}

/* Extract the directory portion of fn1 into dir_path (NUL-terminated).
 * fn1 = "X:\SUBDIR\*.TXT"  →  dir_path = "\SUBDIR\"
 * fn1 = "X:\*.TXT"         →  dir_path = "\"            */
static void get_dir_path(unsigned char *dir_path, const unsigned char far *fn1) {
  unsigned short i, last_slash = 2u; /* fn1[2] is always the leading '\' */
  for (i = 2u; fn1[i] != 0; i++) {
    if (fn1[i] == '\\') last_slash = i;
  }
  /* copy fn1[2..last_slash] inclusive, then NUL-terminate */
  for (i = 0; i < (last_slash - 1u); i++) dir_path[i] = fn1[i + 2u];
  dir_path[last_slash - 1u] = 0;
}

/* Copy fn1[2..] (path after "X:") to buf + offset, append NUL.
 * Returns total bytes written (path length + 1 for NUL). */
static unsigned short copy_fn1_path(unsigned char *buf, unsigned short off,
                                    const unsigned char far *fn1) {
  unsigned short i = 0;
  while (fn1[i + 2u] != 0 && (off + i) < 509u) {
    buf[off + i] = fn1[i + 2u];
    i++;
  }
  buf[off + i] = 0;
  return i + 1u;
}

/* FINDFIRST/FINDNEXT batch scanner: scan dir_batch from dir_batch_idx for
 * the first entry matching tmpl (FCB 11-byte) and srch_attr.
 * Returns pointer to matching 23-byte entry, or NULL if none.
 * Updates dir_batch_idx to point past the matched entry. */
static unsigned char *scan_batch(const unsigned char far *tmpl,
                                 unsigned char srch_attr) {
  unsigned char fcbname[11];
  while (dir_batch_idx < dir_batch_count) {
    unsigned char *e = dir_batch + (unsigned int)dir_batch_idx * DIR_ENTRY_SIZE;
    dir_batch_idx++;
    if (!attr_match(e[12], srch_attr)) continue;
    name83_to_fcb(fcbname, e);
    if (fcb_match(tmpl, fcbname)) return e;
  }
  return NULL;
}

/* Fill glob_sdaptr->found_file from a 23-byte dir_batch entry. */
static void fill_found_file(const unsigned char *e) {
  unsigned char fcbname[11];
  name83_to_fcb(fcbname, e);
  copybytes(glob_sdaptr->found_file.fname, fcbname, 11u);
  glob_sdaptr->found_file.fattr        = e[12];
  glob_sdaptr->found_file.time_lstupd  = *(unsigned short far *)&e[15];
  glob_sdaptr->found_file.date_lstupd  = *(unsigned short far *)&e[13];
  glob_sdaptr->found_file.start_clstr  = 0;
  glob_sdaptr->found_file.fsize        = *(unsigned long far *)&e[17];
}

/* ── Phase 10: VGA status bar helpers (resident, BEGTEXT) ────────────────── */

/* Write one character to VGA row with 0x1E attr (blue bg / yellow text). */
static void vga_putch(unsigned short far *row, unsigned char *col, unsigned char ch) {
  if (*col < 80u)
    row[(*col)++] = (unsigned short)(0x1E00u | ch);
}

/* Write decimal number to VGA row. */
static void vga_putnum(unsigned short far *row, unsigned char *col, unsigned long val) {
  unsigned char tmp[10];
  unsigned char n = 0;
  do {
    tmp[n++] = (unsigned char)('0' + (unsigned char)(val % 10));
    val /= 10;
  } while (val > 0u && n < 10u);
  /* tmp is reversed */
  while (n > 0u && *col < 80u) {
    n--;
    row[(*col)++] = (unsigned short)(0x1E00u | tmp[n]);
  }
}

/* Write NUL-terminated string to VGA row. */
static void vga_putstr(unsigned short far *row, unsigned char *col, const char *s) {
  while (*s && *col < 80u)
    row[(*col)++] = (unsigned short)(0x1E00u | (unsigned char)*s++);
}

/* Render the status bar on row 24 if InDOS=0 and video mode=3. */
static void status_render_if_safe(void) {
  unsigned char far *indos;
  unsigned char video_mode;
  unsigned short far *row;
  unsigned char col;

  indos = MK_FP(glob_data.indos_seg, glob_data.indos_off);
  if (*indos != 0u) return;
  video_mode = *(unsigned char far *)MK_FP(0x40u, 0x49u);
  if (video_mode != 3u) return;

  stat_dirty = 0;
  row = (unsigned short far *)MK_FP(0xB800u, 24u * 80u * 2u);
  col = 0;

  vga_putstr(row, &col, "SDF TX:");
  vga_putnum(row, &col, glob_data.stat_tx_bytes);
  vga_putstr(row, &col, " RX:");
  vga_putnum(row, &col, glob_data.stat_rx_bytes);
  vga_putstr(row, &col, " TO:");
  vga_putnum(row, &col, (unsigned long)glob_data.stat_timeouts);
  vga_putstr(row, &col, " RT:");
  vga_putnum(row, &col, (unsigned long)glob_data.stat_retries);
  vga_putstr(row, &col, " CE:");
  vga_putnum(row, &col, (unsigned long)glob_data.stat_crc_errs);
  while (col < 80u)
    vga_putch(row, &col, ' ');
}

/* INT 1Ch (timer tick) handler — marks bar dirty.
 * union INTPACK r is required by _mvchain_intr calling convention. */
void __interrupt __far int1ch_handler(union INTPACK r) {
  _asm {
    jmp SKIP1CH
    SIG1CH DB 'S','D','1','C'
    SKIP1CH:
    push ax
    mov  ax, 0
    mov  ds, ax
    pop  ax
  }
  r.x.ax = r.x.ax;   /* _mvchain_intr needs INTPACK frame; suppress W303 */
  if (stat_enabled) stat_dirty = 1;
  _mvchain_intr(MK_FP(glob_data.prev_int1ch_seg, glob_data.prev_int1ch_off));
}

/* INT 28h (DOS idle) handler — triggers render when dirty.
 * union INTPACK r is required by _mvchain_intr calling convention. */
void __interrupt __far int28h_handler(union INTPACK r) {
  _asm {
    jmp SKIP28H
    SIG28H DB 'S','D','2','8'
    SKIP28H:
    push ax
    mov  ax, 0
    mov  ds, ax
    pop  ax
  }
  r.x.ax = r.x.ax;   /* _mvchain_intr needs INTPACK frame; suppress W303 */
  if (stat_dirty && stat_enabled)
    status_render_if_safe();
  _mvchain_intr(MK_FP(glob_data.prev_int28h_seg, glob_data.prev_int28h_off));
}

/* ── process2f() — INT 2Fh subfunction dispatcher ───────────────────────── */

void process2f(void) {
  unsigned char  subfunction;
  unsigned char  rstatus;
  unsigned int   plen;
  unsigned int   replylen;

  subfunction = glob_intregs.h.al;

  /* Assume success — cases set FAILFLAG on error */
  SUCCESSFLAG;

  switch (subfunction) {

    /* ── AL_RMDIR (01h) ─────────────────────────────────────────────────────── */
    case AL_RMDIR:
    {
      if (mystrlen(glob_sdaptr->fn1) < 2u) { FAILFLAG(3); break; }
      glob_req_payload[0] = 0;
      plen = 1u + copy_fn1_path(glob_req_payload, 1u, glob_sdaptr->fn1);
      if (serial_rpc(glob_data.comport, CMD_RMDIR,
                     glob_req_payload, plen,
                     glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
          || rstatus != STATUS_OK) {
        FAILFLAG(dos_err_from_status(rstatus));
      }
    }
    break;

    /* ── AL_MKDIR (03h) ─────────────────────────────────────────────────────── */
    case AL_MKDIR:
    {
      if (mystrlen(glob_sdaptr->fn1) < 2u) { FAILFLAG(3); break; }
      glob_req_payload[0] = 0;
      plen = 1u + copy_fn1_path(glob_req_payload, 1u, glob_sdaptr->fn1);
      if (serial_rpc(glob_data.comport, CMD_MKDIR,
                     glob_req_payload, plen,
                     glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
          || rstatus != STATUS_OK) {
        FAILFLAG(dos_err_from_status(rstatus));
      }
    }
    break;

    /* ── AL_CHDIR (05h) ───────────────────────────────────────────────────── */
    case AL_CHDIR:
    {
      /* Validate that the target path exists and is a directory via GET_ATTR */
      if (mystrlen(glob_sdaptr->fn1) < 2u) { FAILFLAG(3); break; }
      glob_req_payload[0] = 0; /* drive byte */
      plen = 1u + copy_fn1_path(glob_req_payload, 1u, glob_sdaptr->fn1);
      if (serial_rpc(glob_data.comport, CMD_GET_ATTR,
                     glob_req_payload, plen,
                     glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
          || rstatus != STATUS_OK || replylen < 1u) {
        FAILFLAG(3);
        break;
      }
      if (!(glob_rep_payload[0] & 0x10u)) { /* must be a directory */
        FAILFLAG(3);
      }
    }
    break;

    /* ── AL_CLSFIL (06h) — close file ────────────────────────────────────── */
    case AL_CLSFIL:
    {
      struct sftstruct far *sft = MK_FP(glob_intregs.x.es, glob_intregs.x.di);
      unsigned int cplen;
      if (sft->handle_count > 0u) sft->handle_count--;
      glob_req_payload[0] = (unsigned char)sft->start_sector; /* hid */
      if (sft->open_mode & 1u) {
        /* Propagate SFT timestamp to server: file_time hi=date lo=time. */
        *(unsigned short far *)&glob_req_payload[1] =
            (unsigned short)(sft->file_time >> 16);
        *(unsigned short far *)&glob_req_payload[3] =
            (unsigned short)(sft->file_time & 0xFFFFu);
        cplen = 5u;
      } else {
        cplen = 1u;
      }
      serial_rpc(glob_data.comport, CMD_CLOSE,
                 glob_req_payload, cplen,
                 glob_rep_payload, &replylen, &rstatus);
      /* ignore close errors — file position is already invalid */
    }
    break;

    /* ── AL_CMMTFIL (07h) — flush/commit ────────────────────────────────── */
    case AL_CMMTFIL:
    {
      struct sftstruct far *sft = MK_FP(glob_intregs.x.es, glob_intregs.x.di);
      glob_req_payload[0] = (unsigned char)sft->start_sector;
      if (serial_rpc(glob_data.comport, CMD_FLUSH,
                     glob_req_payload, 1u,
                     glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
          || rstatus != STATUS_OK) {
        FAILFLAG(dos_err_from_status(rstatus));
      }
    }
    break;

    /* ── AL_READFIL (08h) ────────────────────────────────────────────────── */
    case AL_READFIL:
    {
      struct sftstruct far *sft = MK_FP(glob_intregs.x.es, glob_intregs.x.di);
      unsigned short totread = 0, chunk, got;

      if (sft->open_mode & 1u) { FAILFLAG(5); break; } /* write-only */
      if (glob_intregs.x.cx == 0u) break;  /* 0-byte read = success, CX=0 */

      for (;;) {
        chunk = glob_intregs.x.cx - totread;
        if (chunk > FRAME_MAX_PAYLOAD) chunk = FRAME_MAX_PAYLOAD;

        /* CMD_READ payload v2: hid(1) + count(2 LE) + offset(4 LE) = 7 bytes.
         * The explicit offset makes the call idempotent so TSR retries on
         * timeout don't skip chunks (daemon's old position-tracking _read
         * advanced the cursor on every call, including retries). Daemon
         * accepts both 3-byte (legacy, position-tracked) and 7-byte payloads. */
        glob_req_payload[0] = (unsigned char)sft->start_sector; /* hid */
        *(unsigned short far *)&glob_req_payload[1] = chunk;
        *(unsigned long  far *)&glob_req_payload[3] = sft->file_pos + totread;

        if (serial_rpc(glob_data.comport, CMD_READ,
                       glob_req_payload, 7u,
                       glob_rep_payload, &replylen, &rstatus) != SERRPC_OK) {
          FAILFLAG(2);
          break;
        }
        if (rstatus != STATUS_OK && replylen == 0u) {
          FAILFLAG(dos_err_from_status(rstatus));
          break;
        }
        got = (unsigned short)replylen;
        copybytes(glob_sdaptr->curr_dta + totread, glob_rep_payload, got);
        totread += got;
        if (got < chunk || totread >= glob_intregs.x.cx) {
          sft->file_pos += totread;
          glob_intregs.x.cx = totread;
          break;
        }
      }
    }
    break;

    /* ── AL_WRITEFIL (09h) ──────────────────────────────────────────────────── */
    case AL_WRITEFIL:
    {
      struct sftstruct far *sft = MK_FP(glob_intregs.x.es, glob_intregs.x.di);
      unsigned short towrite = glob_intregs.x.cx;
      unsigned short written = 0;
      unsigned short chunk;

      if (!(sft->open_mode & 1u)) { FAILFLAG(5); break; } /* read-only handle */
      if (towrite == 0u) break;

      while (written < towrite) {
        chunk = towrite - written;
        if (chunk > (FRAME_MAX_PAYLOAD - 1u)) chunk = FRAME_MAX_PAYLOAD - 1u;
        glob_req_payload[0] = (unsigned char)sft->start_sector;
        copybytes(glob_req_payload + 1u, glob_sdaptr->curr_dta + written, chunk);
        if (serial_rpc(glob_data.comport, CMD_WRITE,
                       glob_req_payload, 1u + chunk,
                       glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
            || rstatus != STATUS_OK) {
          FAILFLAG(dos_err_from_status(rstatus));
          break;
        }
        written += chunk;
        sft->file_pos += chunk;
        if (sft->file_pos > sft->file_size) sft->file_size = sft->file_pos;
      }
      glob_intregs.x.cx = written;
    }
    break;

    /* ── AL_LOCKFIL (0Ah) / AL_UNLOCKFIL (0Bh) — no-op ──────────────────── */
    case AL_LOCKFIL:
    case AL_UNLOCKFIL:
      break;

    /* ── AL_DISKSPACE (0Ch) — synthetic disk info ────────────────────────── */
    case AL_DISKSPACE:
      glob_intregs.w.ax = 4;      /* sectors per cluster */
      glob_intregs.w.cx = 512;    /* bytes per sector */
      glob_intregs.w.bx = 32767u; /* total clusters */
      glob_intregs.w.dx = 16383u; /* available clusters */
      break;

    /* ── AL_GETATTR (0Fh) — get file/dir attributes ─────────────────────── */
    case AL_GETATTR:
    {
      if (mystrlen(glob_sdaptr->fn1) < 2u) { FAILFLAG(2); break; }
      glob_req_payload[0] = 0;
      plen = 1u + copy_fn1_path(glob_req_payload, 1u, glob_sdaptr->fn1);
      if (serial_rpc(glob_data.comport, CMD_GET_ATTR,
                     glob_req_payload, plen,
                     glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
          || rstatus != STATUS_OK || replylen < 9u) {
        FAILFLAG(rstatus == STATUS_OK ? 2u : dos_err_from_status(rstatus));
        break;
      }
      /* Reply: attr(1) + size(4 LE) + date(2 LE) + time(2 LE) */
      glob_intregs.w.ax = glob_rep_payload[0];                         /* attr */
      glob_intregs.w.di = *(unsigned short far *)&glob_rep_payload[1]; /* size lo */
      glob_intregs.w.bx = *(unsigned short far *)&glob_rep_payload[3]; /* size hi */
      glob_intregs.w.dx = *(unsigned short far *)&glob_rep_payload[5]; /* date */
      glob_intregs.w.cx = *(unsigned short far *)&glob_rep_payload[7]; /* time */
    }
    break;

    /* ── AL_SETATTR (0Eh) ───────────────────────────────────────────────────── */
    case AL_SETATTR:
    {
      unsigned char newattr = (unsigned char)(glob_reqstkword & 0xFFu);
      if (mystrlen(glob_sdaptr->fn1) < 2u) { FAILFLAG(2); break; }
      glob_req_payload[0] = 0;
      plen = 1u + copy_fn1_path(glob_req_payload, 1u, glob_sdaptr->fn1);
      glob_req_payload[plen] = newattr;
      plen++;
      if (serial_rpc(glob_data.comport, CMD_SET_ATTR,
                     glob_req_payload, plen,
                     glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
          || rstatus != STATUS_OK) {
        FAILFLAG(dos_err_from_status(rstatus));
      }
    }
    break;

    /* ── AL_RENAME (11h) ─────────────────────────────────────────────────────── */
    case AL_RENAME:
    {
      unsigned short fn1len, fn2len;
      if (mystrlen(glob_sdaptr->fn1) < 2u || mystrlen(glob_sdaptr->fn2) < 2u) {
        FAILFLAG(3); break;
      }
      /* CMD_RENAME payload: old_path\0new_path\0 (no drive byte) */
      fn1len = copy_fn1_path(glob_req_payload, 0u, glob_sdaptr->fn1);
      fn2len = copy_fn1_path(glob_req_payload, fn1len, glob_sdaptr->fn2);
      plen   = fn1len + fn2len;
      if (serial_rpc(glob_data.comport, CMD_RENAME,
                     glob_req_payload, plen,
                     glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
          || rstatus != STATUS_OK) {
        FAILFLAG(dos_err_from_status(rstatus));
      }
    }
    break;

    /* ── AL_DELETE (13h) ─────────────────────────────────────────────────────── */
    case AL_DELETE:
    {
      if (mystrlen(glob_sdaptr->fn1) < 2u) { FAILFLAG(2); break; }
      glob_req_payload[0] = 0;
      plen = 1u + copy_fn1_path(glob_req_payload, 1u, glob_sdaptr->fn1);
      if (serial_rpc(glob_data.comport, CMD_DELETE,
                     glob_req_payload, plen,
                     glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
          || rstatus != STATUS_OK) {
        FAILFLAG(dos_err_from_status(rstatus));
      }
    }
    break;

    /* ── AL_OPEN (16h) — open existing file ─────────────────────────────────── */
    case AL_OPEN:
    {
      struct sftstruct far *sft = MK_FP(glob_intregs.x.es, glob_intregs.x.di);
      unsigned char hid;
      unsigned char open_mode_byte;

      if (len_if_no_wildcards(glob_sdaptr->fn1) < 2) { FAILFLAG(3); break; }

      open_mode_byte = (unsigned char)(sft->open_mode & 0x03u);
      /* CMD_OPEN: mode(1) + path + NUL */
      glob_req_payload[0] = open_mode_byte;
      plen = 1u + copy_fn1_path(glob_req_payload, 1u, glob_sdaptr->fn1);
      if (serial_rpc(glob_data.comport, CMD_OPEN,
                     glob_req_payload, plen,
                     glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
          || rstatus != STATUS_OK || replylen < 1u) {
        FAILFLAG(dos_err_from_status(rstatus));
        break;
      }
      hid = glob_rep_payload[0];

      /* CMD_GET_ATTR for SFT fill: attr(1)+size(4)+date(2)+time(2) */
      glob_req_payload[0] = 0;
      plen = 1u + copy_fn1_path(glob_req_payload, 1u, glob_sdaptr->fn1);
      if (serial_rpc(glob_data.comport, CMD_GET_ATTR,
                     glob_req_payload, plen,
                     glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
          || rstatus != STATUS_OK || replylen < 9u) {
        /* Can't fill SFT — close the server handle and fail */
        unsigned char cp[1];
        unsigned int crl;
        unsigned char crs;
        cp[0] = hid;
        serial_rpc(glob_data.comport, CMD_CLOSE, cp, 1u, glob_rep_payload, &crl, &crs);
        FAILFLAG(2);
        break;
      }

      /* Fill SFT fields */
      sft->file_attr     = glob_rep_payload[0];
      sft->dev_info_word = 0x8040u | glob_reqdrv; /* network + not-unwritten + drive */
      sft->dev_drvr_ptr  = NULL;
      sft->start_sector  = hid;
      sft->file_size     = *(unsigned long far *)&glob_rep_payload[1];
      /* file_time: high word=date, low word=time */
      sft->file_time     = ((unsigned long)(*(unsigned short far *)&glob_rep_payload[5]) << 16)
                           | *(unsigned short far *)&glob_rep_payload[7];
      sft->file_pos      = 0;
      sft->open_mode    &= 0xFF00u;
      sft->open_mode    |= open_mode_byte;
      sft->rel_sector    = 0xFFFFu;
      sft->abs_sector    = 0xFFFFu;
      sft->dir_sector    = 0;
      sft->dir_entry_no  = 0xFFu;
      copybytes(sft->file_name, glob_sdaptr->fcb_fn1, 11u);
    }
    break;

    /* ── AL_CREATE (17h) ─────────────────────────────────────────────────────── */
    case AL_CREATE:
    {
      struct sftstruct far *sft = MK_FP(glob_intregs.x.es, glob_intregs.x.di);
      unsigned char hid;
      unsigned char open_mode_byte = 1; /* write-only after CREAT */

      if (len_if_no_wildcards(glob_sdaptr->fn1) < 2) { FAILFLAG(3); break; }

      /* CMD_CREATE mode=1: create or truncate existing */
      glob_req_payload[0] = 1;
      plen = 1u + copy_fn1_path(glob_req_payload, 1u, glob_sdaptr->fn1);
      if (serial_rpc(glob_data.comport, CMD_CREATE,
                     glob_req_payload, plen,
                     glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
          || rstatus != STATUS_OK || replylen < 1u) {
        FAILFLAG(dos_err_from_status(rstatus));
        break;
      }
      hid = glob_rep_payload[0];

      /* CMD_GET_ATTR for SFT fill */
      glob_req_payload[0] = 0;
      plen = 1u + copy_fn1_path(glob_req_payload, 1u, glob_sdaptr->fn1);
      if (serial_rpc(glob_data.comport, CMD_GET_ATTR,
                     glob_req_payload, plen,
                     glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
          || rstatus != STATUS_OK || replylen < 9u) {
        unsigned char cp[1]; unsigned int crl; unsigned char crs;
        cp[0] = hid;
        serial_rpc(glob_data.comport, CMD_CLOSE, cp, 1u, glob_rep_payload, &crl, &crs);
        FAILFLAG(2); break;
      }
      sft->file_attr     = glob_rep_payload[0];
      sft->dev_info_word = 0x8040u | glob_reqdrv;
      sft->dev_drvr_ptr  = NULL;
      sft->start_sector  = hid;
      sft->file_size     = *(unsigned long far *)&glob_rep_payload[1];
      sft->file_time     = ((unsigned long)(*(unsigned short far *)&glob_rep_payload[5]) << 16)
                           | *(unsigned short far *)&glob_rep_payload[7];
      sft->file_pos      = 0;
      sft->open_mode    &= 0xFF00u;
      sft->open_mode    |= open_mode_byte;
      sft->rel_sector    = 0xFFFFu; sft->abs_sector = 0xFFFFu;
      sft->dir_sector    = 0; sft->dir_entry_no = 0xFFu;
      copybytes(sft->file_name, glob_sdaptr->fcb_fn1, 11u);
    }
    break;

    /* ── AL_SPOPNFIL (2Eh) — extended open/create ─────────────────────────
     *
     * spop_act (DOS INT 21h AH=6Ch DX action code) layout:
     *   hi nibble: action if file EXISTS — 0=fail, 1=open, 2=truncate
     *   lo nibble: action if file MISSING — 0=fail, 1=create
     *
     * Strategy: probe with CMD_OPEN whenever the action could legitimately
     * open an existing file (hi_act == 1) OR when it's the "exclusive
     * create" code (hi=0,lo=1) that DOS 6.22 COMMAND.COM uses as an
     * existence test for COPY's source and TYPE — DOS doesn't recover
     * gracefully from the error-80 ("file exists") response we'd otherwise
     * have to send, so falling through to OPEN matches its empirical
     * expectation. If OPEN succeeds we either return the handle directly
     * (hi_act=1 or 0+1) or close it and re-do via CMD_CREATE for the
     * truncate-existing case (hi_act=2).
     */
    case AL_SPOPNFIL:
    {
      struct sftstruct far *sft = MK_FP(glob_intregs.x.es, glob_intregs.x.di);
      unsigned char hid;
      unsigned char open_mode_byte = (unsigned char)(sft->open_mode & 0x03u);
      unsigned char lo_act = (unsigned char)(glob_sdaptr->spop_act & 0x0Fu);  /* if not exists */
      unsigned char hi_act = (unsigned char)((glob_sdaptr->spop_act >> 4) & 0x0Fu); /* if exists */
      unsigned char file_exists = 0;

      if (len_if_no_wildcards(glob_sdaptr->fn1) < 2) { FAILFLAG(3); break; }

      /* Probe: try OPEN if any action wants to interact with an existing file. */
      if (hi_act == 1 || hi_act == 2 ||
          (hi_act == 0 && lo_act == 1)) {
        glob_req_payload[0] = open_mode_byte;
        plen = 1u + copy_fn1_path(glob_req_payload, 1u, glob_sdaptr->fn1);
        if (serial_rpc(glob_data.comport, CMD_OPEN,
                       glob_req_payload, plen,
                       glob_rep_payload, &replylen, &rstatus) == SERRPC_OK
            && rstatus == STATUS_OK && replylen >= 1u) {
          hid = glob_rep_payload[0];
          file_exists = 1;
          if (hi_act == 2) {
            /* Truncate-existing: close OPEN handle and re-do via CREATE
             * mode=1 (truncate). */
            unsigned char cp[1]; unsigned int crl; unsigned char crs;
            cp[0] = hid;
            serial_rpc(glob_data.comport, CMD_CLOSE, cp, 1u, glob_rep_payload, &crl, &crs);
            /* fall through to CREATE branch below */
          } else {
            /* hi_act == 1 (open existing) OR hi_act == 0 (DOS probe) —
             * return the open handle. CX=1 means "opened existing". */
            glob_intregs.x.cx = 1;
            goto spop_fill_sft;
          }
        } else if (rstatus != STATUS_OK && rstatus != ERR_NOT_FOUND) {
          /* Real error from the probe (CRC, timeout, access). Fail through
           * to DOS rather than silently masking it. */
          FAILFLAG(dos_err_from_status(rstatus)); break;
        }
        /* OPEN-not-found: fall through; CREATE branch will fire if lo_act==1 */
      }

      /* CREATE branch: file doesn't exist (or hi_act==2 truncate path). */
      if ((hi_act == 2 && file_exists) || lo_act == 1) {
        unsigned char cmode = (hi_act == 2) ? 1 : 0; /* truncate or create-new */
        glob_req_payload[0] = cmode;
        plen = 1u + copy_fn1_path(glob_req_payload, 1u, glob_sdaptr->fn1);
        if (serial_rpc(glob_data.comport, CMD_CREATE,
                       glob_req_payload, plen,
                       glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
            || rstatus != STATUS_OK || replylen < 1u) {
          FAILFLAG(dos_err_from_status(rstatus)); break;
        }
        hid = glob_rep_payload[0];
        glob_intregs.x.cx = (hi_act == 2 && file_exists) ? 3u : 2u; /* truncated/created */
      } else {
        /* hi_act==1, lo_act==0, file not found → "open-only" miss */
        FAILFLAG(2); break;
      }

      spop_fill_sft:
      glob_req_payload[0] = 0;
      plen = 1u + copy_fn1_path(glob_req_payload, 1u, glob_sdaptr->fn1);
      if (serial_rpc(glob_data.comport, CMD_GET_ATTR,
                     glob_req_payload, plen,
                     glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
          || rstatus != STATUS_OK || replylen < 9u) {
        unsigned char cp[1]; unsigned int crl; unsigned char crs;
        cp[0] = hid;
        serial_rpc(glob_data.comport, CMD_CLOSE, cp, 1u, glob_rep_payload, &crl, &crs);
        FAILFLAG(2); break;
      }
      sft->file_attr     = glob_rep_payload[0];
      sft->dev_info_word = 0x8040u | glob_reqdrv;
      sft->dev_drvr_ptr  = NULL;
      sft->start_sector  = hid;
      sft->file_size     = *(unsigned long far *)&glob_rep_payload[1];
      sft->file_time     = ((unsigned long)(*(unsigned short far *)&glob_rep_payload[5]) << 16)
                           | *(unsigned short far *)&glob_rep_payload[7];
      sft->file_pos      = 0;
      sft->open_mode    &= 0xFF00u;
      sft->open_mode    |= open_mode_byte;
      sft->rel_sector    = 0xFFFFu; sft->abs_sector = 0xFFFFu;
      sft->dir_sector    = 0; sft->dir_entry_no = 0xFFu;
      copybytes(sft->file_name, glob_sdaptr->fcb_fn1, 11u);
    }
    break;

    /* ── AL_FINDFIRST (1Bh) / AL_FINDNEXT (1Ch) ─────────────────────────── */
    case AL_FINDFIRST:
    case AL_FINDNEXT:
    {
      struct sdbstruct far *dta;
      unsigned char        *e;
      unsigned char         srch_attr;
      const unsigned char far *tmpl;
      unsigned char         dir_path[65];

      if (subfunction == AL_FINDFIRST) {
        dta       = (struct sdbstruct far *)(glob_sdaptr->curr_dta);
        srch_attr = glob_sdaptr->srch_attr;
        tmpl      = glob_sdaptr->fcb_fn1;

        /* Extract directory portion of fn1 and send CMD_LIST_DIR */
        get_dir_path(dir_path, glob_sdaptr->fn1);
        glob_req_payload[0] = 0; /* drive byte */
        {
          unsigned short i = 0;
          while (dir_path[i] && i < 63u) {
            glob_req_payload[1u + i] = dir_path[i];
            i++;
          }
          glob_req_payload[1u + i] = 0;
          plen = 2u + i;
        }

        if (serial_rpc(glob_data.comport, CMD_LIST_DIR,
                       glob_req_payload, plen,
                       glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
            || rstatus != STATUS_OK || replylen < 1u) {
          FAILFLAG(2);
          break;
        }

        /* Reply: hid(1) + entries(n*23) */
        dir_hid = glob_rep_payload[0];
        {
          unsigned short n = (replylen - 1u) / DIR_ENTRY_SIZE;
          if (n > DIR_BATCH_MAX) n = DIR_BATCH_MAX;
          copybytes(dir_batch, glob_rep_payload + 1u, n * DIR_ENTRY_SIZE);
          dir_batch_count = (unsigned char)n;
          dir_batch_idx   = 0;
          dir_batch_more  = (n > 0u) ? dir_batch[(n - 1u) * DIR_ENTRY_SIZE + 21u] : 0;
        }

        /* Init DTA header for this search */
        dta->drv_lett  = glob_reqdrv | 0x80u;
        copybytes(dta->srch_tmpl, glob_sdaptr->fcb_fn1, 11u);
        dta->srch_attr = srch_attr;

      } else {
        /* FINDNEXT: DTA from ES:DI */
        dta       = MK_FP(glob_intregs.x.es, glob_intregs.x.di);
        srch_attr = dta->srch_attr;
        tmpl      = dta->srch_tmpl;
        /* Restore batch position from DTA */
        dir_batch_idx = (unsigned char)(dta->dir_entry);
      }

      /* Scan the cached batch for a matching entry */
      for (;;) {
        e = scan_batch(tmpl, srch_attr);
        if (e != NULL) break;

        /* Current batch exhausted — fetch more if available */
        if (!dir_batch_more) {
          e = NULL;
          break;
        }
        glob_req_payload[0] = dir_hid;
        if (serial_rpc(glob_data.comport, CMD_LIST_DIR_NEXT,
                       glob_req_payload, 1u,
                       glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
            || rstatus != STATUS_OK) {
          e = NULL;
          break;
        }
        if (replylen == 0u) {
          dir_batch_more = 0;
          e = NULL;
          break;
        }
        {
          unsigned short n = replylen / DIR_ENTRY_SIZE;
          if (n > DIR_BATCH_MAX) n = DIR_BATCH_MAX;
          copybytes(dir_batch, glob_rep_payload, n * DIR_ENTRY_SIZE);
          dir_batch_count = (unsigned char)n;
          dir_batch_idx   = 0;
          dir_batch_more  = (n > 0u) ? dir_batch[(n - 1u) * DIR_ENTRY_SIZE + 21u] : 0;
        }
      }

      if (e == NULL) {
        FAILFLAG(subfunction == AL_FINDFIRST ? 2u : 18u);
        break;
      }

      /* Fill found_file and update DTA */
      fill_found_file(e);
      dta->par_clstr = dir_hid;
      dta->dir_entry = dir_batch_idx;
      /* Copy found_file to DTA+sizeof(sdbstruct) = DTA+0x15 */
      copybytes((unsigned char far *)dta + (unsigned int)sizeof(struct sdbstruct),
                &(glob_sdaptr->found_file),
                sizeof(struct foundfilestruct));
    }
    break;

    /* ── AL_SKFMEND (21h) — seek from end ───────────────────────────────── */
    case AL_SKFMEND:
    {
      struct sftstruct far *sft = MK_FP(glob_intregs.x.es, glob_intregs.x.di);
      unsigned long new_pos;

      /* Payload: hid(1) + whence=2(1) + offset(4 signed LE from CX:DX) */
      glob_req_payload[0] = (unsigned char)sft->start_sector; /* hid */
      glob_req_payload[1] = 2; /* SEEK_END */
      *(unsigned short far *)&glob_req_payload[2] = glob_intregs.x.dx; /* offset lo */
      *(unsigned short far *)&glob_req_payload[4] = glob_intregs.x.cx; /* offset hi */

      if (serial_rpc(glob_data.comport, CMD_SEEK,
                     glob_req_payload, 6u,
                     glob_rep_payload, &replylen, &rstatus) != SERRPC_OK
          || rstatus != STATUS_OK || replylen < 4u) {
        FAILFLAG(2);
        break;
      }

      new_pos   = *(unsigned long far *)glob_rep_payload;
      sft->file_pos = new_pos;
      glob_intregs.w.ax = (unsigned short)(new_pos & 0xFFFFu);
      glob_intregs.w.dx = (unsigned short)(new_pos >> 16);
    }
    break;

    /* ── AL_UNKNOWN_2D (2Dh) ─────────────────────────────────────────────── */
    case AL_UNKNOWN_2D:
      glob_intregs.w.ax = 2;
      break;

  } /* end switch */
}


/* ── inthandler() — hooked on INT 2Fh ───────────────────────────────────── */

void __interrupt __far inthandler(union INTPACK r) {
  /* Static signature patched at install time to contain the new DS.
   * 'S','D','F','S' is our TSR fingerprint; updatetsrds() and the unload
   * path scan for it to locate the MOV AX,0 / MOV DS,AX pair. */
  _asm {
    jmp SKIPTSRSIG
    TSRSIG DB 'S','D','F','S'
    SKIPTSRSIG:
    push ax
    mov  ax, 0        /* patched by updatetsrds() with new DS value */
    mov  ds, ax
    /* Save one word from the caller's stack (used by SETATTR) */
    mov  ax, ss:[BP+30]
    mov  glob_reqstkword, ax
    pop  ax
  }

  /* Is this a multiplex call for us? */
  if (r.h.ah == glob_multiplexid) {
    if (r.h.al == 0) { /* install-check */
      r.h.al = 0xFF;
      r.w.bx = SDF_MUXBX;
      r.w.cx = SDF_MUXCX;
      return;
    }
    if (r.h.al == 1 && r.w.bx == SDF_MUXBX) { /* get data pointer */
      _asm {
        push ds
        pop glob_reqstkword
      }
      r.w.ax = 0;
      r.w.bx = glob_reqstkword;
      r.w.cx = FP_OFF(&glob_data);
      return;
    }
  }

  /* Chain to previous handler if not a redirector call, or unsupported */
  if ((r.h.ah != 0x11) || (r.h.al == AL_INSTALLCHK) ||
      (r.h.al > 0x2Eu) || (supportedfunctions[r.h.al] == AL_UNKNOWN))
    goto CHAINTOPREVHANDLER;

  /* Determine which drive the call is for */
  if (((r.h.al >= AL_CLSFIL) && (r.h.al <= AL_UNLOCKFIL))
      || (r.h.al == AL_SKFMEND) || (r.h.al == AL_UNKNOWN_2D)) {
    struct sftstruct far *sft = MK_FP(r.w.es, r.w.di);
    glob_reqdrv = sft->dev_info_word & 0x3Fu;
  } else {
    switch (r.h.al) {
      case AL_FINDNEXT:
        glob_reqdrv = glob_sdaptr->sdb.drv_lett & 0x1Fu;
        break;
      case AL_SETATTR:
      case AL_GETATTR:
      case AL_DELETE:
      case AL_OPEN:
      case AL_CREATE:
      case AL_SPOPNFIL:
      case AL_MKDIR:
      case AL_RMDIR:
      case AL_CHDIR:
      case AL_RENAME:
      case AL_FINDFIRST:
        glob_reqdrv = DRIVETONUM(glob_sdaptr->fn1[0]);
        break;
      default:
        {
          struct cdsstruct far *cds = MK_FP(r.w.es, r.w.di);
          glob_reqdrv = DRIVETONUM(cds->current_path[0]);
        }
        break;
    }
  }

  /* Validate drive: must be mapped to us */
  if ((glob_reqdrv > 25u) || (glob_data.ldrv[glob_reqdrv] == 0xFFu))
    goto CHAINTOPREVHANDLER;

  /* Normalise fcb_fn1 from fn1 (handles "CD .." etc.) */
  if (r.h.al != AL_DISKSPACE) {
    unsigned short i;
    unsigned char far *path = glob_sdaptr->fn1;
    for (i = 0;; i++) {
      if (glob_sdaptr->fn1[i] == '\\') path = glob_sdaptr->fn1 + i + 1;
      if (glob_sdaptr->fn1[i] == 0) break;
    }
    for (i = 0; i < 11u; i++) glob_sdaptr->fcb_fn1[i] = ' ';
    for (i = 0; *path != 0; path++) {
      if (*path == '.') {
        i = 8;
      } else if (i < 11u) {
        glob_sdaptr->fcb_fn1[i++] = *path;
      }
    }
  }

  /* Save registers, switch to resident stack, call dispatcher, restore */
  copybytes(&glob_intregs, &r, sizeof(union INTPACK));
  _asm {
    cli
    mov glob_oldstack_seg, SS
    mov glob_oldstack_off, SP
    mov ax, ds
    mov ss, ax
    mov sp, DATASEGSZ-2
    sti
  }
  process2f();
  _asm {
    cli
    mov SS, glob_oldstack_seg
    mov SP, glob_oldstack_off
    sti
  }
  copybytes(&r, &glob_intregs, sizeof(union INTPACK));
  return;

  CHAINTOPREVHANDLER:
  _mvchain_intr(MK_FP(glob_data.prev_2f_handler_seg,
                      glob_data.prev_2f_handler_off));
}


/* ═══════════════════════ HERE ENDS THE RESIDENT PART ════════════════════ */
#pragma code_seg("_TEXT", "CODE")

/* Low-water mark for TSR size calculation: DX = offset begtextend in main() */
void begtextend(void) {}


/* ── Transient-only helper functions ─────────────────────────────────────── */

static struct sdastruct far *getsda(void) {
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
  return MK_FP(rds, rsi);
}

static struct cdsstruct far *getcds(unsigned int drive) {
  static unsigned char far *dir;
  static int  ok = -1;
  static unsigned char lastdrv;
  if (ok == -1) {
    ok = 1;
    _asm {
      push si
      mov  ah, 52h
      int  21h
      mov  si, 21h
      mov  ah, byte ptr es:[bx+si]
      mov  lastdrv, ah
      mov  si, 16h
      les  bx, es:[bx+si]
      mov  word ptr dir+2, es
      mov  word ptr dir, bx
      pop  si
    }
    if (dir == (unsigned char far *)-1L) ok = 0;
  }
  if (ok == 0 || drive > (unsigned int)lastdrv) return NULL;
  return (struct cdsstruct far *)((unsigned char far *)dir + drive * 0x58u);
}

static void outmsg(char *s) {
  _asm {
    mov ah, 9h
    mov dx, s
    int 21h
  }
}

static void zerobytes(void *obj, unsigned short l) {
  unsigned char *p = (unsigned char *)obj;
  while (l-- != 0) *p++ = 0;
}

/* Append decimal representation of val to buf; return chars written. */
static int appendulong(char *buf, unsigned long val) {
  char tmp[10];
  int n = 0, i;
  do { tmp[n++] = (char)('0' + (char)(val % 10)); val /= 10; } while (val > 0 && n < 10);
  for (i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
  return n;
}


static unsigned short allocseg(unsigned short sz) {
  unsigned short volatile res = 0;
  sz += 15u; sz >>= 4;
  _asm {
    push cx
    mov  ax, 5800h
    int  21h
    mov  cx, ax
    mov  ax, 5801h
    mov  bx, 2
    int  21h
    mov  ah, 48h
    mov  bx, sz
    int  21h
    jc   allocfail
    mov  res, ax
    allocfail:
    mov  ax, 5801h
    mov  bx, cx
    int  21h
    pop  cx
  }
  return res;
}

static void freeseg(unsigned short segm) {
  _asm {
    mov ah, 49h
    mov es, segm
    int 21h
  }
}

/* Patch a handler's MOV AX,0/MOV DS,AX pair so it loads the correct DS.
 * Each handler begins: JMP SKIP(2B) + 4-byte sig + push ax + MOV AX,imm16.
 * patchhandlerds scans ptr+2 for sig[0..3] then patches the imm16.       */
static int patchhandlerds(void far *handler, char s0, char s1, char s2, char s3) {
  unsigned short newds = 0;
  unsigned char far *ptr;
  unsigned short far *sptr;
  int i;
  _asm {
    push ds
    pop newds
  }
  /* Watcom inserts a ~24-byte __interrupt prologue before the _asm block.
   * Scan up to 32 bytes for the 4-byte signature to stay robust across
   * optimisation settings. Layout after sig: push_ax(1) + B8(1) + imm16(2). */
  for (i = 0; i <= 28; i++) {
    ptr = (unsigned char far *)handler + i;
    if (ptr[0] == (unsigned char)s0 && ptr[1] == (unsigned char)s1 &&
        ptr[2] == (unsigned char)s2 && ptr[3] == (unsigned char)s3) {
      sptr = (unsigned short far *)ptr;
      sptr[3] = newds;  /* patch imm16 of MOV AX,0 at sig+6 */
      return 0;
    }
  }
  return -1;
}

/* Patch inthandler()'s DS-load using the 'SDFS' signature. */
static int updatetsrds(void) {
  return patchhandlerds((void far *)inthandler, 'S', 'D', 'F', 'S');
}

/* Scan INT 2Fh range C0h–FFh for a free multiplex slot.
 * Also detects an already-loaded SerialDFS instance (presentflag). */
static unsigned char findfreemultiplex(unsigned char *presentflag) {
  unsigned char id = 0, freeid = 0, pflag = 0;
  _asm {
    mov  id, 0C0h
    checkid:
    xor  al, al
    mov  ah, id
    int  2Fh
    test al, al
    jnz  notfree
    mov  freeid, ah
    jmp  nexid
    notfree:
    cmp  al, 0FFh
    jne  nexid
    cmp  bx, SDF_MUXBX
    jne  nexid
    cmp  cx, SDF_MUXCX
    jne  nexid
    mov  ah, id
    mov  freeid, ah
    mov  pflag, 1
    jmp  done
    nexid:
    inc  id
    jnz  checkid
    done:
  }
  *presentflag = pflag;
  return freeid;
}


/* ── Argument parsing ────────────────────────────────────────────────────── */

#define ARGFL_QUIET   1u
#define ARGFL_UNLOAD  2u
#define ARGFL_STATUS  4u
#define ARGFL_STATS   8u   /* query-only: print stats and exit */

struct argstruct {
  int            argc;
  char         **argv;
  unsigned char  comport;   /* SERUART_COM1 or SERUART_COM2 */
  unsigned int   baudiv;    /* UART divisor */
  unsigned char  flags;
};

static int parseargv(struct argstruct *args) {
  int i, drivemapflag = 0;
  for (i = 1; i < args->argc; i++) {
    char *a = args->argv[i];
    /* drive mapping: e.g. "C-X" */
    if ((a[0]>='A'&&a[0]<='Z') && a[1]=='-' &&
        (a[2]>='A'&&a[2]<='Z') && a[3]==0) {
      unsigned char rdrv = DRIVETONUM(a[0]);
      unsigned char ldrv = DRIVETONUM(a[2]);
      if (ldrv > 25u || rdrv > 25u) return -2;
      if (glob_data.ldrv[ldrv] != 0xFFu) return -2;
      glob_data.ldrv[ldrv] = rdrv;
      drivemapflag = 1;
      continue;
    }
    /* option (starts with '/') */
    if (a[0] != '/') return -3;
    a++;
    if (a[0]=='C'&&a[1]=='O'&&a[2]=='M') {
      if (a[3]=='1'&&a[4]==0) { args->comport = SERUART_COM1; continue; }
      if (a[3]=='2'&&a[4]==0) { args->comport = SERUART_COM2; continue; }
      return -4;
    }
    if (a[0]=='B'&&a[1]=='A'&&a[2]=='U'&&a[3]=='D'&&a[4]==':') {
      long baud = 0;
      char *p = a + 5;
      if (*p == 0) return -4;
      while (*p >= '0' && *p <= '9') { baud = baud * 10 + (*p - '0'); p++; }
      if (*p != 0) return -4;
      args->baudiv = seruart_baud_to_div(baud);
      if (args->baudiv == 0) return -4;
      continue;
    }
    if (a[0]=='S'&&a[1]=='T'&&a[2]=='A'&&a[3]=='T'&&a[4]=='S'&&a[5]==0) {
      args->flags |= ARGFL_STATS; continue;
    }
    if (a[0]=='S'&&a[1]=='T'&&a[2]=='A'&&a[3]=='T'&&a[4]=='U'&&a[5]=='S'&&a[6]==0) {
      args->flags |= ARGFL_STATUS; continue;
    }
    if (a[0]=='N'&&a[1]=='O'&&a[2]=='S'&&a[3]=='T'&&a[4]=='A'&&a[5]=='T'&&a[6]=='U'&&a[7]=='S'&&a[8]==0) {
      args->flags &= ~ARGFL_STATUS; continue;
    }
    if ((a[0]=='q'||a[0]=='Q') && a[1]==0) { args->flags |= ARGFL_QUIET;  continue; }
    if ((a[0]=='u'||a[0]=='U') && a[1]==0) { args->flags |= ARGFL_UNLOAD; continue; }
    return -5;
  }
  if (args->flags & ARGFL_UNLOAD) {
    if (drivemapflag) return -1;
    return 0;
  }
  if (args->flags & ARGFL_STATS) {
    return 0;   /* query-only; no drive map or COM port needed */
  }
  if (!drivemapflag) return -6;
  if (!args->comport) return -7;     /* /COMn required */
  if (!args->baudiv) args->baudiv = SERUART_DIV_9600;
  return 0;
}


/* ── main() ─────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
  struct argstruct args;
  struct cdsstruct far *cds;
  unsigned char tmpflag = 0;
  int i;
  unsigned short volatile newdataseg;

  /* Initialise all drive mappings as unused */
  for (i = 0; i < 26; i++) glob_data.ldrv[i] = 0xFFu;

  /* Parse arguments */
  zerobytes(&args, sizeof(args));
  args.argc = argc;
  args.argv = argv;
  if (parseargv(&args) != 0) {
    #include "msg/help.c"
    return 1;
  }

  /* Handle /STATS — query counters from resident and print */
  if (args.flags & ARGFL_STATS) {
    unsigned char  serdfsid;
    unsigned short myseg = 0, myoff = 0;
    struct tsrshareddata far *tsrdata;
    char line[48];
    int  k;

    serdfsid = findfreemultiplex(&tmpflag);
    if (!tmpflag) {
      outmsg("SerialDFS not loaded.\r\n$");
      return 1;
    }
    myseg = 0xFFFFu;
    _asm {
      push ax
      push bx
      push cx
      pushf
      mov  ah, serdfsid
      mov  al, 1
      mov  bx, SDF_MUXBX
      int  2Fh
      test ax, ax
      jnz  statsgetfail
      mov  myseg, bx
      mov  myoff, cx
      statsgetfail:
      popf
      pop  cx
      pop  bx
      pop  ax
    }
    if (myseg == 0xFFFFu) {
      #include "msg/tsrcomfa.c"
      return 1;
    }
    tsrdata = MK_FP(myseg, myoff);

    /* "TX:NNNNNNNNNN RX:NNNNNNNNNN TO:NNNNN RT:NNNNN CE:NNNNN\r\n$" */
    k = 0;
    line[k++]='T'; line[k++]='X'; line[k++]=':';
    k += appendulong(line + k, tsrdata->stat_tx_bytes);
    line[k++]=' '; line[k++]='R'; line[k++]='X'; line[k++]=':';
    k += appendulong(line + k, tsrdata->stat_rx_bytes);
    line[k++]='\r'; line[k++]='\n'; line[k++]='$';
    outmsg(line);

    k = 0;
    line[k++]='T'; line[k++]='O'; line[k++]=':';
    k += appendulong(line + k, (unsigned long)tsrdata->stat_timeouts);
    line[k++]=' '; line[k++]='R'; line[k++]='T'; line[k++]=':';
    k += appendulong(line + k, (unsigned long)tsrdata->stat_retries);
    line[k++]=' '; line[k++]='C'; line[k++]='E'; line[k++]=':';
    k += appendulong(line + k, (unsigned long)tsrdata->stat_crc_errs);
    line[k++]='\r'; line[k++]='\n'; line[k++]='$';
    outmsg(line);
    return 0;
  }

  /* Require DOS 5+ */
  _asm {
    mov  ax, 3306h
    int  21h
    mov  tmpflag, bl
    inc  al
    jnz  dosvok
    mov  tmpflag, 0
    dosvok:
  }
  if (tmpflag < 5u) {
    #include "msg/unsupdos.c"
    return 1;
  }

  /* Check redirector installation is permitted */
  _asm {
    mov  tmpflag, 0
    mov  ax, 1100h
    int  2Fh
    dec  ax
    jnz  redirchkdone
    mov  tmpflag, 1
    redirchkdone:
  }
  if (tmpflag) {
    #include "msg/noredir.c"
    return 1;
  }

  /* Handle /UNLOAD */
  if (args.flags & ARGFL_UNLOAD) {
    unsigned char  serdfsid;
    unsigned short myseg, myoff, mydataseg;
    struct tsrshareddata far *tsrdata;
    unsigned char far *int2fptr;

    serdfsid = findfreemultiplex(&tmpflag);
    if (!tmpflag) {
      #include "msg/notload.c"
      return 1;
    }
    /* Verify we're at the top of INT 2Fh chain */
    _asm {
      push ax
      push bx
      push es
      mov  ax, 352Fh
      int  21h
      mov  myseg, es
      mov  myoff, bx
      pop  es
      pop  bx
      pop  ax
    }
    /* Scan first 32 bytes of inthandler for the 'SDFS' signature, since the
     * Watcom __interrupt __far prologue varies in size (~24 bytes). */
    {
      unsigned char far *base = (unsigned char far *)MK_FP(myseg, myoff);
      int sigoff = -1;
      int s;
      for (s = 0; s <= 28; s++) {
        if (base[s]=='S' && base[s+1]=='D' && base[s+2]=='F' && base[s+3]=='S') {
          sigoff = s;
          break;
        }
      }
      if (sigoff < 0) {
        #include "msg/othertsr.c"
        return 1;
      }
      int2fptr = base + sigoff;
    }
    /* Get TSR data pointer via multiplex AL=1 */
    _asm {
      push ax
      push bx
      push cx
      pushf
      mov  ah, serdfsid
      mov  al, 1
      mov  bx, SDF_MUXBX
      mov  myseg, 0FFFFh
      int  2Fh
      test ax, ax
      jnz  getptrfail
      mov  myseg, bx
      mov  myoff, cx
      getptrfail:
      popf
      pop  cx
      pop  bx
      pop  ax
    }
    if (myseg == 0xFFFFu) {
      #include "msg/tsrcomfa.c"
      return 1;
    }
    tsrdata   = MK_FP(myseg, myoff);
    mydataseg = myseg;

    /* Restore previous INT 2Fh handler */
    myseg = tsrdata->prev_2f_handler_seg;
    myoff = tsrdata->prev_2f_handler_off;
    _asm {
      push ax
      push ds
      push dx
      mov  ax, myseg
      push ax
      pop  ds
      mov  dx, myoff
      mov  ax, 252Fh
      int  21h
      pop  dx
      pop  ds
      pop  ax
    }

    /* Restore INT 28h if we hooked it */
    if (tsrdata->prev_int28h_seg != 0 || tsrdata->prev_int28h_off != 0) {
      myseg = tsrdata->prev_int28h_seg;
      myoff = tsrdata->prev_int28h_off;
      _asm {
        push ax
        push ds
        push dx
        mov  ax, myseg
        push ax
        pop  ds
        mov  dx, myoff
        mov  ax, 2528h
        int  21h
        pop  dx
        pop  ds
        pop  ax
      }
    }

    /* Restore INT 1Ch if we hooked it */
    if (tsrdata->prev_int1ch_seg != 0 || tsrdata->prev_int1ch_off != 0) {
      myseg = tsrdata->prev_int1ch_seg;
      myoff = tsrdata->prev_int1ch_off;
      _asm {
        push ax
        push ds
        push dx
        mov  ax, myseg
        push ax
        pop  ds
        mov  dx, myoff
        mov  ax, 251Ch
        int  21h
        pop  dx
        pop  ds
        pop  ax
      }
    }

    /* Mark all mapped drives as non-network in CDS */
    for (i = 0; i < 26; i++) {
      if (tsrdata->ldrv[i] == 0xFFu) continue;
      cds = getcds((unsigned int)i);
      if (cds != NULL) cds->flags = 0;
    }
    freeseg(mydataseg);
    freeseg(tsrdata->pspseg);

    if (!(args.flags & ARGFL_QUIET)) {
      #include "msg/unloaded.c"
    }
    return 0;
  }

  /* Save current INT 2Fh vector */
  _asm {
    mov  ax, 352Fh
    push es
    push bx
    int  21h
    mov  word ptr [glob_data + GLOB_DATOFF_PREV2FHANDLERSEG], es
    mov  word ptr [glob_data + GLOB_DATOFF_PREV2FHANDLEROFF], bx
    pop  bx
    pop  es
  }

  /* Check for existing SerialDFS instance */
  glob_multiplexid = findfreemultiplex(&tmpflag);
  if (tmpflag) {
    #include "msg/alrload.c"
    return 1;
  }
  if (!glob_multiplexid) {
    #include "msg/nomultpx.c"
    return 1;
  }

  /* Ensure target drive letters are available */
  for (i = 0; i < 26; i++) {
    if (glob_data.ldrv[i] == 0xFFu) continue;
    cds = getcds((unsigned int)i);
    if (cds == NULL) {
      #include "msg/mapfail.c"
      return 1;
    }
    if (cds->flags != 0) {
      #include "msg/drvactiv.c"
      return 1;
    }
  }

  /* Allocate permanent data segment and copy DGROUP into it, then
   * redirect DS to the new segment.  SS is left pointing at the
   * original transient stack — that is fine because the transient
   * code never returns through the C runtime (it calls INT 21h
   * AX=3100h to go resident), so the old stack is simply abandoned.
   * Only DS matters for the resident handler which uses its own
   * (interrupted-program's) SS:SP at runtime. */
  newdataseg = allocseg(DATASEGSZ);
  if (!newdataseg) {
    #include "msg/memfail.c"
    return 1;
  }
  _asm {
    cli
    push es
    push cx
    push si
    push di
    mov  cx, DATASEGSZ
    xor  si, si
    xor  di, di
    cld
    mov  es, newdataseg
    rep  movsb
    pop  di
    pop  si
    pop  cx
    pop  ax
    push es
    pop  ds
    push ax
    pop  es
    sti
  }

  /* Patch inthandler DS to the new segment */
  if (updatetsrds() != 0) {
    #include "msg/relfail.c"
    /* Cannot return through C runtime: DS now points to newdataseg.
     * Free the segment then exit directly to DOS. */
    freeseg(newdataseg);
    _asm { mov ax, 4C01h
           int 21h }
  }

  /* Hook INT 28h + INT 1Ch if /STATUS requested */
  if (args.flags & ARGFL_STATUS) {
    unsigned short s28seg = 0, s28off = 0, s1cseg = 0, s1coff = 0;
    _asm {
      push bx
      push es
      mov  ax, 3528h
      int  21h
      mov  s28seg, es
      mov  s28off, bx
      pop  es
      pop  bx
    }
    _asm {
      push bx
      push es
      mov  ax, 351Ch
      int  21h
      mov  s1cseg, es
      mov  s1coff, bx
      pop  es
      pop  bx
    }
    if (patchhandlerds((void far *)int28h_handler, 'S', 'D', '2', '8') == 0 &&
        patchhandlerds((void far *)int1ch_handler, 'S', 'D', '1', 'C') == 0) {
      glob_data.prev_int28h_seg = s28seg;
      glob_data.prev_int28h_off = s28off;
      glob_data.prev_int1ch_seg = s1cseg;
      glob_data.prev_int1ch_off = s1coff;
      _asm {
        cli
        mov  ax, 2528h
        push ds
        push dx
        push cs
        pop  ds
        mov  dx, offset int28h_handler
        int  21h
        pop  dx
        pop  ds
        sti
      }
      _asm {
        cli
        mov  ax, 251Ch
        push ds
        push dx
        push cs
        pop  ds
        mov  dx, offset int1ch_handler
        int  21h
        pop  dx
        pop  ds
        sti
      }
      stat_enabled = 1;
    }
  }

  /* Fetch SDA and InDOS pointer */
  glob_sdaptr = getsda();
  {
    unsigned short indos_seg = 0, indos_off = 0;
    _asm {
      mov  ah, 34h
      int  21h
      mov  indos_seg, es
      mov  indos_off, bx
    }
    glob_data.indos_seg = indos_seg;
    glob_data.indos_off = indos_off;
  }

  /* Store COM port and baud divisor, then initialise UART */
  glob_data.comport  = args.comport;
  glob_data.baudcode = args.baudiv;
  seruart_init(args.comport, args.baudiv);

  /* Mark mapped drives as network drives in CDS */
  for (i = 0; i < 26; i++) {
    if (glob_data.ldrv[i] == 0xFFu) continue;
    cds = getcds((unsigned int)i);
    cds->flags          = CDSFLAG_NET | CDSFLAG_PHY;
    cds->current_path[0]= (char)('A' + i);
    cds->current_path[1]= ':';
    cds->current_path[2]= '\\';
    cds->current_path[3]= 0;
  }

  /* Print install message */
  if (!(args.flags & ARGFL_QUIET)) {
    char buff[24];
    #include "msg/instlled.c"
    /* Print "COMn @ NNNNNNbaud)\r\n" */
    buff[0] = 'C'; buff[1] = 'O'; buff[2] = 'M';
    buff[3] = (char)('0' + args.comport);
    buff[4] = ' '; buff[5] = '@'; buff[6] = ' ';
    /* embed baud rate from divisor */
    {
      long baud;
      int  k = 7;
      char tmp[8];
      int  n = 0;
      if (args.baudiv == SERUART_DIV_9600)   baud = 9600L;
      else if (args.baudiv == SERUART_DIV_19200) baud = 19200L;
      else if (args.baudiv == SERUART_DIV_38400) baud = 38400L;
      else if (args.baudiv == SERUART_DIV_57600) baud = 57600L;
      else                                        baud = 115200L;
      do { tmp[n++] = (char)('0' + baud % 10); baud /= 10; } while (baud > 0);
      while (n-- > 0) buff[k++] = tmp[n];
      buff[k++] = 'b'; buff[k++] = 'a'; buff[k++] = 'u'; buff[k++] = 'd';
      buff[k++] = ')'; buff[k++] = '\r'; buff[k++] = '\n'; buff[k++] = '$';
    }
    outmsg(buff);
    /* Print drive mapping lines: " X: -> [C:]\r\n" */
    for (i = 0; i < 26; i++) {
      if (glob_data.ldrv[i] == 0xFFu) continue;
      buff[0] = ' '; buff[1] = (char)('A' + i); buff[2] = ':';
      buff[3] = ' '; buff[4] = '-'; buff[5] = '>';
      buff[6] = ' '; buff[7] = '['; buff[8] = (char)('A' + glob_data.ldrv[i]);
      buff[9] = ':'; buff[10] = ']';
      buff[11] = '\r'; buff[12] = '\n'; buff[13] = '$';
      outmsg(buff);
    }
  }

  /* Get PSP segment and free the environment */
  _asm {
    mov  ah, 62h
    int  21h
    mov  word ptr [glob_data + GLOB_DATOFF_PSPSEG], bx
  }
  _asm {
    mov  es, word ptr [glob_data + GLOB_DATOFF_PSPSEG]
    mov  es, es:[2Ch]
    mov  ah, 49h
    int  21h
  }

  /* Hook INT 2Fh */
  _asm {
    cli
    mov  ax, 252Fh
    push ds
    push dx
    push cs
    pop  ds
    mov  dx, offset inthandler
    int  21h
    pop  dx
    pop  ds
    sti
  }

  /* Go TSR — keep resident from PSP start through end of BEGTEXT */
  _asm {
    mov  ax, 3100h
    mov  dx, offset begtextend
    add  dx, 256
    add  dx, 15
    shr  dx, 1
    shr  dx, 1
    shr  dx, 1
    shr  dx, 1
    int  21h
  }

  return 0;
}
