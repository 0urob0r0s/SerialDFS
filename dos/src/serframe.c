/*
 * serdfs/dos/src/serframe.c
 * Serial frame codec — CRC-16/CCITT-FALSE, 10-byte header.
 * See protocol/spec.txt for the wire format.
 */
#include "serframe.h"

/* CRC-16/CCITT-FALSE table (poly=0x1021, init=0xFFFF, no reflect) */
static unsigned int crc_table[256];
static int          crc_ready = 0;

static void crc_init(void) {
    unsigned int i, j, v;
    for (i = 0; i < 256u; i++) {
        v = i << 8;
        for (j = 0; j < 8u; j++) {
            if (v & 0x8000u)
                v = (v << 1) ^ 0x1021u;
            else
                v <<= 1;
            v &= 0xFFFFu;
        }
        crc_table[i] = v;
    }
    crc_ready = 1;
}

unsigned int crc16_update(unsigned int crc, unsigned char b) {
    if (!crc_ready) crc_init();
    return (crc_table[((crc >> 8) ^ b) & 0xFFu] ^ (crc << 8)) & 0xFFFFu;
}

unsigned int crc16_block(const unsigned char *buf, unsigned int len) {
    unsigned int crc = 0xFFFFu;
    unsigned int i;
    if (!crc_ready) crc_init();
    for (i = 0; i < len; i++)
        crc = (crc_table[((crc >> 8) ^ buf[i]) & 0xFFu] ^ (crc << 8)) & 0xFFFFu;
    return crc;
}

unsigned int frame_encode(unsigned char cmd, unsigned char seq,
                          unsigned char flags, unsigned char status,
                          const unsigned char *payload, unsigned int plen,
                          unsigned char *out) {
    unsigned int i, crc;
    if (plen > FRAME_MAX_PAYLOAD) return 0u;

    out[0] = FRAME_MAGIC_0;
    out[1] = FRAME_MAGIC_1;
    out[2] = FRAME_VERSION;
    out[3] = flags;
    out[4] = seq;
    out[5] = cmd;
    out[6] = status;
    out[7] = 0x00u;
    out[8] = (unsigned char)(plen & 0xFFu);
    out[9] = (unsigned char)((plen >> 8) & 0xFFu);
    for (i = 0; i < plen; i++)
        out[10u + i] = payload[i];

    crc = crc16_block(out, FRAME_HDR_SIZE + plen);
    out[FRAME_HDR_SIZE + plen]      = (unsigned char)(crc & 0xFFu);
    out[FRAME_HDR_SIZE + plen + 1u] = (unsigned char)((crc >> 8) & 0xFFu);

    return FRAME_HDR_SIZE + plen + FRAME_CRC_SIZE;
}

int frame_decode(const unsigned char *buf, unsigned int len,
                 FrameHdr *hdr,
                 const unsigned char **payload_out,
                 unsigned int *plen_out) {
    unsigned int plen, crc_wire, crc_calc;

    if (len < FRAME_HDR_SIZE) return FRAME_SHORT;
    if (buf[0] != FRAME_MAGIC_0 || buf[1] != FRAME_MAGIC_1) return FRAME_BADMAGIC;
    if (buf[2] != FRAME_VERSION) return FRAME_BADVER;

    plen = (unsigned int)buf[8] | ((unsigned int)buf[9] << 8);
    if (plen > FRAME_MAX_PAYLOAD) return FRAME_TOOLONG;
    if (len < FRAME_HDR_SIZE + plen + FRAME_CRC_SIZE) return FRAME_SHORT;

    crc_wire = (unsigned int)buf[FRAME_HDR_SIZE + plen] |
               ((unsigned int)buf[FRAME_HDR_SIZE + plen + 1u] << 8);
    crc_calc = crc16_block(buf, FRAME_HDR_SIZE + plen);
    if (crc_wire != crc_calc) return FRAME_BADCRC;

    hdr->version     = buf[2];
    hdr->flags       = buf[3];
    hdr->seq         = buf[4];
    hdr->command     = buf[5];
    hdr->status      = buf[6];
    hdr->payload_len = plen;

    *payload_out = buf + FRAME_HDR_SIZE;
    *plen_out    = plen;
    return FRAME_OK;
}
