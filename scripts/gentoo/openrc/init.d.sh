#!/sbin/runscript
# Copyright 1999-2013 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

EXEC=/usr/local/bin/shairport

. /etc/conf.d/shairport

depend() {
	need net
	after bootmisc
}

start() {
	OPTIONS="--daemon"

	if [[ -z "$PIDFILE" ]]; then PIDFILE=/var/run/shairport.pid; fi
	OPTIONS="$OPTIONS --pidfile=$PIDFILE"

	if [[ ! -z "$NAME" ]]; then OPTIONS="$OPTIONS --name=$NAME"; fi
	
	if [[ ! -z "$BUF_FILL" ]]; then OPTIONS="$OPTIONS -b $BUF_FILL"; fi

	if [[ -z $NAME ]]
	then
		ebegin "Starting shairport"
	else
		ebegin "Starting shairport as $NAME"
	fi

	if [[ -z "$LOGFILE" ]]; then LOGFILE=/var/log/shairport.log; fi
	OPTIONS="$OPTIONS --log $LOGFILE"

	if [[ ! -z "$ERRFILE" ]]; then OPTIONS="$OPTIONS -error $ERRFILE"; fi

	if [[ ! -z "$BACKEND" ]]; then OPTIONS="$OPTIONS -output=$BACKEND $BACKEND_OPTS"; fi
	
	start-stop-daemon --start --exec $EXEC -- $OPTIONS
	eend $?
}

stop() {
	ebegin "Stopping shairport"
	start-stop-daemon --stop --exec $EXEC
	eend $?
}
