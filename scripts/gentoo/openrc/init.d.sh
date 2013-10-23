#!/sbin/runscript

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

	if [[ ! -z "$LOGFILE" ]]; then OPTIONS="$OPTIONS --log $LOGFILE"; fi

	if [[ ! -z "$ERRFILE" ]]; then OPTIONS="$OPTIONS --error $ERRFILE"; fi

	if [[ ! -z "$BACKEND" ]]; then OPTIONS="$OPTIONS --output=$BACKEND $BACKEND_OPTS"; fi
	if [[ ! -z "$BACKEND_OPTS" ]]; then OPTIONS="$OPTIONS -- $BACKEND_OPTS"; fi

	start-stop-daemon --start --exec $EXEC -- $OPTIONS
	eend $?
}

stop() {
	ebegin "Stopping shairport"
	start-stop-daemon --stop --exec $EXEC
	eend $?
}
