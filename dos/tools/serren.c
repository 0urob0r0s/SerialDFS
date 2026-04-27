/*
 * serdfs/dos/tools/serren.c
 * SERREN COM1|COM2 BAUD \OLD_PATH \NEW_PATH
 *
 * Renames (or moves) a file or directory on the remote SerialDFS filesystem.
 * Phase 6 exerciser for CMD_RENAME.
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
    const char *old_path;
    const char *new_path;
    unsigned char reply_status;
    unsigned int replylen;
    unsigned int off, olen, nlen;
    int rc;

    if (argc < 5) {
        printf("Usage: SERREN COM1|COM2 BAUD \\OLD_PATH \\NEW_PATH\n");
        return 1;
    }

    port = parse_port(argv[1]);
    if (!port) { printf("Unknown port: %s\n", argv[1]); return 1; }

    div = seruart_baud_to_div(atol(argv[2]));
    if (!div) { printf("Unsupported baud: %s\n", argv[2]); return 1; }

    old_path = argv[3];
    new_path = argv[4];

    olen = (unsigned int)strlen(old_path);
    nlen = (unsigned int)strlen(new_path);

    /* RENAME payload: old_path(NUL) + new_path(NUL) */
    if (olen + nlen + 2u > FRAME_MAX_PAYLOAD) {
        printf("SERREN: path too long\n");
        return 1;
    }

    off = 0;
    memcpy(reqbuf + off, old_path, olen); off += olen;
    reqbuf[off++] = '\0';
    memcpy(reqbuf + off, new_path, nlen); off += nlen;
    reqbuf[off++] = '\0';

    seruart_init(port, div);
    seruart_drain(port);

    rc = serial_rpc(port, CMD_RENAME,
                    reqbuf, off,
                    replybuf, &replylen, &reply_status);

    if (rc != SERRPC_OK) {
        printf("SERREN: transport timeout\n");
        return 1;
    }
    switch (reply_status) {
        case STATUS_OK:
            printf("Renamed: %s -> %s\n", old_path, new_path);
            return 0;
        case ERR_NOT_FOUND:
            printf("SERREN: source not found: %s\n", old_path);
            return 1;
        case ERR_EXISTS:
            printf("SERREN: destination already exists: %s\n", new_path);
            return 1;
        case ERR_ACCESS:
            printf("SERREN: access denied\n");
            return 1;
        default:
            printf("SERREN: server error 0x%02X\n", (unsigned)reply_status);
            return 1;
    }
}
