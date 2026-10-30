#include "grstageexp.h"

#ifdef TARGET_PC

#include <stdlib.h>

#include "grdatfiles.h"
#include "types.h"
#include <sysdolphin/baselib/archive.h>

#include "grstageexp_tables.h"

bool grStageExp_On(void)
{
    static int on = -1;
    if (on < 0) {
        const char* v = getenv("MELEE_STAGE_EXPANSION");
        on = !(v != NULL && v[0] == '0');
    }
    return on != 0;
}

bool grStageExp_RCruiseOn(void)
{
    static int on = -1;
    if (on < 0) {
        const char* v = getenv("MELEE_STAGE_EXPANSION_RCRUISE");
        on = v != NULL && v[0] == '1';
    }
    return on != 0 && grStageExp_On();
}

/* The file's data is big-endian, as on the GameCube. */
static void store_be32(u8* p, u32 v)
{
    p[0] = (u8) (v >> 24);
    p[1] = (u8) (v >> 16);
    p[2] = (u8) (v >> 8);
    p[3] = (u8) v;
}

void grStageExp_PatchStageFile(StKind stkind)
{
    const u32* e;
    UnkArchiveStruct* arc;
    u8* file; /* the .dat file's first byte: its 0x20-byte header */

    if (!grStageExp_On() || (unsigned) stkind > 32 ||
        (e = grSe_Lists[stkind]) == NULL)
    {
        return;
    }
    if (stkind == St_Kind_RCruise && !grStageExp_RCruiseOn()) {
        return;
    }
    arc = grDatFiles_GetArchive();
    if (arc == NULL || arc->unk0 == NULL || arc->unk0->data == NULL) {
        return;
    }
    file = arc->unk0->data - 0x20;

    /* As the Gecko code: a word whose low byte is 0xFF ends the list. */
    while ((e[0] & 0xFF) != 0xFF) {
        u32 tag = e[0] >> 24;
        u32 off = e[0] & 0x00FFFFFF;
        if (tag == 0xCD) {
            /* (offset, length, value): fill length bytes with value */
            u32 len = e[1];
            while (len != 0) {
                len -= 4;
                store_be32(file + off + len, e[2]);
            }
            e += 3;
        } else if (tag == 0xCC) {
            /* (offset, target): a pointer to the file's data + target */
            store_be32(file + off, pc_encode_dp(file + 0x20 + e[1]));
            e += 2;
        } else {
            store_be32(file + off, e[1]);
            e += 2;
        }
    }
}

#endif
