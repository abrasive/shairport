#!/bin/bash

# Calls $TARGET_SCRIPT with the following parameters...
# $1: artist
# $2: title
# $3: album
# $4: genre
# $5: comment
# $6: The full pathname to the artwork file

TARGET_SCRIPT="/home/user/shairport_now_playing.sh"
METADATA_DIR="/tmp/shairport"

test -p $METADATA_DIR/now_playing || mkfifo $METADATA_DIR/now_playing

while exec 3<$METADATA_DIR/now_playing
do
declare -A METADATA
IFS="="
while read -u3 mK mV; do
  if test "x$mK" = "x"; then break; fi
  METADATA["$mK"]="$mV"
done
exec 3<&-
$TARGET_SCRIPT "${METADATA["artist"]}" "${METADATA["title"]}" "${METADATA["album"]}" \
  "${METADATA["genre"]}" "${METADATA["comment"]}" "$METADATA_DIR/${METADATA["artwork"]}"

unset METADATA
done
