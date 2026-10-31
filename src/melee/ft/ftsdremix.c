#include "ftsdremix.h"

#ifdef TARGET_PC

#include <string.h>

#include "ftanim.h"
#include "inlines.h"
#include "types.h"
#include <melee/ft/kinds/ftCLink/ftclink.h>
#include <melee/ft/kinds/ftLink/ftlink.h>

/* SD Remix's FSM data (MCM_Mods/2_Balance.txt, "FSM Engine + SDR FSM
 * Data", by Magus): per character (external id), frame and action, a new
 * animation speed. Each entry is two words:
 *   [char:8][from frame:8][action:16]  speed (float bits; FFFFFFFF = leave)
 * action is a motion state id, or 0x8000 | an animation (subaction) id.
 * Sorted by character; within one, later "from" frames come first, and the
 * first entry that applies wins. */
static const u32 ftSdr_FsmData[] = {
    /* Captain Falcon */
    0x00080160, 0x3FC00000, 0x001400EB, 0x3FC00000,
    /* Donkey Kong */
    0x01190042, 0x3F800000, 0x01000042, 0x3FA00000,
    0x01180045, 0x3FC00000, 0x011800EB, 0x3FD00000,
    0x01000181, 0x40000000,
    /* Mr. Game & Watch */
    0x03090038, 0x3F800000, 0x03000038, 0x3FC00000,
    0x0318003F, 0x3F800000, 0x0301003F, 0xFFFFFFFF,
    0x0300003F, 0x3FC00000, 0x030A0042, 0x3F800000,
    0x03000042, 0x3FA00000, 0x031400E9, 0x3FF00000,
    0x031400EA, 0x3FF00000, 0x030C00EB, 0x3FE66666,
    0x03260177, 0x41300000, 0x0326017A, 0x41300000,
    /* Kirby */
    0x040A0042, 0x3F800000, 0x04000042, 0x3FD55555,
    0x04120045, 0x3F800000, 0x04000045, 0x3FE66666,
    0x041D00DC, 0x40000000, 0x043900DE, 0x402AAAAB,
    0x0416017F, 0x3F800000, 0x0400017F, 0x3FA5A5A6,
    0x042E0180, 0x40000000, 0x0400018C, 0x40000000,
    /* Bowser */
    0x05160044, 0x3F800000, 0x05000044, 0x3FB00000,
    0x052F00DE, 0x3FC00000, 0x051700E9, 0x40124925,
    0x051A00EA, 0x40500000, 0x051900EB, 0x40080000,
    0x05000157, 0x40000000, 0x0512015B, 0x3F9E1E1E,
    0x0510015B, 0x3F800000, 0x0500015B, 0x3FAAAAAB,
    0x05120161, 0x3F9E1E1E, 0x05100161, 0x3F800000,
    0x05000161, 0x3FAAAAAB, 0x0500016B, 0x3FC00000,
    0x05140045, 0x3ECCCCCD,
    /* Link */
    0x06100035, 0x3F800000, 0x06000035, 0x3FBA2E8C,
    0x060D0039, 0x3F800000, 0x06000039, 0x3FE00000,
    0x061E0042, 0x3F800000, 0x06100042, 0x3FE00000,
    0x061400EA, 0x3FC5D174, 0x061B015E, 0x3F800000,
    0x0600015E, 0x3FA2E8BA, 0x061B0161, 0x3F800000,
    0x06000161, 0x3FA2E8BA, 0x06000166, 0x3FA66666,
    0x06000167, 0x3FA66666,
    /* Mario */
    0x08160042, 0x3FF33333, 0x08120042, 0x3F800000,
    0x08000042, 0x3FADB6DB,
    /* Mewtwo */
    0x0A13003C, 0x3FC00000, 0x0A020042, 0x3F800000,
    0x0A010042, 0x3E800000, 0x0A0C0043, 0x3F800000,
    0x0A000043, 0x3FC00000, 0x0A150045, 0x3FC00000,
    0x0A1500EB, 0x402AAAAB, 0x0A000155, 0x3FD9999A,
    0x0A00015A, 0x3FD9999A, 0x0A27015F, 0x3FB33333,
    0x0A270160, 0x3FB33333, 0x0A0F0167, 0x3F800000,
    0x0A000167, 0x3FD55555, 0x0A0F0168, 0x3F800000,
    0x0A000168, 0x3FD55555,
    /* Ness */
    0x0B1D0045, 0x3FE00000, 0x0B140045, 0x3F800000,
    0x0B000045, 0x40000000, 0x0B0C0159, 0x3F800000,
    0x0B000159, 0x3FC00000, 0x0B140164, 0x3FC00000,
    0x0B000164, 0x3FB6DB6E, 0x0B140165, 0x3FC00000,
    0x0B000165, 0x3FB6DB6E, 0x0B00016F, 0x41200000,
    0x0B000174, 0x41200000,
    /* Pikachu */
    0x0D10003C, 0x3F800000, 0x0D01003C, 0xFFFFFFFF,
    0x0D00003C, 0x3FAAAAAB, 0x0D0E0045, 0x3F800000,
    0x0D000045, 0x3FB33333, 0x0D1400DE, 0x3FB8E38E,
    0x0D00016A, 0x3FB851EC, 0x0D00016E, 0x3FB851EC,
    /* Samus */
    0x1016003F, 0x3F4CCCCD,
    /* Yoshi */
    0x11130042, 0x3F800000, 0x11000042, 0x3FAAAAAB,
    0x111800D4, 0x3FC00000, 0x111200D4, 0x3F800000,
    0x110000D4, 0x3FD1745D, 0x110C00DD, 0x3FCCCCCD,
    0x1100016F, 0x3F99999A, 0x11000159, 0x3F400000,
    0x111700D6, 0x3F99999A, 0x11000155, 0x3F400000,
    /* Zelda */
    0x120C0033, 0x3F800000, 0x12000033, 0x40000000,
    0x120C0035, 0x3F800000, 0x12000035, 0x40000000,
    0x120C0037, 0x3F800000, 0x12000037, 0x40000000,
    0x12080038, 0x3F800000, 0x12000038, 0x3FEAAAAB,
    0x12030162, 0x40000000,
    /* Young Link */
    0x150E0039, 0x3F800000, 0x15000039, 0x3FE00000,
    0x150A003F, 0x3F800000, 0x1501003F, 0xFFFFFFFF,
    0x1500003F, 0x40000000, 0x150E0042, 0x3F800000,
    0x15000042, 0x40000000, 0x151400EA, 0x3FC5D174,
    0x1500015A, 0x3FAAAAAB, 0x1500015D, 0x3FAAAAAB,
    0x151B015E, 0x3F800000, 0x1500015E, 0x3FACCCCD,
    0x151B0161, 0x3F800000, 0x15000161, 0x3FACCCCD,
    /* Dr. Mario */
    0x16160042, 0x3FF33333,
    /* Roy */
    0x170D0045, 0x3F9C71C7,
    /* Pichu */
    0x1810003C, 0x3F800000, 0x1801003C, 0xFFFFFFFF,
    0x1800003C, 0x3FCCCCCD, 0x180A0042, 0x3F800000,
    0x18000042, 0x40000000, 0x180E0045, 0x3F800000,
    0x18000045, 0x3FE00000, 0x1800016A, 0x3FF8E38E,
    0x1800016E, 0x3FF8E38E, 0x180E00DE, 0x3FA00000,
    /* Ganondorf */
    0x191400EB, 0x40000000, 0x19080160, 0x3FC00000,
    0x19120038, 0x3ECCCCCD, 0x19030038, 0x3F400000,
    0x19010038, 0x3E000000,
    0x00000000, 0x00000000,
};

