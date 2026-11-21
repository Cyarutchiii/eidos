/*
 * patches: src/game/ov019/union_msg.c UNION_MSG_SelectMainMenu
 *
 * symbols:
 *   UNION_MSG_SelectMenu 0x0217F310 thumb ov019
 */
#include "mp.h"

typedef struct UnionRoomWork UnionRoomWork;
extern int UNION_MSG_SelectMenu(UnionRoomWork *work, void *fieldmap);

int UNION_MSG_SelectMainMenu(UnionRoomWork *work, void *fieldmap)
{
    int choice = bw_mp_union_menu_choice();

    return choice >= 0 ? choice : UNION_MSG_SelectMenu(work, fieldmap);
}
