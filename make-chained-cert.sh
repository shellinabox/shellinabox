#!/bin/bash -e

tmp=/tmp/make-chained-cert.$$
trap 'echo; tput bel; echo FAILURE; rm -rf "${tmp}"; exit 1' EXIT INT TERM QUIT
mkdir -p "${tmp}/demoCA/newcerts"
printf '%08x' $$ >"${tmp}/demoCA/serial"
touch "${tmp}/demoCA/index.txt"
cd "${tmp}"

openssl req -nodes -new -x509 -keyout "${tmp}/ca-key.pem"                     \
            -out "${tmp}/ca-cert.pem" -days 7300                              \
            -subj "/CN=Demo CA/" 2>/dev/null

openssl x509 -in "${tmp}/ca-cert.pem" -out "${tmp}/ca-cert.crt" 2>/dev/null

openssl req -nodes -new -keyout /dev/stdout                                   \
            -out "${tmp}/ssl-req.pem" -days 7300 -subj "/CN=$(hostname -f)/"  \
            2>/dev/null | cat

openssl ca -batch -keyfile "${tmp}/ca-key.pem" -cert "${tmp}/ca-cert.crt"     \
           -notext -policy policy_anything -days 7300 -out /dev/stdout        \
           -infiles "${tmp}/ssl-req.pem" 2>/dev/null | cat
cat "${tmp}/ca-cert.crt"

trap 'rm -rf "${tmp}"' EXIT INT TERM QUIT

exit 0
