/*
 * serdfs/dos/tools/serdir.c
 * SERDIR COM1|COM2 9600|... [\PATH]
 *
 * Lists a directory on the SerialDFS daemon.
 * Sends CMD_LIST_DIR; loops CMD_LIST_DIR_NEXT until all entries received.
 * Phase 5 non-resident exerciser.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/seruart.h"
#include "../src/serframe.h"
#include "../src/serrpc.h"

/* Directory entry layout (23 bytes, spec §5.5) */
#define DIR_ENTRY_SIZE  23
#define NAME_LEN        12
#define ATTR_OFF        12
#define DATE_OFF        13
#define TIME_OFF        15
#define SIZE_OFF        17
#define MORE_OFF        21

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

static unsigned long get_le32(const unsigned char *p) {
    return (unsigned long)p[0]
         | ((unsigned long)p[1] << 8)
         | ((unsigned long)p[2] << 16)
         | ((unsigned long)p[3] << 24);
}

static unsigned get_le16(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static void print_entry(const unsigned char *e) {
    char name[NAME_LEN + 1];
    unsigned char attr;
    unsigned dos_date, dos_time;
    unsigned long size;
    int yr, mo, dy, hr, mn;

    memcpy(name, e, NAME_LEN);
    name[NAME_LEN] = '\0';
    attr     = e[ATTR_OFF];
    dos_date = get_le16(e + DATE_OFF);
    dos_time = get_le16(e + TIME_OFF);
    size     = get_le32(e + SIZE_OFF);

    yr = ((dos_date >> 9) & 0x7F) + 1980;
    mo = (dos_date >> 5) & 0x0F;
    dy = dos_date & 0x1F;
    hr = (dos_time >> 11) & 0x1F;
    mn = (dos_time >> 5)  & 0x3F;

    if (attr & 0x10) {
        printf("%-12s  <DIR>             %04d-%02d-%02d %02d:%02d\n",
               name, yr, mo, dy, hr, mn);
    } else {
        printf("%-12s  %10lu  %04d-%02d-%02d %02d:%02d\n",
               name, size, yr, mo, dy, hr, mn);
    }
}

int main(int argc, char *argv[]) {
    int port;
    unsigned int div;
    const char *dos_path;
    unsigned char reply_status;
    unsigned int replylen;
    unsigned char hid;
    unsigned int entries;
    int rc;

    if (argc < 3) {
        printf("Usage: SERDIR COM1|COM2 BAUD [\\PATH]\n");
        return 1;
    }

    port = parse_port(argv[1]);
    if (!port) { printf("Unknown port: %s\n", argv[1]); return 1; }

    div = seruart_baud_to_div(atol(argv[2]));
    if (!div) { printf("Unsupported baud: %s\n", argv[2]); return 1; }

    dos_path = (argc >= 4) ? argv[3] : "\\";

    seruart_init(port, div);
    seruart_drain(port);

    /* Build LIST_DIR payload: drive letter + path + NUL */
    reqbuf[0] = 'X';
    strncpy((char *)(reqbuf + 1), dos_path, FRAME_MAX_PAYLOAD - 2);
    reqbuf[FRAME_MAX_PAYLOAD - 1] = '\0';
    {
        unsigned int pathlen = (unsigned int)strlen(dos_path);
        if (pathlen > FRAME_MAX_PAYLOAD - 3) pathlen = FRAME_MAX_PAYLOAD - 3;
        reqbuf[1 + pathlen] = '\0';  /* NUL terminator */
        rc = serial_rpc(port, CMD_LIST_DIR,
                        reqbuf, 1u + pathlen + 1u,
                        replybuf, &replylen, &reply_status);
    }

    if (rc != SERRPC_OK) {
        printf("SERDIR: transport timeout\n");
        return 1;
    }
    if (reply_status == ERR_NOT_FOUND) {
        printf("SERDIR: path not found: %s\n", dos_path);
        return 1;
    }
    if (reply_status != STATUS_OK) {
        printf("SERDIR: server error 0x%02X\n", (unsigned)reply_status);
        return 1;
    }

    /* First byte of reply is the handle ID */
    if (replylen < 1) {
        printf("SERDIR: empty reply\n");
        return 1;
    }
    hid = replybuf[0];
    entries = 0;

    /* Print first batch (reply[1..]) */
    {
        unsigned int off;
        for (off = 1; off + DIR_ENTRY_SIZE <= replylen; off += DIR_ENTRY_SIZE) {
            print_entry(replybuf + off);
            entries++;
        }
    }

    /* Check if more entries remain (more flag on last entry of batch) */
    while (replylen >= 1u + DIR_ENTRY_SIZE) {
        unsigned char last_more;
        unsigned int last_off = replylen - DIR_ENTRY_SIZE;
        /* For first batch, last entry is at (replylen-DIR_ENTRY_SIZE) from reply[1] */
        /* But we've already checked; re-read the 'more' flag */
        if (replylen == 1u) break;  /* no entries */
        last_off = 1u + ((replylen - 1u) / DIR_ENTRY_SIZE - 1u) * DIR_ENTRY_SIZE;
        last_more = replybuf[last_off + MORE_OFF];
        if (!last_more) break;

        /* More batches: send LIST_DIR_NEXT */
        rc = serial_rpc(port, CMD_LIST_DIR_NEXT,
                        &hid, 1u,
                        replybuf, &replylen, &reply_status);
        if (rc != SERRPC_OK || reply_status != STATUS_OK) break;

        {
            unsigned int off;
            for (off = 0; off + DIR_ENTRY_SIZE <= replylen; off += DIR_ENTRY_SIZE) {
                print_entry(replybuf + off);
                entries++;
            }
        }
        if (replylen == 0) break;
    }

    printf("\n  %u entry(s)\n", entries);
    return 0;
}