/* FighterKind -> CharacterKind, as the FSM data is keyed. */
static int ftSdr_ExternalId(FighterKind kind)
{
    static const s8 table[Ft_Kind_Emblem + 1] = {
        CKind_Mario, CKind_Fox,     CKind_Captain,  CKind_Donkey, CKind_Kirby,
        CKind_Koopa, CKind_Link,    CKind_Seak,     CKind_Ness,   CKind_Peach,
        CKind_PopoNana, CKind_PopoNana, CKind_Pikachu, CKind_Samus, CKind_Yoshi,
        CKind_Purin, CKind_Mewtwo,  CKind_Luigi,    CKind_Mars,   CKind_Zelda,
        CKind_CLink, CKind_DrMario, CKind_Falco,    CKind_Pichu,  CKind_GameWatch,
        CKind_Ganon, CKind_Emblem,
    };
    if ((unsigned) kind > Ft_Kind_Emblem) {
        return -1;
    }
    return table[kind];
}

void ftSdr_ApplyFsm(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int id, frame, i;
    u32 motion, anim;

    if (!FT_SDR) {
        return;
    }
    id = ftSdr_ExternalId(fp->kind);
    if (id < 0) {
        return;
    }
    frame = (int) fp->cur_anim_frame;
    motion = (u32) fp->motion_id;
    anim = (u32) fp->anim_id | 0x8000;
    for (i = 0; ftSdr_FsmData[i] != 0; i += 2) {
        u32 w = ftSdr_FsmData[i];
        int ch = (int) (w >> 24);
        if (ch != 0xFF) {
            if (ch > id) {
                return; /* past this character's entries */
            }
            if (ch != id) {
                continue;
            }
        }
        if ((int) ((w >> 16) & 0xFF) > frame) {
            continue;
        }
        if ((w & 0xFFFF) != motion && (w & 0xFFFF) != anim) {
            continue;
        }
        if (ftSdr_FsmData[i + 1] != 0xFFFFFFFF) {
            u32 bits = ftSdr_FsmData[i + 1];
            f32 speed;
            memcpy(&speed, &bits, sizeof speed);
            ftAnim_SetAnimRate(gobj, speed);
        }
        return;
    }
}

/* SD Remix's motion-table change ("Link/Young Link - Boomerang Hookshot
 * cancel", v1.00's super jump): their air grab states lose their flags. */
void ftSdr_PatchTables(void)
{
    if (!FT_SDR) {
        return;
    }
    ftLk_Init_MotionStateTable[ftLk_MS_AirCatch - ftCo_MS_Count].x4_flags = 0;
    ftLk_Init_MotionStateTable[ftLk_MS_AirCatchHit - ftCo_MS_Count].x4_flags =
        0;
    ftCl_Init_MotionStateTable[ftLk_MS_AirCatch - ftCo_MS_Count].x4_flags = 0;
    ftCl_Init_MotionStateTable[ftLk_MS_AirCatchHit - ftCo_MS_Count].x4_flags =
        0;
}

#endif
