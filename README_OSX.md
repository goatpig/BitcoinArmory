# macOS (OS X) README
## Requirements
Armory will run on macOS 10.8 and beyond. It is highly recommended to install Armory on the newest version of macOS that is feasible, as Apple often introduces changes to newer versions that make support of older versions difficult, if not impossible.

Due to [Python](https://python.org/) requirements that can't be met by default on macOS, users must use `brew` to install an updated version of OpenSSL in order for Armory to run. Please follow these instructions.

1. Open a terminal and run the `brew` command. If `brew` is not found, please execute the following commands from the terminal (aka command line). If the final command returns any errors, consult Google, the *bitcoin-armory* IRC channel on Freenode, or the [Bitcoin Forum](https://bitcointalk.org/index.php?board=97.0) for further instructions.

        /usr/bin/ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
        touch ~/.bashrc
        echo "export CFLAGS=\"-arch x86_64\"" >> ~/.bashrc
        echo "export ARCHFLAGS=\"-arch x86_64\"" >> ~/.bashrc
        source ~/.bashrc
        brew update
        brew doctor

2. If everything is successful, execute the following command.

        brew install openssl

You may now run Armory.

## macOS-specific Bugs
Armory developers make a best effort to ensure that all Armory features are available on all versions of Armory. Unfortunately, there are rare cases where the macOS version is missing features or has bugs not found elsewhere. The following is a list of known bugs/issues. Please report any other bugs on the [Bitcoin Forum](https://bitcointalk.org/index.php?board=97.0) or on the *bitcoin-armory* IRC channel on Freenode.

- Due to unknown issues with multithreading on Qt 4 (a library Armory uses), there are rare instances where Armory may crash suddenly. The Armory team has done its best to mitigate the crashes, with very few users reporting any crashes at all. Armory developers themselves haven't experienced any such crashes since approximately June 2015, and no users have reported such issues since around that time.
- The "File Open" dialog under macOS Armory is very "dumb." This is due to an unknown Qt 4 bug that causes Armory to crash whenever a "native" dialog is opened. This means opening files in certain locations (e.g., thumb drives) is very difficult. The only consistent solutions are to copy files over to a location that can be opened, or to generate a [symbolic link](http://askubuntu.com/questions/600714/creating-a-symlink-from-one-folder-to-another-with-different-names) to the desired directory.
