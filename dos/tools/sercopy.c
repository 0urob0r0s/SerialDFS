/*
 * serdfs/dos/tools/sercopy.c
 * SERCOPY COM1|COM2 BAUD LOCAL_SRC \REMOTE_DST
 *
 * Copies a local DOS file to the remote SerialDFS filesystem.
 * Uses CREATE (mode=1, truncate) + WRITE (512-byte chunks) + CLOSE.
 * Phase 6 exerciser for write operations.
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
    const char *src_path;
    const char *dst_path;
    FILE *fp;
    unsigned char reply_status;
    unsigned int replylen;
    unsigned char hid;
    unsigned long total;
    size_t nread;
    int rc;

    if (argc < 5) {
        printf("Usage: SERCOPY COM1|COM2 BAUD SRC_LOCAL \\DST_REMOTE\n");
        return 1;
    }

    port = parse_port(argv[1]);
    if (!port) { printf("Unknown port: %s\n", argv[1]); return 1; }

    div = seruart_baud_to_div(atol(argv[2]));
    if (!div) { printf("Unsupported baud: %s\n", argv[2]); return 1; }

    src_path = argv[3];
    dst_path = argv[4];

    fp = fopen(src_path, "rb");
    if (!fp) { printf("SERCOPY: cannot open source: %s\n", src_path); return 1; }

    seruart_init(port, div);
    seruart_drain(port);

    /* ── CREATE (truncate if exists, mode=1) ──────────────────────────────── */
    reqbuf[0] = 1;  /* mode: truncate */
    {
        unsigned int dlen = (unsigned int)strlen(dst_path);
        if (dst_path[0] != '\\' && dst_path[0] != '/') {
            reqbuf[1] = '\\';
            strncpy((char *)(reqbuf + 2), dst_path, FRAME_MAX_PAYLOAD - 4);
            reqbuf[2 + dlen] = '\0';
            dlen += 1;
        } else {
            strncpy((char *)(reqbuf + 1), dst_path, FRAME_MAX_PAYLOAD - 3);
            reqbuf[1 + dlen] = '\0';
        }
        rc = serial_rpc(port, CMD_CREATE,
                        reqbuf, 1u + dlen + 1u,
                        replybuf, &replylen, &reply_status);
    }
    if (rc != SERRPC_OK) {
        printf("SERCOPY: transport timeout (CREATE)\n");
        fclose(fp);
        return 1;
    }
    if (reply_status != STATUS_OK) {
        printf("SERCOPY: CREATE error 0x%02X\n", (unsigned)reply_status);
        fclose(fp);
        return 1;
    }
    if (replylen < 1) {
        printf("SERCOPY: missing handle in CREATE reply\n");
        fclose(fp);
        return 1;
    }
    hid   = replybuf[0];
    total = 0;

    /* ── WRITE loop (512-byte chunks) ─────────────────────────────────────── */
    for (;;) {
        reqbuf[0] = hid;
        nread = fread(reqbuf + 1, 1, 511, fp);  /* 511 = FRAME_MAX_PAYLOAD - 1 hid byte */
        if (nread == 0) break;

        rc = serial_rpc(port, CMD_WRITE,
                        reqbuf, 1u + (unsigned int)nread,
                        replybuf, &replylen, &reply_status);
        if (rc != SERRPC_OK) {
            printf("\nSERCOPY: transport timeout (WRITE)\n");
            fclose(fp);
            return 1;
        }
        if (reply_status != STATUS_OK) {
            printf("\nSERCOPY: WRITE error 0x%02X\n", (unsigned)reply_status);
            fclose(fp);
            return 1;
        }
        total += (unsigned long)nread;
        printf(".");
    }
    fclose(fp);

    /* ── CLOSE ────────────────────────────────────────────────────────────── */
    reqbuf[0] = hid;
    rc = serial_rpc(port, CMD_CLOSE,
                    reqbuf, 1u,
                    replybuf, &replylen, &reply_status);
    if (rc != SERRPC_OK || reply_status != STATUS_OK) {
        printf("\nSERCOPY: warning — CLOSE failed\n");
        return 1;
    }

    printf("\n%lu bytes copied to %s\n", total, dst_path);
    return 0;
}
