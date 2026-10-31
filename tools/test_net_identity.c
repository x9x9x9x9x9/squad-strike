#include "pc/net_identity.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/melee-identity-XXXXXX";
    assert(mkdtemp(path));
    PcNetIdentity a, b;
    unsigned char sig[64];
    const unsigned char message[] = "meleepc pairing transcript";
    /* A chosen code is used as given, in canonical form. */
    assert(pc_identity_load(&a, path, "chrs#007"));
    assert(strcmp(a.code, "CHRS#7") == 0);
    assert(pc_identity_load(&b, path, "CHRS # 123"));
    assert(strcmp(b.code, "CHRS#123") == 0);
    assert(memcmp(a.public_key, b.public_key, 32) == 0);
    /* Anything else is a hint: up to four of its letters and a number from
     * the key, the same every time for the same key. */
    assert(pc_identity_load(&a, path, "alice"));
    assert(pc_identity_load(&b, path, "ALICE"));
    assert(strcmp(a.code, b.code) == 0);
    assert(strncmp(a.code, "ALIC#", 5) == 0 && pc_identity_code_valid(a.code));
    assert(pc_identity_load(&b, path, ""));
    assert(strncmp(b.code, "PLYR#", 5) == 0 && pc_identity_code_valid(b.code));
    assert(pc_identity_load(&b, path, NULL));
    assert(strncmp(b.code, "PLYR#", 5) == 0);

    /* The format: 1-4 letters, '#', 1-3 digits, canonical. */
    assert(pc_identity_code_valid("A#0") && pc_identity_code_valid("CHRS#999"));
    assert(!pc_identity_code_valid("CHRSX#1") && !pc_identity_code_valid("CHRS#1000"));
    assert(!pc_identity_code_valid("chrs#1") && !pc_identity_code_valid("CHRS#01"));
    assert(!pc_identity_code_valid("#12") && !pc_identity_code_valid("AB#") &&
           !pc_identity_code_valid("A1#2") && !pc_identity_code_valid("SIAN#K3XQ2M7A"));
    char code[PC_CODE_SIZE];
    assert(pc_identity_normalize_code(" fox#0 ", code) && !strcmp(code, "FOX#0"));
    assert(!pc_identity_normalize_code("fox#12a", code));
    assert(!pc_identity_normalize_code("fox", code));
    /* Found in free text, but not cut out of something longer. */
    assert(pc_identity_parse_code("my code is chrs#123!", code) && !strcmp(code, "CHRS#123"));
    assert(pc_identity_parse_code("add me: FOX # 42", code) && !strcmp(code, "FOX#42"));
    assert(pc_identity_parse_code("#1 then BOB#2", code) && !strcmp(code, "BOB#2"));
    assert(!pc_identity_parse_code("TOOLONG#1", code));
    assert(!pc_identity_parse_code("FOX#1234", code));
    assert(!pc_identity_parse_code("SIAN#K3XQ2M7A", code));
    assert(!pc_identity_parse_code("hello there", code));
    pc_identity_sign(&a, sig, message, sizeof message);
    assert(pc_identity_verify(a.public_key, sig, message, sizeof message));
    sig[0] ^= 1;
    assert(!pc_identity_verify(a.public_key, sig, message, sizeof message));
    char file[512];
    snprintf(file, sizeof file, "%s/identity.key", path);
    /* A 32-byte key is the identity a rating history belongs to: reloading
     * must return the same one, byte for byte. */
    PcNetIdentity reloaded;
    assert(pc_identity_load(&reloaded, path, "alice"));
    assert(memcmp(reloaded.public_key, a.public_key, 32) == 0);
    /* A file that cannot hold a key (wrong length) is replaced instead of
     * stranding the profile on "identity unavailable" forever. */
    FILE* f = fopen(file, "wb");
    assert(f);
    fputc(1, f);
    fclose(f);
    assert(pc_identity_load(&b, path, "ALICE"));
    assert(strncmp(b.code, "ALIC#", 5) == 0);
    assert(memcmp(b.public_key, a.public_key, 32) != 0);
    /* and the replacement is itself persistent */
    assert(pc_identity_load(&reloaded, path, "ALICE"));
    assert(memcmp(reloaded.public_key, b.public_key, 32) == 0);
    pc_identity_clear(&a);
    for (size_t i = 0; i < sizeof a; ++i)
        assert(((unsigned char*)&a)[i] == 0);
    unlink(file);
    rmdir(path);
    puts("identity persistence, signatures, TAG#NUM codes and wrong-length key recovery passed");
}
