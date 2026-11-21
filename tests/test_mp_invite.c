/* Standalone invite protocol tests: no relay, ROM, or SDL required. */
#include <stdio.h>
#include <string.h>
#include "mp_invite.h"

static char sent[128];
static u32 sent_peer;
static int sent_count;
static int allow = 1;
static int errors;
static int fail_send;

static int send_packet(u32 peer, const void *data, u32 size)
{
    if (fail_send || size >= sizeof sent) return -1;
    memcpy(sent, data, size);
    sent[size] = '\0';
    sent_peer = peer;
    sent_count++;
    return 0;
}

static int is_nearby(u32 peer)
{
    return allow && peer == 2;
}

#define CHECK(msg, expr) do { \
    if (!(expr)) { fprintf(stderr, "FAILED: %s\n", msg); errors++; } \
} while (0)

int main(void)
{
    static const BwMpInviteIO io = { send_packet, is_nearby };
    BwMpInviteView v;
    u32 id;
    char pkt[80];

    bw_mp_invite_set_io(&io);
    bw_mp_invite_reset();
    CHECK("invalid target", bw_mp_invite_request(3, BW_MP_INVITE_BATTLE, 100) == -1);
    CHECK("request sent", bw_mp_invite_request(2, BW_MP_INVITE_BATTLE, 100) == 0);
    CHECK("directly addressed", sent_peer == 2);
    CHECK("protocol", sscanf(sent, "invite %u 1", &id) == 1 && id != 0);
    CHECK("can't overlap", bw_mp_invite_request(2, BW_MP_INVITE_TRADE, 101) == -1);
    v = bw_mp_invite_view();
    CHECK("outgoing battle", v.state == BW_MP_INVITE_OUTGOING && v.kind == BW_MP_INVITE_BATTLE);

    snprintf(pkt, sizeof pkt, "answer %u 1", (unsigned)id);
    (void)bw_mp_invite_message(3, pkt, (u32)strlen(pkt), 110);
    CHECK("spoofed answer ignored", bw_mp_invite_view().state == BW_MP_INVITE_OUTGOING);
    (void)bw_mp_invite_message(2, "answer 9999 1", 13, 110);
    CHECK("wrong id ignored", bw_mp_invite_view().state == BW_MP_INVITE_OUTGOING);
    (void)bw_mp_invite_message(2, pkt, (u32)strlen(pkt), 111);
    CHECK("accepted", bw_mp_invite_view().state == BW_MP_INVITE_ACCEPTED);
    bw_mp_invite_dismiss();

    CHECK("trade offer arrives", bw_mp_invite_message(2, "invite 55 2", 11, 200) == 1);
    v = bw_mp_invite_view();
    CHECK("inbound trade", v.state == BW_MP_INVITE_INCOMING && v.kind == BW_MP_INVITE_TRADE);
    {
        int before = sent_count;
        CHECK("duplicate pending invite consumed",
              bw_mp_invite_message(2, "invite 55 2", 11, 201) == 1);
        CHECK("duplicate does not answer or replace",
              sent_count == before && bw_mp_invite_view().request_id == 55);
    }
    CHECK("busy invite consumed", bw_mp_invite_message(2, "invite 56 1", 11, 201) == 1);
    CHECK("busy invite denied", strcmp(sent, "answer 56 0") == 0);
    CHECK("can reject", bw_mp_invite_reply(0, 210) == 0);
    CHECK("reply sent", strcmp(sent, "answer 55 0") == 0 && sent_peer == 2);
    CHECK("denied result", bw_mp_invite_view().state == BW_MP_INVITE_DENIED);
    bw_mp_invite_dismiss();

    CHECK("resolved invite stays dismissed",
          bw_mp_invite_message(2, "invite 55 2", 11, 220) == 1 &&
          bw_mp_invite_view().state == BW_MP_INVITE_NONE);
    CHECK("busy invite cannot reappear",
          bw_mp_invite_message(2, "invite 56 1", 11, 221) == 1 &&
          bw_mp_invite_view().state == BW_MP_INVITE_NONE);

    CHECK("another invite", bw_mp_invite_message(2, "invite 57 1", 11, 300) == 1);
    allow = 0;
    CHECK("can't accept after movement", bw_mp_invite_reply(1, 310) == 0);
    CHECK("distance veto", strcmp(sent, "answer 57 0") == 0);
    bw_mp_invite_dismiss();
    allow = 1;

    fail_send = 1;
    CHECK("transport failure leaves no outgoing invite",
          bw_mp_invite_request(2, BW_MP_INVITE_TRADE, 490) == -1 &&
          bw_mp_invite_view().state == BW_MP_INVITE_NONE);
    fail_send = 0;
    CHECK("request for timeout", bw_mp_invite_request(2, BW_MP_INVITE_TRADE, 500) == 0);
    bw_mp_invite_tick(500 + BW_MP_INVITE_TIMEOUT_MS - 1);
    CHECK("not early", bw_mp_invite_view().state == BW_MP_INVITE_OUTGOING);
    snprintf(pkt, sizeof pkt, "answer %u 1",
             (unsigned)bw_mp_invite_view().request_id);
    (void)bw_mp_invite_message(2, pkt, (u32)strlen(pkt),
                               500 + BW_MP_INVITE_TIMEOUT_MS);
    CHECK("timeout notifies the other peer", strncmp(sent, "cancel ", 7) == 0);
    CHECK("expired", bw_mp_invite_view().state == BW_MP_INVITE_EXPIRED);
    bw_mp_invite_dismiss();

    CHECK("offer for cancellation", bw_mp_invite_message(2, "invite 60 2", 11, 600) == 1);
    CHECK("remote cancellation", bw_mp_invite_message(2, "cancel 60", 9, 605) == 1);
    CHECK("cleared by cancellation", bw_mp_invite_view().state == BW_MP_INVITE_NONE);
    CHECK("one more inbound", bw_mp_invite_message(2, "invite 61 2", 11, 610) == 1);
    bw_mp_invite_peer_left(2, 611);
    CHECK("disconnect", bw_mp_invite_view().state == BW_MP_INVITE_CANCELLED);
    bw_mp_invite_reset();
    CHECK("non-invite passes through", bw_mp_invite_message(2, "pos 3 4 5", 9, 700) == 0);
    CHECK("bad invite consumed", bw_mp_invite_message(2, "invite bogus", 12, 700) == 1);
    CHECK("not accepted", bw_mp_invite_view().state == BW_MP_INVITE_NONE);
    CHECK("overflow rejected",
          bw_mp_invite_message(2, "invite 4294967297 1", 19, 710) == 1 &&
          bw_mp_invite_view().state == BW_MP_INVITE_NONE);
    CHECK("embedded NUL rejected",
          bw_mp_invite_message(2, "invite 73 1\0x", 13, 710) == 1 &&
          bw_mp_invite_view().state == BW_MP_INVITE_NONE);
    CHECK("local cancel starts with outgoing offer",
          bw_mp_invite_request(2, BW_MP_INVITE_TRADE, 720) == 0);
    bw_mp_invite_dismiss();
    CHECK("local cancel sent", strncmp(sent, "cancel ", 7) == 0 &&
          bw_mp_invite_view().state == BW_MP_INVITE_NONE);
    CHECK("incoming offer times out",
          bw_mp_invite_message(2, "invite 74 1", 11, 730) == 1 &&
          bw_mp_invite_reply(1, 730 + BW_MP_INVITE_TIMEOUT_MS) == -1 &&
          bw_mp_invite_view().state == BW_MP_INVITE_EXPIRED &&
          strcmp(sent, "answer 74 0") == 0);
    bw_mp_invite_tick(730 + BW_MP_INVITE_TIMEOUT_MS + 5000);
    CHECK("expired result clears", bw_mp_invite_view().state == BW_MP_INVITE_NONE);
    puts(errors ? "test_mp_invite failed" : "test_mp_invite passed");
    return errors ? 1 : 0;
}
