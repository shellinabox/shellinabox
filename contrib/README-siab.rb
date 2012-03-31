Reported by bXXX.rXXX@oXXX.com, Mar 11, 2011

I found the need to have a nice start / stop / restart wrapper for
shellinabox, and a quick way to add new configurations and URLS.
Attached is a file that does just that.

It is ruby based, and expects a conf directory in the shellinthebox
home directory.  All file(s) placed in the conf directory will become
a URL for shellinthebox, and the contents of the file(s) will be the
command run.

Ex : conf/nethack    -> http://localhost:4200/nethack
/conf/nethack contents -> /usr/games/nethack

When you run the wrapper script (siab.rb) it will start up shellinabox
and the url will be active and you can play nethack in shellinabox.

It has been great for tossing in files with other services and
restarting shellinabox and testing.  (siab.rb restart)

There can certainly be more done, and bulletproofing but, I though you
might be able to use it.

Brad
