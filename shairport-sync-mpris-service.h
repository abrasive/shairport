
#ifndef MPRIS_SERVICE_H
#define MPRIS_SERVICE_H

#include "shairport-sync-mpris-interface.h"
#include "shairport-sync-mpris-player-interface.h"

OrgMprisMediaPlayer2 *mprisPlayerSkeleton;
OrgMprisMediaPlayer2Player *mprisPlayerPlayerSkeleton;

int start_mpris_service();

#endif /* #ifndef MPRIS_SERVICE_H */
