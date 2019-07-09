
shellinabox
===========

[![Build Status](https://drone.io/github.com/shellinabox/shellinabox/status.png)](https://drone.io/github.com/shellinabox/shellinabox/latest)
[![Join the chat at https://gitter.im/shellinabox/shellinabox](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/shellinabox/shellinabox?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)


This is a fork of the project **Shell In A Box**. The fork was created because
the original project was not maintained by the authorized author.  He did provide some
early support for the Debian package.

Our aim is to continue with maintenance of the shellinabox project. For a list of
recent changes, please see [CHANGELOG.md](/CHANGELOG.md).

If you have any questions, issues, or patches, please feel free to submit a pull
request or report an issue. You can also drop an email to the original project
[issue #261](https://code.google.com/p/shellinabox/issues/detail?id=261) discussion
from where this fork started.


About shellinabox
-----------------

Shell In A Box implements a web server that can export arbitrary command line
tools to a web based terminal emulator. This emulator is accessible to any
JavaScript and CSS enabled web browser and does not require any additional
browser plugins.

![Shell In A Box preview](/misc/preview.gif?raw=true)

More information:

* [Manual page](https://github.com/shellinabox/shellinabox/wiki/shellinaboxd_man)
* [Official site](https://code.google.com/p/shellinabox)
* [Official wiki](https://code.google.com/p/shellinabox/wiki/shellinaboxd_man)


Build
-----------------

For building **shellinabox** from source on Debian or RHEL based systems use commands
listed below. This will create executable file `shellinaboxd` in project directory.

1. Install dependencies

   ```
    apt-get install git libssl-dev libpam0g-dev zlib1g-dev dh-autoreconf
   ```
   
   or
   
   ```
   yum install git openssl-devel pam-devel zlib-devel autoconf automake libtool
   ```

2. Clone source files and move to project directory

   ```
    git clone https://github.com/shellinabox/shellinabox.git && cd shellinabox
   ```

3. Run autotools in project directory

   ```
    autoreconf -i
   ```

4. Run configure and make in project directory

   ```
    ./configure && make
   ```

#### Debian package

For building and installing `.deb` packages you can use commands listed bellow.
Note that dependencies from the first step above are also required.

1. Build package

    ```
    dpkg-buildpackage -b
    ```

2. Install package

    ```
    dpkg -i ../shellinabox_{ver}_{arch}.deb
    ```

For more information about `.deb` packages please see [INSTALL.Debian](/INSTALL.Debian) file.

Issues
-----------------

All reported issues were imported from [Google Code Project Issues](https://code.google.com/p/shellinabox/issues/list).
You can report new issues here, but first please try to reproduce them with package
created from our sources. In new issue report please include following things:

* Name and version of your operating system
* Name and version of your browser
* Version of shellinabox
* Steps to reproduce the problem

Also feel free to post any questions or comments in [shellinabox chat room](https://gitter.im/shellinabox/shellinabox)
on Gitter.


Known Issues
------------

* The openssl package is required for HTTP/SSL support.
  Shell-in-a-box may be used without SSL such that the login session
  is not encrypted.  To enable automatic creation of self-signed
  certificates or to use a generated certificate, install openssl.

* On Debian Jessie, the default openssl package does not include the
  utilities necessary for Shell-in-a-box to generate self-signed
  certificates.  Upgrade openssl to install a version of the tools
  that support certificate creation.
