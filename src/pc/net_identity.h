/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_NET_IDENTITY_H
#define PC_NET_IDENTITY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Connect codes: TAG#NUM, 1-4 letters, '#', 1-3 digits ("CHRS#123"). */
#define PC_CODE_TAG_MAX 4
#define PC_CODE_NUM_MAX 3
#define PC_CODE_LEN_MAX (PC_CODE_TAG_MAX + 1 + PC_CODE_NUM_MAX)
#define PC_CODE_SIZE 18 /* buffer size; also the wire size in MatchHello */
typedef struct PcNetIdentity {
    uint8_t secret_key[64];
    uint8_t public_key[32];
    char code[PC_CODE_SIZE]; /* this profile's TAG#NUM */
} PcNetIdentity;
bool pc_identity_random(void* bytes, size_t length);
/* Loads (or creates) the profile key. `code` is the connect code the player
 * chose; when it is not a valid code it is only a hint (an old player name),
 * and a default code is made from its letters and the key. */
bool pc_identity_load(PcNetIdentity* identity, const char* directory, const char* code);
/* A canonical code: upper case, no leading zeros in the number. */
bool pc_identity_code_valid(const char* code);
/* `text` (any case, spaces around the '#', leading zeros) as a canonical
 * code, or false if it is not exactly one code. */
bool pc_identity_normalize_code(const char* text, char out[PC_CODE_SIZE]);
/* Finds a connect code in free text -- a paste or a chat line -- and returns
 * it in canonical form. */
bool pc_identity_parse_code(const char* text, char out[PC_CODE_SIZE]);
void pc_identity_sign(
    const PcNetIdentity* identity, uint8_t signature[64], const void* message, size_t length);
bool pc_identity_verify(
    const uint8_t public_key[32], const uint8_t signature[64], const void* message, size_t length);
void pc_identity_clear(PcNetIdentity* identity);
#ifdef __cplusplus
}
#endif
#endif
