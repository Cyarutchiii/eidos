/* Small, peer-addressed invite protocol carried by eidos:port messages.
 * Every reply carries the request id; delayed replies cannot accept a new
 * request. Only one pending offer per client; busy recipients deny. */
#include "mp_invite.h"
#include "mp.h"

#include <stdio.h>
#include <string.h>

static BwMpInviteIO g_io;
static BwMpInviteView g_invite;
static u64 g_deadline;
/* The relay delivers messages in order. Remember each sender's newest
 * request even after dismissal or a busy decline. */
static u32 g_seen[BW_MP_PLAYERS_MAX];
#define BW_MP_INVITE_RESULT_MS 5000u
static u32 g_serial; /* deliberately NOT reset when another session opens */

static int eligible(u32 peer)
{
    return peer != 0 && g_io.eligible != NULL && g_io.eligible(peer);
}

static int send_pair(u32 peer, const char *verb, u32 id, u32 value)
{
    char buf[64];
    int n;

    if (g_io.send == NULL) {
        return -1;
    }
    n = snprintf(buf, sizeof buf, "%s %u %u", verb, (unsigned)id,
                 (unsigned)value);
    return n > 0 && (u32)n < sizeof buf &&
           g_io.send(peer, buf, (u32)n) == 0 ? 0 : -1;
}

static int send_cancel(u32 peer, u32 id)
{
    char buf[48];
    int n;

    if (g_io.send == NULL) {
        return -1;
    }
    n = snprintf(buf, sizeof buf, "cancel %u", (unsigned)id);
    return n > 0 && (u32)n < sizeof buf &&
           g_io.send(peer, buf, (u32)n) == 0 ? 0 : -1;
}

void bw_mp_invite_set_io(const BwMpInviteIO *io)
{
    if (io != NULL) {
        g_io = *io;
    } else {
        memset(&g_io, 0, sizeof g_io);
    }
}

static void clear_invite(void)
{
    memset(&g_invite, 0, sizeof g_invite);
    g_deadline = 0;
}

void bw_mp_invite_reset(void)
{
    clear_invite();
    memset(g_seen, 0, sizeof g_seen);
}

BwMpInviteView bw_mp_invite_view(void)
{
    return g_invite;
}

int bw_mp_invite_request(u32 peer, BwMpInviteKind kind, u64 now_ms)
{
    u32 id;

    if (g_invite.state != BW_MP_INVITE_NONE || !eligible(peer) ||
        (kind != BW_MP_INVITE_BATTLE && kind != BW_MP_INVITE_TRADE)) {
        return -1;
    }
    id = ++g_serial;
    if (id == 0) {
        id = ++g_serial;
    }
    if (send_pair(peer, "invite", id, (u32)kind) != 0) {
        return -1;
    }
    g_invite.state = BW_MP_INVITE_OUTGOING;
    g_invite.kind = kind;
    g_invite.peer = peer;
    g_invite.request_id = id;
    g_deadline = now_ms + BW_MP_INVITE_TIMEOUT_MS;
    return 0;
}

int bw_mp_invite_reply(int accept, u64 now_ms)
{
    bw_mp_invite_tick(now_ms);
    if (g_invite.state != BW_MP_INVITE_INCOMING) {
        return -1;
    }
    /* If either player has moved away, don't accept a stale offer. */
    if (accept && !eligible(g_invite.peer)) {
        accept = 0;
    }
    if (send_pair(g_invite.peer, "answer", g_invite.request_id,
                  accept ? 1u : 0u) != 0) {
        return -1;
    }
    g_invite.state = accept ? BW_MP_INVITE_ACCEPTED : BW_MP_INVITE_DENIED;
    g_deadline = now_ms + BW_MP_INVITE_RESULT_MS;
    return 0;
}

void bw_mp_invite_dismiss(void)
{
    if (g_invite.state == BW_MP_INVITE_OUTGOING) {
        (void)send_cancel(g_invite.peer, g_invite.request_id);
    } else if (g_invite.state == BW_MP_INVITE_INCOMING) {
        (void)send_pair(g_invite.peer, "answer", g_invite.request_id, 0u);
    }
    clear_invite();
}

