#!/bin/sh -e

DIRNAME=`dirname $0`
cd $DIRNAME
USAGE="$0 [ --update ]"
if [ `id -u` != 0 ]; then
echo 'You must be root to run this script'
exit 1
fi

if [ $# -eq 1 ]; then
	if [ "$1" = "--update" ] ; then
		time=`ls -l --time-style="+%x %X" shellinaboxd.te | awk '{ printf "%s %s", $6, $7 }'`
		rules=`ausearch --start $time -m avc --raw -se shellinaboxd`
		if [ x"$rules" != "x" ] ; then
			echo "Found avc's to update policy with"
			echo -e "$rules" | audit2allow -R
			echo "Do you want these changes added to policy [y/n]?"
			read ANS
			if [ "$ANS" = "y" -o "$ANS" = "Y" ] ; then
				echo "Updating policy"
				echo -e "$rules" | audit2allow -R >> shellinaboxd.te
				# Fall though and rebuild policy
			else
				exit 0
			fi
		else
			echo "No new avcs found"
			exit 0
		fi
	else
		echo -e $USAGE
		exit 1
	fi
elif [ $# -ge 2 ] ; then
	echo -e $USAGE
	exit 1
fi

echo "Building and Loading Policy"
set -x
make -f /usr/share/selinux/devel/Makefile shellinaboxd.pp || exit
/usr/sbin/semodule -i shellinaboxd.pp

# Generate a man page off the installed module
sepolicy manpage -p . -d shellinaboxd_t
# Fixing the file context on /usr/sbin/shellinaboxd
/sbin/restorecon -F -R -v /usr/sbin/shellinaboxd
# Fixing the file context on /usr/lib/systemd/system/shellinaboxd.service
/sbin/restorecon -F -R -v /usr/lib/systemd/system/shellinaboxd.service
# Fixing the file context on /var/lib/shellinabox
/sbin/restorecon -F -R -v /var/lib/shellinabox
