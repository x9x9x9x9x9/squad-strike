/* SPDX-License-Identifier: GPL-3.0-or-later */
/* UPnP-IGD port mapping (net_upnp.h): SSDP discovery of the router, its
 * device description, then AddPortMapping / GetExternalIPAddress /
 * DeletePortMapping over SOAP. HTTP/1.0 throughout, so no reply is chunked.
 * Runs on a detached worker thread with socket timeouts; the game thread
 * only reads the result under the mutex. */
#include "net_upnp.h"
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET UpnpSocket;
#define UPNP_BAD INVALID_SOCKET
#define UPNP_CLOSE closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int UpnpSocket;
#define UPNP_BAD (-1)
#define UPNP_CLOSE close
#endif

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) void pc_log_line(const char* fmt, ...) {
    (void)fmt;
}
#else
void pc_log_line(const char* fmt, ...);
#endif

typedef struct UpnpControl {
    char host[64];
    uint16_t port;
    char path[256];
    char service[128];
} UpnpControl;

static SDL_Mutex* lock;
static unsigned generation; /* bumped by every start/unmap: stale workers drop results */
static bool mapped;
static uint32_t mapped_address;
static uint16_t mapped_port;
static UpnpControl mapped_control;

typedef struct Job {
    unsigned generation;
    uint16_t port;
    bool remove;
    UpnpControl control;
} Job;

static void set_timeouts(UpnpSocket s, int ms) {
#ifdef _WIN32
    DWORD t = (DWORD)ms;
#else
    struct timeval t = {ms / 1000, (ms % 1000) * 1000};
#endif
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof t);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&t, sizeof t);
}

/* Case-insensitive strstr. */
static const char* find_ci(const char* hay, const char* needle) {
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == n)
            return hay;
    }
    return NULL;
}

/* "http://host[:port]/path" -> parts. */
static bool parse_url(const char* url, char host[64], uint16_t* port, char path[256]) {
    if (strncmp(url, "http://", 7) != 0)
        return false;
    const char* p = url + 7;
    size_t h = strcspn(p, ":/");
    if (h == 0 || h >= 64)
        return false;
    memcpy(host, p, h);
    host[h] = 0;
    p += h;
    *port = 80;
    if (*p == ':') {
        long v = strtol(p + 1, (char**)&p, 10);
        if (v <= 0 || v > 65535)
            return false;
        *port = (uint16_t)v;
    }
    snprintf(path, 256, "%s", *p == '/' ? p : "/");
    return true;
}

/* One HTTP/1.0 exchange; the whole reply into `out`. `local` gets this PC's
 * address on the router's network. */
static int http(const char* host, uint16_t port, const char* request, size_t request_len,
    char* out, size_t cap, uint32_t* local) {
    struct sockaddr_in to = {0};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &to.sin_addr) != 1)
        return -1; /* routers describe themselves by address */
    UpnpSocket s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == UPNP_BAD)
        return -1;
    set_timeouts(s, 3000);
    int n = -1;
    if (connect(s, (struct sockaddr*)&to, sizeof to) == 0) {
        if (local) {
            struct sockaddr_in me;
#ifdef _WIN32
            int len = sizeof me;
#else
            socklen_t len = sizeof me;
#endif
            if (getsockname(s, (struct sockaddr*)&me, &len) == 0)
                *local = me.sin_addr.s_addr;
        }
        size_t sent = 0;
        while (sent < request_len) {
            int k = (int)send(s, request + sent, (int)(request_len - sent), 0);
            if (k <= 0)
                break;
            sent += (size_t)k;
        }
        if (sent == request_len) {
            size_t got = 0;
            for (;;) {
                if (got + 1 >= cap)
                    break;
                int k = (int)recv(s, out + got, (int)(cap - 1 - got), 0);
                if (k <= 0)
                    break;
                got += (size_t)k;
            }
            out[got] = 0;
            n = (int)got;
        }
    }
    UPNP_CLOSE(s);
    return n;
}

