
shellinabox
===========

This is unofficial fork of project **shellinabox**. Fork was created because
original project is not maintained anymore and we cannot contact original
repository owners.

Our aim is to continue with maintanince of shellinabox project. For list of
recent changes please see [CHANGELOG.md](/CHANGELOG.md).

If you have any questions, issues or patches, please fell free to submit pull
request or report an issue. You can also drop an email to original project
[issue #261](https://code.google.com/p/shellinabox/issues/detail?id=261) discusion
from where this fork started.


About shellinabox
-----------------

Shell In A Box implements a web server that can export arbitrary command line
tools to a web based terminal emulator. This emulator is accessible to any
JavaScript and CSS enabled web browser and does not require any additional
browser plugins.

![Shell In A Box preview](/misc/preview.png?raw=true)

More information:

* [Official site](https://code.google.com/p/shellinabox)
* [Official wiki](https://code.google.com/p/shellinabox/wiki/shellinaboxd_man)


Build
-----------------

For building **shellianbox** from source on Debian based systems use commands listed
below. This will create executable file `shellinaboxd` in project directory.

1. Install dependencies

   ```
    apt-get install git dpkg-dev debhelper autotools-dev libssl-dev libpam0g-dev zlib1g-dev libssl1.0.0 libpam0g
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

For building and installing `.deb` packages you can use commands listed bellow.

1. Build package

    ```
    dpkg-buildpackage -b
    ```

2. Install package

    ```
    dpkg -i ../shellianbox_{ver}_{arch}.deb
    ```

For more information about `.deb` packages please see [INSTALL.Debian](/INSTALL.Debian) file.

Issues
-----------------

All reported issues were imported from [Google Code Project Issues](https://code.google.com/p/shellinabox/issues/list).
You can report new issues here, but first please try to reproduce them with package
created from our sources.

