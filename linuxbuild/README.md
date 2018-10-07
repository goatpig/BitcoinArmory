# Running Armory on Linux

Running Armory in Linux has proven to be quite easy. There are only a few dependencies, and they are all versionless, so there are no games you have to play with system library versions in order to get it to work.

## Verifying Source Code Authenticity

The latest stable version of Armory is always be tagged in the git repository by its version number, and signed with the [Armory signing key (4922589A)](http://keyserver.ubuntu.com/pks/lookup?search=goatpig). For releases 0.93.3 and earlier, the [old Armory signing key (98832223)](http://keyserver.ubuntu.com/pks/lookup?search=Alan+Reiner) was used.

Here’s how you download the Armory signing key directly into your keyring from the Ubuntu keyserver, and then verify the signature using `git tag -v`:

~~~ bash
$ gpg --recv-keys --keyserver keyserver.ubuntu.com 4922589A
gpg: requesting key 4922589A from hkp server keyserver.ubuntu.com
gpg: key 4922589A: public key "goatpig (Offline signing key for Armory releases) <moothecowlord@gmail.com>" imported
$ git tag -v v0.96
object a3d01aa72293f82c3ef3f1d29f93c83ad70099f4
type commit
tag v0.96
tagger goatpig <moothecowlord@gmail.com> 1493581585 +0200

v0.96
gpg: Signature made Sun 30 Apr 2017 03:46:25 PM EDT using RSA key ID 4922589A
gpg: Good signature from "goatpig (Offline signing key for Armory releases) <moothecowlord@gmail.com>"
~~~


The above example is specifically for checking the tag for version "v0.96", which may not be the latest version by the time you are reading these instructions.  You can replace it with the latest version number posted on our website. All releases can be viewed on the [github releases page](https://github.com/goatpig/BitcoinArmory/releases). All major releases are accompanied by a signed tag.

## Ubuntu Dependency Instructions

In Ubuntu, you are required to install some packages before attempting to run Armory. To do so, type the following line (omitting the dollar sign) into a terminal. This only needs to be done once:

    $ sudo apt-get install python2 swig libqtcore4 python-qt4 python-psutil

The following is required only if you wish to run the [armoryd](https://github.com/goatpig/armoryd) script at some point.

    $ sudo apt-get install python-twisted

You're all set! To launch Armory, type in a terminal in the BitcoinArmory directory:

~~~bash
$ python ArmoryQt.py
~~~