/* Parse decimal fields from bounded network data without signed conversion,
 * integer overflow or accepting an embedded NUL as the end of a packet. */
static int number(const unsigned char **cursor, const unsigned char *end,
                  u32 *value)
{
    const unsigned char *p = *cursor;
    u32 n = 0;

    if (p == end || *p < '0' || *p > '9') {
        return 0;
    }
    do {
        u32 digit = *p++ - '0';
        if (n > (0xffffffffu - digit) / 10u) {
            return 0;
        }
        n = n * 10u + digit;
    } while (p < end && *p >= '0' && *p <= '9');
    *cursor = p;
    *value = n;
    return 1;
}

int bw_mp_invite_message(u32 peer, const void *data, u32 size, u64 now_ms)
{
    const unsigned char *p, *end;
    u32 id, value = 0;
    int is_invite, is_answer, is_cancel;

    if (data == NULL || size < 7) {
        return 0;
    }
    is_invite = memcmp(data, "invite ", 7) == 0;
    is_answer = memcmp(data, "answer ", 7) == 0;
    is_cancel = memcmp(data, "cancel ", 7) == 0;
    if (!is_invite && !is_answer && !is_cancel) {
        return 0;
    }
    if (size > 28 || peer == 0 || peer > BW_MP_PLAYERS_MAX) {
        return 1;
    }
    p = (const unsigned char *)data + 7;
    end = (const unsigned char *)data + size;
    if (!number(&p, end, &id) || id == 0 ||
        (!is_cancel && (p == end || *p++ != ' ' || !number(&p, end, &value))) ||
        p != end) {
        return 1;
    }
    bw_mp_invite_tick(now_ms);
    if (is_invite) {
        u32 *seen = &g_seen[peer - 1];

        if (value != BW_MP_INVITE_BATTLE && value != BW_MP_INVITE_TRADE) {
            return 1;
        }
        if (*seen && (id == *seen || id - *seen >= 0x80000000u)) {
            return 1;
        }
        *seen = id;
        if (!eligible(peer) || g_invite.state != BW_MP_INVITE_NONE) {
            (void)send_pair(peer, "answer", id, 0u);
            return 1;
        }
        g_invite.state = BW_MP_INVITE_INCOMING;
        g_invite.kind = (BwMpInviteKind)value;
        g_invite.peer = peer;
        g_invite.request_id = id;
        g_deadline = now_ms + BW_MP_INVITE_TIMEOUT_MS;
    } else if (g_invite.peer == peer && g_invite.request_id == id) {
        if (is_answer && value <= 1 && g_invite.state == BW_MP_INVITE_OUTGOING) {
            g_invite.state = value && eligible(peer) ?
                BW_MP_INVITE_ACCEPTED : BW_MP_INVITE_DENIED;
            g_deadline = now_ms + BW_MP_INVITE_RESULT_MS;
        } else if (is_cancel && g_invite.state == BW_MP_INVITE_INCOMING) {
            clear_invite();
        }
    }
    return 1;
}

void bw_mp_invite_tick(u64 now_ms)
{
    if (g_invite.state == BW_MP_INVITE_NONE) {
        return;
    }
    if (now_ms < g_deadline) {
        return;
    }
    if (g_invite.state == BW_MP_INVITE_OUTGOING) {
        (void)send_cancel(g_invite.peer, g_invite.request_id);
    } else if (g_invite.state == BW_MP_INVITE_INCOMING) {
        (void)send_pair(g_invite.peer, "answer", g_invite.request_id, 0u);
    } else {
        clear_invite();
        return;
    }
    g_invite.state = BW_MP_INVITE_EXPIRED;
    g_deadline = now_ms + BW_MP_INVITE_RESULT_MS;
}

void bw_mp_invite_peer_left(u32 peer, u64 now_ms)
{
    if (peer > 0 && peer <= BW_MP_PLAYERS_MAX) {
        g_seen[peer - 1] = 0;
    }
    if (g_invite.peer == peer &&
        (g_invite.state == BW_MP_INVITE_INCOMING ||
         g_invite.state == BW_MP_INVITE_OUTGOING)) {
        g_invite.state = BW_MP_INVITE_CANCELLED;
        g_deadline = now_ms + BW_MP_INVITE_RESULT_MS;
    }
}