/* The first router that answers an SSDP search: its description URL. */
static bool discover(char location[256]) {
    static const char* const targets[] = {
        "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
        "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
        "urn:schemas-upnp-org:service:WANIPConnection:1",
        "urn:schemas-upnp-org:service:WANPPPConnection:1",
    };
    UpnpSocket s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == UPNP_BAD)
        return false;
    set_timeouts(s, 250);
    struct sockaddr_in group = {0};
    group.sin_family = AF_INET;
    group.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &group.sin_addr);
    bool found = false;
    for (int round = 0; round < 2 && !found; round++) {
        for (size_t i = 0; i < sizeof targets / sizeof targets[0]; i++) {
            char msg[256];
            int n = snprintf(msg, sizeof msg,
                "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
                "MAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n",
                targets[i]);
            sendto(s, msg, n, 0, (struct sockaddr*)&group, sizeof group);
        }
        uint64_t until = SDL_GetTicks() + 2500;
        while (!found && SDL_GetTicks() < until) {
            char reply[1536];
            int n = (int)recv(s, reply, sizeof reply - 1, 0);
            if (n <= 0)
                continue;
            reply[n] = 0;
            const char* at = find_ci(reply, "\nlocation:");
            if (!at)
                continue;
            at += 10;
            while (*at == ' ')
                at++;
            size_t len = strcspn(at, "\r\n");
            if (len == 0 || len >= 256)
                continue;
            memcpy(location, at, len);
            location[len] = 0;
            found = strncmp(location, "http://", 7) == 0;
        }
    }
    UPNP_CLOSE(s);
    return found;
}

/* The WAN connection service in a device description, and its control URL
 * resolved against the description's own address (or its URLBase). */
static bool find_control(const char* xml, const char* location, UpnpControl* c) {
    char base_host[64], base_path[256];
    uint16_t base_port;
    if (!parse_url(location, base_host, &base_port, base_path))
        return false;
    const char* url_base = find_ci(xml, "<URLBase>");
    if (url_base) {
        char tmp[256];
        url_base += 9;
        size_t len = strcspn(url_base, "<");
        if (len < sizeof tmp) {
            memcpy(tmp, url_base, len);
            tmp[len] = 0;
            parse_url(tmp, base_host, &base_port, base_path);
        }
    }
    for (const char* p = find_ci(xml, "<serviceType>"); p; p = find_ci(p + 1, "<serviceType>")) {
        const char* type = p + 13;
        size_t len = strcspn(type, "<");
        if (len >= sizeof c->service)
            continue;
        memcpy(c->service, type, len);
        c->service[len] = 0;
        if (!strstr(c->service, "WANIPConnection:") && !strstr(c->service, "WANPPPConnection:"))
            continue;
        const char* end = find_ci(type, "</service>");
        const char* ctl = find_ci(type, "<controlURL>");
        if (!ctl || (end && ctl > end))
            continue;
        ctl += 12;
        size_t clen = strcspn(ctl, "<");
        char url[256];
        if (clen == 0 || clen >= sizeof url)
            continue;
        memcpy(url, ctl, clen);
        url[clen] = 0;
        if (strncmp(url, "http://", 7) == 0)
            return parse_url(url, c->host, &c->port, c->path);
        snprintf(c->host, sizeof c->host, "%s", base_host);
        c->port = base_port;
        snprintf(c->path, sizeof c->path, "%s%s", url[0] == '/' ? "" : "/", url);
        return true;
    }
    return false;
}

/* A SOAP call; true on HTTP 200. */
static bool soap(const UpnpControl* c, const char* action, const char* args, char* out,
    size_t cap) {
    char body[1536], request[2304];
    int b = snprintf(body, sizeof body,
        "<?xml version=\"1.0\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
        "<u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>\r\n",
        action, c->service, args, action);
    if (b < 0 || b >= (int)sizeof body)
        return false;
    int r = snprintf(request, sizeof request,
        "POST %s HTTP/1.0\r\nHost: %s:%u\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
        "Content-Length: %d\r\nSOAPAction: \"%s#%s\"\r\nConnection: close\r\n\r\n%s",
        c->path, c->host, (unsigned)c->port, b, c->service, action, body);
    if (r < 0 || r >= (int)sizeof request)
        return false;
    int n = http(c->host, c->port, request, (size_t)r, out, cap, NULL);
    return n > 12 && strncmp(out, "HTTP/", 5) == 0 && strstr(out, " 200") &&
           strstr(out, " 200") < strchr(out, '\n');
}

static void delete_mapping(const UpnpControl* c, uint16_t port) {
    char args[256], reply[2048];
    snprintf(args, sizeof args,
        "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort>"
        "<NewProtocol>UDP</NewProtocol>",
        (unsigned)port);
    bool ok = soap(c, "DeletePortMapping", args, reply, sizeof reply);
    pc_log_line("upnp: delete UDP %u %s", (unsigned)port, ok ? "ok" : "failed");
}

