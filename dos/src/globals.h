/*
 * serdfs/dos/src/globals.h
 * Adapted from vendor/etherdfs-baseline/etherdfs/trunk/globals.h
 * EtherDFS by Mateusz Viste, MIT licence. See COPYING and NOTICES.
 *
 * Changes from vendor:
 *   - Replaced pkthandle/pktint with comport/baudcode (serial identity).
 *   - Added indos_seg/indos_off for InDOS pointer (fetched at install via
 *     INT 21h AH=34h; used by Phase 10 status bar safe-render check).
 *   - Removed glob_pktdrv_recvbuff, glob_pktdrv_sndbuff, glob_pktdrv_pktcall
 *     (serial RX/TX handled by seruart/serrpc).
 *   - Removed GLOB_LMAC, GLOB_RMAC, all MAC/Ethernet references.
 *   - GLOB_DATOFF_* constants updated to match new tsrshareddata layout.
 *   - DATASEGSZ increased to 4096 to hold serial buffers + dir-batch cache.
 *   - Added dir-batch cache globals and req/reply payload buffers.
 */

#ifndef GLOBALS_SENTINEL
#define GLOBALS_SENTINEL

/* Required size (in bytes) of the data segment.
 * serrpc tx/rx bufs: 524*2=1048, CRC table: 512, dir_batch: 22*23=506,
 * req/reply bufs: 512*2=1024, stack headroom: ~800 bytes, misc: ~100 bytes.
 * Total worst case: ~4090 — round up to 4096. */
#define DATASEGSZ 4096

/* Debug VGA pointer — Phase 10 status bar borrows this pattern */
#if DEBUGLEVEL > 0
static unsigned short dbg_xpos = 0;
static unsigned short far *dbg_VGA = (unsigned short far *)(0xB8000000l);
static unsigned char dbg_hexc[16] = "0123456789ABCDEF";
#define dbg_startoffset 80*16
#endif

/* Offsets into tsrshareddata — must match struct layout exactly.
 * Assembly routines (updatetsrds) use these to patch the ISR prologue. */
#define GLOB_DATOFF_PREV2FHANDLERSEG  0
#define GLOB_DATOFF_PREV2FHANDLEROFF  2
#define GLOB_DATOFF_PSPSEG            4
#define GLOB_DATOFF_COMPORT           6   /* serial port I/O base (0x3F8/0x2F8) */
#define GLOB_DATOFF_BAUDCODE          8   /* baud divisor stored as unsigned short */
#define GLOB_DATOFF_INDOS_SEG        10   /* InDOS byte segment (Phase 10 status bar) */
#define GLOB_DATOFF_INDOS_OFF        12   /* InDOS byte offset */

static struct tsrshareddata {
/*offs*/
/*  0 */ unsigned short prev_2f_handler_seg; /* seg:off of the previous INT 2Fh handler */
/*  2 */ unsigned short prev_2f_handler_off;
/*  4 */ unsigned short pspseg;    /* segment of the program's PSP block */
/*  6 */ unsigned short comport;   /* SERUART_COM1=1 or SERUART_COM2=2 */
/*  8 */ unsigned short baudcode;  /* UART divisor latch value */
/* 10 */ unsigned short indos_seg; /* segment of DOS InDOS byte (from INT 21h AH=34h) */
/* 12 */ unsigned short indos_off; /* offset  of DOS InDOS byte */

         unsigned char ldrv[26]; /* local-to-remote drive mappings (0=A:, 1=B:, …) */
/* 40 */ unsigned short prev_int28h_seg; /* INT 28h prev handler seg (0 = not hooked) */
/* 42 */ unsigned short prev_int28h_off;
/* 44 */ unsigned short prev_int1ch_seg; /* INT 1Ch prev handler seg (0 = not hooked) */
/* 46 */ unsigned short prev_int1ch_off;
/* 48 */ unsigned long  stat_tx_bytes;   /* bytes sent to UART */
/* 52 */ unsigned long  stat_rx_bytes;   /* bytes received from UART */
/* 56 */ unsigned short stat_crc_errs;   /* CRC decode failures */
/* 58 */ unsigned short stat_timeouts;   /* receive timeouts */
/* 60 */ unsigned short stat_retries;    /* RPC retries (attempt > 0) */
} glob_data;

/* ── RPC payload buffers (reused across all RPC calls) ───────────────────── */
/* Resident — placed in the global segment so serrpc.c can reference them. */
static unsigned char glob_req_payload[512];
static unsigned char glob_rep_payload[512];

/* ── FINDFIRST / FINDNEXT dir-entry batch cache ──────────────────────────── */
#define DIR_ENTRY_SIZE  23
#define DIR_BATCH_MAX   22   /* floor(512 / 23) */
static unsigned char  dir_batch[DIR_BATCH_MAX * DIR_ENTRY_SIZE]; /* 506 bytes */
static unsigned char  dir_batch_count;   /* valid entries in dir_batch[]       */
static unsigned char  dir_batch_idx;     /* next entry to hand to caller       */
static unsigned char  dir_batch_more;    /* server said more entries available  */
static unsigned char  dir_hid;           /* open dir handle from LIST_DIR reply */

/* ── Status bar state (resident; /STATS transient doesn't need these) ────── */
static unsigned char stat_dirty;    /* 1 = bar needs refresh */
static unsigned char stat_enabled;  /* 1 = bar is active */

/* ── Misc globals used by inthandler() and process2f() ──────────────────── */
static unsigned char glob_reqdrv;       /* requested drive, set by inthandler  */
static unsigned short glob_reqstkword;  /* WORD saved from stack (SETATTR)     */
static struct sdastruct far *glob_sdaptr;  /* DOS SDA pointer, set at install  */

/* seg:off of the old (DOS) stack — saved during stack-switch in inthandler() */
static unsigned short glob_oldstack_seg;
static unsigned short glob_oldstack_off;

/* INT 2Fh multiplex ID registered by SerialDFS */
static unsigned char glob_multiplexid;

/* register snapshot captured when INT 2Fh fires */
static union INTPACK glob_intregs;

#endif
