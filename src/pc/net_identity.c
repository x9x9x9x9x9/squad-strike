/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "net_identity.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

/* The same SHA-1 as the DHT infohash, not a password/secret hash. */
extern void pc_dht_sha1(const void* data, size_t length, uint8_t out[20]);
extern void pc_log_line(const char* fmt, ...);

bool pc_identity_random(void* bytes, size_t length) {
#ifdef _WIN32
    return length <= ULONG_MAX &&
           BCryptGenRandom(NULL, bytes, (ULONG)length, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    size_t pos = 0;
    while (pos < length) {
        ssize_t n = read(fd, (uint8_t*)bytes + pos, length - pos);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            close(fd);
            return false;
        }
        pos += (size_t)n;
    }
    close(fd);
    return true;
#endif
}

static char upper(char c) {
    return c >= 'a' && c <= 'z' ? (char)(c - ('a' - 'A')) : c;
}
static bool tag_char(char c) {
    return c >= 'A' && c <= 'Z';
}
static bool digit(char c) {
    return c >= '0' && c <= '9';
}

/* Connect codes are Slippi-style and chosen by the player: TAG#NUM, a tag
 * of 1-4 letters and a number of 1-3 digits ("CHRS#123"). Unlike the old
 * NAME#K3XQ2M7A codes they are not derived from the key, so they can be
 * anything; the key still signs every pairing message (net_match.c). The
 * number is kept without leading zeros so "CHRS#007" and "CHRS#7" are the
 * same code. */
bool pc_identity_code_valid(const char* code) {
    if (!code)
        return false;
    size_t tag = 0;
    while (tag_char(code[tag]))
        tag++;
    if (tag < 1 || tag > PC_CODE_TAG_MAX || code[tag] != '#')
        return false;
    const char* num = code + tag + 1;
    size_t digits = 0;
    while (digit(num[digits]))
        digits++;
    if (digits < 1 || digits > PC_CODE_NUM_MAX || num[digits] != '\0')
        return false;
    return !(digits > 1 && num[0] == '0');
}

/* Tag letters and number digits to a canonical code, or false. */
static bool make_code(
    const char* tag, size_t tag_len, const char* num, size_t num_len, char out[PC_CODE_SIZE]) {
    if (tag_len < 1 || tag_len > PC_CODE_TAG_MAX || num_len < 1 || num_len > PC_CODE_NUM_MAX)
        return false;
    while (num_len > 1 && num[0] == '0') {
        num++;
        num_len--;
    }
    size_t n = 0;
    for (size_t i = 0; i < tag_len; i++)
        out[n++] = upper(tag[i]);
    out[n++] = '#';
    memcpy(out + n, num, num_len);
    out[n + num_len] = '\0';
    return pc_identity_code_valid(out);
}

bool pc_identity_normalize_code(const char* text, char out[PC_CODE_SIZE]) {
    if (!text)
        return false;
    char tag[PC_CODE_TAG_MAX], num[PC_CODE_NUM_MAX];
    size_t t = 0, d = 0;
    const char* p = text;
    while (*p == ' ')
        p++;
    for (; tag_char(upper(*p)); p++) {
        if (t == sizeof tag)
            return false;
        tag[t++] = *p;
    }
    while (*p == ' ')
        p++;
    if (*p++ != '#')
        return false;
    while (*p == ' ')
        p++;
    for (; digit(*p); p++) {
        if (d == sizeof num)
            return false;
        num[d++] = *p;
    }
    while (*p == ' ')
        p++;
    return *p == '\0' && make_code(tag, t, num, d, out);
}

bool pc_identity_parse_code(const char* text, char out[PC_CODE_SIZE]) {
    if (!text)
        return false;
    /* TAG#NUM anywhere in the text ("my code is chrs#123!"), with a space
     * allowed either side of the '#'. A tag or number that runs on past its
     * limit is not a code, rather than a code cut out of a longer word. */
    for (const char* hash = strchr(text, '#'); hash; hash = strchr(hash + 1, '#')) {
        const char* end = hash;
        if (end > text && end[-1] == ' ')
            end--;
        const char* tag = end;
        while (tag > text && tag_char(upper(tag[-1])))
            tag--;
        if (tag > text && digit(tag[-1]))
            continue;
        const char* num = hash + 1;
        if (*num == ' ')
            num++;
        size_t digits = 0;
        while (digit(num[digits]))
            digits++;
        if (tag_char(upper(num[digits])))
            continue;
        if (make_code(tag, (size_t)(end - tag), num, digits, out))
            return true;
    }
    return false;
}

