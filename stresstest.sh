#!/bin/bash -e

PORT=4201
PIDFILE=stresstest.pid

rm -f "${PIDFILE}"
trap '[ -r "${PIDFILE}" ] && kill "$(cat "${PIDFILE}")"; rm -f "${PIDFILE}"'  \
     EXIT INT TERM QUIT HUP

./shellinaboxd -p "${PORT}" -s "/:$(id -u):$(id -g):${PWD}:/bin/bash -c       \
               'while read i; do [ -z "${i}" ] && break; echo \" $i\"; done'" \
               --background="${PIDFILE}"

session() {
  local data="$(wget -O- --post-data='' --quiet "http://localhost:${PORT}/")"
  local session="${data##*\"session\":\"}"
  session="${session%%\"*}"
  while read -r i; do
    local keys="$(echo -n "${i}" | od -tx1 -An -w1000)"
    wget -O/dev/null --post-data="session=${session}&keys=${keys// /}"        \
                     --quiet "http://localhost:${PORT}/"
    kill -0 "$(cat "${PIDFILE}")" || break
    [ -z "$i" ] && break
    data="$(wget -O- --post-data="session=${session}"                         \
                 --quiet "http://localhost:${PORT}/")"
    data=${data##*\"data\":\"}
    data=${data%%\"*}
    echo "${data}"
  done <<'EOF'
Hello world
This is a test
OK, that's it for now

EOF
}

pids=""
for i in `seq 100`; do
  session &
  pids="${pids} $!"
  sleep 0.02
  kill -0 "$(cat "${PIDFILE}")" || break
done
wait $pids >&/dev/null
