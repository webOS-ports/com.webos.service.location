#!/bin/bash
# Functional and stress test for com.webos.service.location on a connected
# LuneOS device (adb).  Exercises every luna method the service registers,
# feeds it the malformed payloads the herrie/fixes audit hardened against,
# and hammers the subscription paths to shake out crashes and fd leaks.
#
# Usage: tools/device-test.sh [adb-serial] [stress-iterations]
set -u

SERIAL=${1:-}
ITER=${2:-200}
ADB="adb ${SERIAL:+-s $SERIAL}"
URI=luna://com.webos.service.location
PASS=0
FAIL=0

shell() { $ADB shell "$@" 2>/dev/null; }

call() { # call <method> <payload>  -> stdout json
    shell "luna-send -n 1 $URI/$1 '$2'" | tr -d '\r'
}

check() { # check <label> <method> <payload> <grep-pattern>
    local out
    out=$(call "$2" "$3")
    if echo "$out" | grep -q "$4"; then
        PASS=$((PASS+1))
        echo "ok   $1"
    else
        FAIL=$((FAIL+1))
        echo "FAIL $1: got: $out"
    fi
}

service_pid() { shell 'ps -A 2>/dev/null || ps' | awk '/[/]usr\/bin\/location|[ ]location$/ {print $1; exit}'; }

fd_count() { local pid=$1; shell "ls /proc/$pid/fd 2>/dev/null | wc -l" | tr -dc 0-9; }

echo "=== com.webos.service.location device test ==="
$ADB get-serialno

# ---- functional: every registered method answers, correctly shaped --------
check getAllLocationHandlers  getAllLocationHandlers '{}'                    '"returnValue":true'
check getLocationHandlerDetails-gps getLocationHandlerDetails '{"Handler":"gps"}' '"returnValue":true'
check getLocationHandlerDetails-nw  getLocationHandlerDetails '{"Handler":"network"}' '"returnValue":true'
check getLocationHandlerDetails-bad getLocationHandlerDetails '{"Handler":"bogus"}' '"returnValue":false'
check getState-gps            getState  '{"Handler":"gps"}'                  '"returnValue":true'
check getState-network        getState  '{"Handler":"network"}'              '"returnValue":true'
check setState-gps-on         setState  '{"Handler":"gps","state":true}'     '"returnValue":true'
check getGpsStatus            getGpsStatus '{}'                              '"returnValue":true'
check getTimeToFirstFix       getTimeToFirstFix '{}'                         '"TTFF"'
check getGpsDebugData         getGpsDebugData '{}'                           '"returnValue"'
check getNfwNotifications     getNfwNotifications '{"subscribe":true}'       '"returnValue"'
check stopGPS                 stopGPS  '{}'                                  '"returnValue"'
# geofence: an unknown id must be rejected, not crash the bounds-checked map
check removeGeofence-unknown  '/geofence/removeGeofenceArea' '{"geofenceid":1100}' '"returnValue":false'
check removeGeofence-oob-low  '/geofence/removeGeofenceArea' '{"geofenceid":-5}'   '"returnValue":false'
check removeGeofence-oob-high '/geofence/removeGeofenceArea' '{"geofenceid":99999}' '"returnValue":false'

# ---- hostile payloads: the parsers hardened in this branch ----------------
# sendExtraCommand/enable_cep_log used to strncpy unbounded segments
LONG=$(printf 'A%.0s' $(seq 1 2000))
check cep-log-overflow sendExtraCommand "{\"command\":\"enable_cep_log:$LONG,$LONG,$LONG\"}" '"returnValue"'
check cep-log-disable  sendExtraCommand '{"command":"disable_cep_log"}' '"returnValue"'
check extra-cmd-bogus  sendExtraCommand '{"command":"no_such_command"}' '"returnValue":false'
# setState with a long handler name exercises the bounded subscription key
check setState-long-handler setState "{\"Handler\":\"$LONG\",\"state\":true}" '"returnValue"'
# schema violations must be rejected cleanly
check schema-bad-json getState '{"Handler":12345}' '"returnValue":false'
check mock-bad-name   '/mock/setLocation' '{"name":"bogus","location":{"latitude":1,"longitude":2}}' '"returnValue":false'

SVCPID=$(service_pid)
echo "service pid: ${SVCPID:-not running}"

# ---- stress: subscribe/cancel churn + hostile payload loop ----------------
echo "=== stress: $ITER iterations ==="
FD_BEFORE=$( [ -n "$SVCPID" ] && fd_count "$SVCPID" || echo 0)

shell "for i in \$(seq 1 $ITER); do
    luna-send -n 1 $URI/getAllLocationHandlers '{}' >/dev/null 2>&1
    luna-send -n 1 $URI/getState '{\"Handler\":\"gps\"}' >/dev/null 2>&1
    luna-send -n 1 $URI/getTimeToFirstFix '{}' >/dev/null 2>&1
    luna-send -n 1 -t 1 $URI/getNfwNotifications '{\"subscribe\":true}' >/dev/null 2>&1
    luna-send -n 1 $URI/sendExtraCommand '{\"command\":\"enable_cep_log:1,2,3\"}' >/dev/null 2>&1
    luna-send -n 1 $URI/sendExtraCommand '{\"command\":\"disable_cep_log\"}' >/dev/null 2>&1
    luna-send -n 1 $URI/geofence/removeGeofenceArea '{\"geofenceid\":99999}' >/dev/null 2>&1
done; echo STRESS-DONE"

# concurrent hammering from several shells
echo "=== stress: 4-way concurrent ==="
for w in 1 2 3 4; do
    shell "for i in \$(seq 1 50); do
        luna-send -n 1 $URI/getAllLocationHandlers '{}' >/dev/null 2>&1
        luna-send -n 1 $URI/getGpsStatus '{}' >/dev/null 2>&1
    done" &
done
wait

NEWPID=$(service_pid)
if [ -n "$SVCPID" ] && [ "$SVCPID" != "$NEWPID" ]; then
    FAIL=$((FAIL+1))
    echo "FAIL service crashed/restarted during stress (pid $SVCPID -> ${NEWPID:-gone})"
else
    PASS=$((PASS+1))
    echo "ok   service survived stress (pid ${NEWPID:-on-demand})"
fi

if [ -n "$NEWPID" ]; then
    FD_AFTER=$(fd_count "$NEWPID")
    echo "fds: before=$FD_BEFORE after=$FD_AFTER"
    if [ "${FD_AFTER:-0}" -gt $(( ${FD_BEFORE:-0} + 50 )) ]; then
        FAIL=$((FAIL+1))
        echo "FAIL fd count grew by more than 50 during stress"
    else
        PASS=$((PASS+1))
        echo "ok   no fd leak"
    fi
fi

# the service must still answer after everything above
check still-alive getAllLocationHandlers '{}' '"returnValue":true'

echo "=== result: $PASS ok, $FAIL failed ==="
[ "$FAIL" -eq 0 ]
