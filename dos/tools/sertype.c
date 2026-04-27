/*
 * serdfs/dos/tools/sertype.c
 * SERTYPE COM1|COM2 9600|... FILENAME
 *
 * Opens, reads (512-byte chunks), and closes a file on the SerialDFS daemon.
 * Prints file content to stdout.
 * Phase 5 non-resident exerciser.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/seruart.h"
#include "../src/serframe.h"
#include "../src/serrpc.h"

static unsigned char replybuf[FRAME_MAX_PAYLOAD];
static unsigned char reqbuf[FRAME_MAX_PAYLOAD];

static int parse_port(const char *s) {
    const char *p = s;
    if ((p[0]=='C'||p[0]=='c') && (p[1]=='O'||p[1]=='o') &&
        (p[2]=='M'||p[2]=='m'))
        p += 3;
    if (p[0]=='1' && !p[1]) return SERUART_COM1;
    if (p[0]=='2' && !p[1]) return SERUART_COM2;
    return 0;
}

int main(int argc, char *argv[]) {
    int port;
    unsigned int div;
    const char *filename;
    unsigned char reply_status;
    unsigned int replylen;
    unsigned char hid;
    unsigned long total;
    int rc;

    if (argc < 4) {
        printf("Usage: SERTYPE COM1|COM2 BAUD FILENAME\n");
        return 1;
    }

    port = parse_port(argv[1]);
    if (!port) { printf("Unknown port: %s\n", argv[1]); return 1; }

    div = seruart_baud_to_div(atol(argv[2]));
    if (!div) { printf("Unsupported baud: %s\n", argv[2]); return 1; }

    filename = argv[3];

    seruart_init(port, div);
    seruart_drain(port);

    /* ── OPEN ──────────────────────────────────────────────────────────── */
    reqbuf[0] = 0;  /* mode = read-only */
    {
        unsigned int flen = (unsigned int)strlen(filename);
        /* Prepend backslash if not already present */
        if (filename[0] != '\\' && filename[0] != '/') {
            reqbuf[1] = '\\';
            strncpy((char *)(reqbuf + 2), filename, FRAME_MAX_PAYLOAD - 4);
            reqbuf[2 + flen] = '\0';
            flen += 1;  /* for the prepended backslash */
        } else {
            strncpy((char *)(reqbuf + 1), filename, FRAME_MAX_PAYLOAD - 3);
            reqbuf[1 + flen] = '\0';
        }
        rc = serial_rpc(port, CMD_OPEN,
                        reqbuf, 1u + flen + 1u,
                        replybuf, &replylen, &reply_status);
    }

    if (rc != SERRPC_OK) {
        printf("SERTYPE: transport timeout (OPEN)\n");
        return 1;
    }
    if (reply_status == ERR_NOT_FOUND) {
        printf("SERTYPE: file not found: %s\n", filename);
        return 1;
    }
    if (reply_status != STATUS_OK) {
        printf("SERTYPE: server error 0x%02X (OPEN)\n", (unsigned)reply_status);
        return 1;
    }
    if (replylen < 1) {
        printf("SERTYPE: missing handle in OPEN reply\n");
        return 1;
    }
    hid = replybuf[0];
    total = 0;

    /* ── READ loop ─────────────────────────────────────────────────────── */
    for (;;) {
        /* READ payload: uint8 hid + uint16le count */
        reqbuf[0] = hid;
        reqbuf[1] = 0u;    /* low byte of 512 */
        reqbuf[2] = 2u;    /* high byte of 512 */

        rc = serial_rpc(port, CMD_READ,
                        reqbuf, 3u,
                        replybuf, &replylen, &reply_status);

        if (rc != SERRPC_OK) {
            printf("\nSERTYPE: transport timeout (READ)\n");
            /* Best-effort close */
            reqbuf[0] = hid;
            serial_rpc(port, CMD_CLOSE, reqbuf, 1u,
                       replybuf, &replylen, &reply_status);
            return 1;
        }

        if (reply_status != STATUS_OK) {
            printf("\nSERTYPE: server error 0x%02X (READ)\n",
                   (unsigned)reply_status);
            reqbuf[0] = hid;
            serial_rpc(port, CMD_CLOSE, reqbuf, 1u,
                       replybuf, &replylen, &reply_status);
            return 1;
        }

        if (replylen == 0) break;  /* EOF */

        fwrite(replybuf, 1, replylen, stdout);
        total += replylen;
    }

    /* ── CLOSE ─────────────────────────────────────────────────────────── */
    reqbuf[0] = hid;
    rc = serial_rpc(port, CMD_CLOSE,
                    reqbuf, 1u,
                    replybuf, &replylen, &reply_status);

    if (rc != SERRPC_OK || reply_status != STATUS_OK) {
        fprintf(stderr, "SERTYPE: warning — CLOSE failed\n");
    }

    fprintf(stderr, "\n[%lu bytes]\n", total);
    return 0;
}
