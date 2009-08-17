#!/bin/bash

# This is a simple demo CGI script that illustrates how to use ShellInABox in
# CGI mode.

case "${REQUEST_METHOD}" in
  POST)
     # Retrieve CGI parameter, then start shellinabox with this command
     read parms
     parms="$(printf "$(echo "${parms}"|sed -e 's/%\(..\)/\\x\1/g;s/%/%%/g')")"
     parms="${parms#cmd=}"
     shellinaboxd --cgi -t -s "/:$(id -u):$(id -g):HOME:${parms}"
     ;;

  *) # First time that the CGI script was called. Show initial HTML page.
     printf 'Content-Type: text/html\r\n\r\n'
     cat <<EOF
     <html>
       <head>
         <title>Demo CGI for ShellInABox</title>
       </head>
       <body>
         <h1>Shell In A Box</h1>

         <p>This is a demo CGI script for ShellInABox. It shows how to execute
         ShellInABox in one-shot CGI mode.</p>

         <p>Please note that we do not implement any access controls. So, this
         script is unsafe to use on any untrusted network. It allows anybody
         on the Internet to run arbitrary commands on your computer!</p>

         <p>Use this as a template to write your own custom application -- and
         don't forget to add appropriate access controls.</p>

         <p>Enter command to run:
         <form method="POST">
           <input type="text" name="cmd" style="width: 40em" value="/bin/bash" />
         </form>
         </p>
       </body>
EOF
     ;;
esac
