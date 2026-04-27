/*
 * serdfs/dos/tools/serdel.c
 * SERDEL COM1|COM2 BAUD \REMOTE_FILE
 *
 * Deletes a file on the remote SerialDFS filesystem.
 * Phase 6 exerciser for CMD_DELETE.
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
    const char *remote_path;
    unsigned char reply_status;
    unsigned int replylen;
    unsigned int plen;
    int rc;

    if (argc < 4) {
        printf("Usage: SERDEL COM1|COM2 BAUD \\REMOTE_FILE\n");
        return 1;
    }

    port = parse_port(argv[1]);
    if (!port) { printf("Unknown port: %s\n", argv[1]); return 1; }

    div = seruart_baud_to_div(atol(argv[2]));
    if (!div) { printf("Unsupported baud: %s\n", argv[2]); return 1; }

    remote_path = argv[3];

    seruart_init(port, div);
    seruart_drain(port);

    /* DELETE payload: drive(1) + path(NUL) */
    reqbuf[0] = 'X';
    strncpy((char *)(reqbuf + 1), remote_path, FRAME_MAX_PAYLOAD - 3);
    plen = 1u + (unsigned int)strlen(remote_path) + 1u;
    reqbuf[plen - 1] = '\0';

    rc = serial_rpc(port, CMD_DELETE,
                    reqbuf, plen,
                    replybuf, &replylen, &reply_status);

    if (rc != SERRPC_OK) {
        printf("SERDEL: transport timeout\n");
        return 1;
    }
    switch (reply_status) {
        case STATUS_OK:
            printf("Deleted: %s\n", remote_path);
            return 0;
        case ERR_NOT_FOUND:
            printf("SERDEL: file not found: %s\n", remote_path);
            return 1;
        case ERR_ACCESS:
            printf("SERDEL: access denied: %s\n", remote_path);
            return 1;
        default:
            printf("SERDEL: server error 0x%02X\n", (unsigned)reply_status);
            return 1;
    }
}