static int worker(void* data) {
    Job job = *(Job*)data;
    free(data);
    if (job.remove) {
        delete_mapping(&job.control, job.port);
        return 0;
    }
    char location[256];
    if (!discover(location)) {
        pc_log_line("upnp: no router answered (UPnP off, or not a home network)");
        return 0;
    }
    char path[256], host[64];
    uint16_t port;
    static char page[32768];
    char request[512];
    uint32_t local = 0;
    if (!parse_url(location, host, &port, path))
        return 0;
    int r = snprintf(request, sizeof request,
        "GET %s HTTP/1.0\r\nHost: %s:%u\r\nConnection: close\r\n\r\n", path, host, (unsigned)port);
    UpnpControl c = {0};
    if (r <= 0 || http(host, port, request, (size_t)r, page, sizeof page, &local) <= 0 ||
        !find_control(page, location, &c) || !local)
    {
        pc_log_line("upnp: router at %s has no usable WAN service", location);
        return 0;
    }
    char me[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local, me, sizeof me);
    char args[768], reply[4096];
    bool ok = false;
    /* An hour's lease; some routers only take permanent ones (error 725),
     * which pc_upnp_unmap removes again. */
    for (int lease = 3600; !ok && lease >= 0; lease -= 3600) {
        snprintf(args, sizeof args,
            "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort>"
            "<NewProtocol>UDP</NewProtocol><NewInternalPort>%u</NewInternalPort>"
            "<NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
            "<NewPortMappingDescription>melee-pc</NewPortMappingDescription>"
            "<NewLeaseDuration>%d</NewLeaseDuration>",
            (unsigned)job.port, (unsigned)job.port, me, lease);
        ok = soap(&c, "AddPortMapping", args, reply, sizeof reply);
        if (!ok && !strstr(reply, "725"))
            break;
    }
    if (!ok) {
        pc_log_line("upnp: router refused UDP %u -> %s", (unsigned)job.port, me);
        return 0;
    }
    uint32_t external = 0;
    if (soap(&c, "GetExternalIPAddress", "", reply, sizeof reply)) {
        const char* at = find_ci(reply, "<NewExternalIPAddress>");
        if (at) {
            char ip[INET_ADDRSTRLEN] = {0};
            at += 22;
            size_t len = strcspn(at, "<");
            if (len < sizeof ip) {
                memcpy(ip, at, len);
                inet_pton(AF_INET, ip, &external);
            }
        }
    }
    pc_log_line("upnp: router %s forwards UDP %u -> %s (external address %s)", c.host,
        (unsigned)job.port, me, external ? "known" : "unknown");
    SDL_LockMutex(lock);
    if (job.generation == generation) {
        mapped = true;
        mapped_address = external;
        mapped_port = job.port;
        mapped_control = c;
    } else {
        /* Superseded while it ran: undo it rather than leave it open. */
        Job* undo = malloc(sizeof *undo);
        if (undo) {
            *undo = (Job){0, job.port, true, c};
            SDL_Thread* t = SDL_CreateThread(worker, "upnp", undo);
            if (t)
                SDL_DetachThread(t);
            else
                free(undo);
        }
    }
    SDL_UnlockMutex(lock);
    return 0;
}

static void spawn(Job job) {
    Job* j = malloc(sizeof *j);
    if (!j)
        return;
    *j = job;
    SDL_Thread* t = SDL_CreateThread(worker, "upnp", j);
    if (t)
        SDL_DetachThread(t);
    else
        free(j);
}

void pc_upnp_map_start(uint16_t port) {
    if (getenv("MELEE_UPNP") && getenv("MELEE_UPNP")[0] == '0')
        return;
    if (!lock && !(lock = SDL_CreateMutex()))
        return;
    SDL_LockMutex(lock);
    bool same = mapped && mapped_port == port;
    SDL_UnlockMutex(lock);
    if (same)
        return; /* still forwarded from the last search */
    pc_upnp_unmap();
    SDL_LockMutex(lock);
    unsigned g = ++generation;
    SDL_UnlockMutex(lock);
    Job job = {0};
    job.generation = g;
    job.port = port;
    spawn(job);
}

bool pc_upnp_mapped(uint32_t* external_address, uint16_t* external_port) {
    if (!lock)
        return false;
    SDL_LockMutex(lock);
    bool ok = mapped;
    if (ok) {
        *external_address = mapped_address;
        *external_port = mapped_port;
    }
    SDL_UnlockMutex(lock);
    return ok;
}

void pc_upnp_unmap(void) {
    if (!lock)
        return;
    SDL_LockMutex(lock);
    generation++;
    bool had = mapped;
    Job job = {0, mapped_port, true, mapped_control};
    mapped = false;
    SDL_UnlockMutex(lock);
    if (had)
        spawn(job);
}
