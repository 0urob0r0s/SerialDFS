/*
 * serdfs/dos/tools/crcfuzz.c
 * CRCFUZZ.EXE — frame codec conformance tester.
 * Loads golden vector .BIN files from C:\SERDFS\PROTOCOL\VECTORS\
 * and asserts expected frame_decode outcomes.
 *
 * Pass: prints "Results: N passed, 0 failed", exits 0.
 * Fail: prints failing checks, exits 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/serframe.h"

#define VEC_DIR   "C:\\SERDFS\\PROTOCOL\\VECTORS\\"
#define BUF_SIZE  (FRAME_MAX_SIZE + 8u)

static int pass_count = 0;
static int fail_count = 0;

static void chk_int(const char *desc, int expected, int actual) {
    if (expected == actual) {
        printf("  PASS: %s\n", desc);
        pass_count++;
    } else {
        printf("  FAIL: %s  (want %d, got %d)\n", desc, expected, actual);
        fail_count++;
    }
}

static int load_vec(const char *name, unsigned char *buf, unsigned int *outlen) {
    char path[80];
    FILE *f;
    int n;
    strcpy(path, VEC_DIR);
    strcat(path, name);
    f = fopen(path, "rb");
    if (!f) {
        printf("  ERR: cannot open %s\n", path);
        fail_count++;
        return 0;
    }
    n = (int)fread(buf, 1u, BUF_SIZE, f);
    fclose(f);
    if (n <= 0) {
        printf("  ERR: empty or unreadable: %s\n", name);
        fail_count++;
        return 0;
    }
    *outlen = (unsigned int)n;
    return 1;
}

int main(void) {
    unsigned char buf[BUF_SIZE];
    unsigned int len;
    FrameHdr hdr;
    const unsigned char *payload;
    unsigned int plen;
    int rc;

    printf("=== CRCFUZZ: frame codec conformance ===\n");

    /* V1: PNGREQ.BIN — valid PING request (seq=1) */
    if (load_vec("PNGREQ.BIN", buf, &len)) {
        rc = frame_decode(buf, len, &hdr, &payload, &plen);
        chk_int("PNGREQ: FRAME_OK",     FRAME_OK,    rc);
        if (rc == FRAME_OK) {
            chk_int("PNGREQ: cmd=PING", (int)CMD_PING, (int)hdr.command);
            chk_int("PNGREQ: seq=1",    1,             (int)hdr.seq);
            chk_int("PNGREQ: plen=0",   0,             (int)plen);
        }
    }

    /* V2: PNGRESP.BIN — valid PING response (seq=1, status=OK) */
    if (load_vec("PNGRESP.BIN", buf, &len)) {
        rc = frame_decode(buf, len, &hdr, &payload, &plen);
        chk_int("PNGRESP: FRAME_OK",       FRAME_OK,    rc);
        if (rc == FRAME_OK) {
            chk_int("PNGRESP: cmd=PING",   (int)CMD_PING,   (int)hdr.command);
            chk_int("PNGRESP: seq=1",      1,               (int)hdr.seq);
            chk_int("PNGRESP: status=OK",  (int)STATUS_OK,  (int)hdr.status);
        }
    }

    /* V3: BADCRC.BIN — CRC XOR'd 0xFFFF, must be rejected */
    if (load_vec("BADCRC.BIN", buf, &len)) {
        rc = frame_decode(buf, len, &hdr, &payload, &plen);
        chk_int("BADCRC: FRAME_BADCRC",  FRAME_BADCRC, rc);
    }

    /* V4: TRUNC.BIN — only 5 bytes, must signal short */
    if (load_vec("TRUNC.BIN", buf, &len)) {
        rc = frame_decode(buf, len, &hdr, &payload, &plen);
        chk_int("TRUNC: FRAME_SHORT",    FRAME_SHORT,  rc);
    }

    /* V5: STALE.BIN — valid frame but seq=2; codec passes, RPC layer drops */
    if (load_vec("STALE.BIN", buf, &len)) {
        rc = frame_decode(buf, len, &hdr, &payload, &plen);
        chk_int("STALE: FRAME_OK",       FRAME_OK,     rc);
        if (rc == FRAME_OK)
            chk_int("STALE: seq=2",      2,            (int)hdr.seq);
    }

    printf("\nResults: %d passed, %d failed\n", pass_count, fail_count);
    return (fail_count == 0) ? 0 : 1;
}