/* A default code for a profile that has not chosen one: up to four letters
 * of `hint` (the old player name) or PLYR, and a number from the key, so two
 * fresh installs are unlikely to share one. */
static void default_code(const char* hint, const uint8_t public_key[32], char out[PC_CODE_SIZE]) {
    char tag[PC_CODE_TAG_MAX];
    size_t t = 0;
    for (const char* p = hint ? hint : ""; *p && t < sizeof tag; p++)
        if (tag_char(upper(*p)))
            tag[t++] = upper(*p);
    if (!t) {
        memcpy(tag, "PLYR", 4);
        t = 4;
    }
    uint8_t digest[20];
    pc_dht_sha1(public_key, 32, digest);
    unsigned number = ((unsigned)digest[0] << 8 | digest[1]) % 1000;
    char num[4];
    int d = snprintf(num, sizeof num, "%u", number);
    make_code(tag, t, num, (size_t)d, out);
}

bool pc_identity_load(PcNetIdentity* id, const char* directory, const char* code) {
    uint8_t seed[32];
    char path[4096];
    if (!id || !directory)
        return false;
    memset(id, 0, sizeof *id);
    int n = snprintf(path, sizeof path, "%s/identity.key", directory);
    if (n < 0 || n >= (int)sizeof path)
        return false;
    /* A key is exactly 32 bytes, so a file of any other length cannot hold one
     * (a 0-byte file left by a crash or a full disk between create and write,
     * or a truncated copy). Failing on those stranded the profile for good:
     * every later run reported "identity unavailable" until the file was
     * deleted by hand. A wrong-length file is replaced; a 32-byte file is
     * never replaced, and a read error on one is still an error, because it
     * may hold the identity this profile's rating history belongs to. */
    bool loaded = false, replace = false;
    FILE* f = fopen(path, "rb");
    if (f) {
        int64_t size = fseek(f, 0, SEEK_END) == 0 ? (int64_t)ftell(f) : -1;
        if (size == (int64_t)sizeof seed && fseek(f, 0, SEEK_SET) == 0) {
            loaded = fread(seed, 1, sizeof seed, f) == sizeof seed && !ferror(f);
        } else if (size >= 0) {
            replace = true;
            pc_log_line("net: %s is %lld bytes, not a 32-byte identity key;"
                        " generating a new one",
                path, (long long)size);
        }
        fclose(f);
        if (!loaded && !replace) {
            pc_log_line("net: cannot read %s (errno %d)", path, errno);
            crypto_wipe(seed, sizeof seed);
            return false;
        }
    } else if (errno != ENOENT) {
        pc_log_line("net: cannot open %s (errno %d)", path, errno);
        return false;
    }
    if (!loaded) {
        if (!pc_identity_random(seed, sizeof seed))
            return false;
#ifdef _WIN32
        f = fopen(path, "wb");
#else
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        f = fd < 0 ? NULL : fdopen(fd, "wb");
        if (!f && fd >= 0)
            close(fd);
#endif
        if (!f) {
            pc_log_line("net: cannot write %s (errno %d)", path, errno);
            crypto_wipe(seed, sizeof seed);
            return false;
        }
        bool ok = fwrite(seed, 1, sizeof seed, f) == sizeof seed;
        if (fflush(f) != 0)
            ok = false;
#ifndef _WIN32
        if (fsync(fileno(f)) != 0)
            ok = false;
#endif
        if (fclose(f) != 0)
            ok = false;
        if (!ok) {
            pc_log_line("net: failed to write %s", path);
            crypto_wipe(seed, sizeof seed);
            return false;
        }
    }
    crypto_ed25519_key_pair(id->secret_key, id->public_key, seed);
    crypto_wipe(seed, sizeof seed);
    if (!pc_identity_normalize_code(code, id->code))
        default_code(code, id->public_key, id->code);
    return true;
}

void pc_identity_sign(
    const PcNetIdentity* id, uint8_t signature[64], const void* message, size_t length) {
    crypto_ed25519_sign(signature, id->secret_key, message, length);
}
bool pc_identity_verify(
    const uint8_t key[32], const uint8_t signature[64], const void* message, size_t length) {
    return crypto_ed25519_check(signature, key, message, length) == 0;
}
void pc_identity_clear(PcNetIdentity* id) {
    crypto_wipe(id, sizeof *id);
}
