/* Multiplayer overworld battle/trade invitations. No game/SDL dependencies. */
#ifndef BW_PORT_MP_INVITE_H
#define BW_PORT_MP_INVITE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BW_MP_INVITE_TIMEOUT_MS 30000u

typedef enum BwMpInviteKind {
    BW_MP_INVITE_BATTLE = 1,
    BW_MP_INVITE_TRADE = 2
} BwMpInviteKind;

typedef enum BwMpInviteState {
    BW_MP_INVITE_NONE = 0,
    BW_MP_INVITE_OUTGOING,
    BW_MP_INVITE_INCOMING,
    BW_MP_INVITE_ACCEPTED,
    BW_MP_INVITE_DENIED,
    BW_MP_INVITE_EXPIRED,
    BW_MP_INVITE_CANCELLED
} BwMpInviteState;

typedef struct BwMpInviteView {
    BwMpInviteState state;
    BwMpInviteKind kind;
    u32 peer;
    u32 request_id;
    int initiator;
} BwMpInviteView;

typedef struct BwMpInviteIO {
    int (*send)(u32 peer, const void *bytes, u32 size); /* 0 on success */
    int (*eligible)(u32 peer);  /* connected, adjacent, same zone, on field */
} BwMpInviteIO;

/* The co-op layer supplies the transport and proximity check. The state
 * machine never starts a trade or battle itself: that requires a separate
 * game-flow handoff after both players consent. */
void bw_mp_invite_set_io(const BwMpInviteIO *io);
void bw_mp_invite_reset(void);
BwMpInviteView bw_mp_invite_view(void);
int  bw_mp_invite_request(u32 peer, BwMpInviteKind kind, u64 now_ms);
int  bw_mp_invite_reply(int accept, u64 now_ms);
/* Exchange matching readiness and take the agreed handoff once. */
int bw_mp_invite_ready(BwMpInviteView *out);
void bw_mp_invite_dismiss(void);  /* cancels a pending request, closes a result */
int  bw_mp_invite_message(u32 peer, const void *data, u32 size, u64 now_ms);
void bw_mp_invite_tick(u64 now_ms);
void bw_mp_invite_peer_left(u32 peer, u64 now_ms);

#ifdef __cplusplus
}
#endif
#endif
