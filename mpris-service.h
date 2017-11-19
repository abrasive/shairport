
#ifndef MPRIS_SERVICE_H
#define MPRIS_SERVICE_H

#include "mpris-interface.h"
#include "mpris-player-interface.h"

MediaPlayer2 *mprisPlayerSkeleton;
MediaPlayer2Player *mprisPlayerPlayerSkeleton;

int start_mpris_service();

#endif /* #ifndef MPRIS_SERVICE_H */
