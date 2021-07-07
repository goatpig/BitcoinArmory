# ArmoryDB
ArmoryDB is an executable binary used by Armory. The code is written in C++11 and is used to control the [LMDB](http://symas.com/mdb/) databases used by Armory. These databases contain information about the wallets/lockboxes in Armory and information about transactions relevant to the wallets. ArmoryDB may be remotely accessible if properly accessed by relevant infrastructure.

ArmoryDB is automatically called whenever Armory starts up; the user needs not intervene. Only advanced users who wish to know more should read any further.

## ArmoryDB usage
ArmoryDB works by reading the blockchain downloaded by Bitcoin Core and finding any transactions relevant to the wallets loaded into Armory. This means that the entire blockchain must be rescanned whenever a new wallet or lockbox is loaded. Once a wallet/lockbox has been loaded and the blockchain fully scanned for that wallet, ArmoryDB will keep an eye on the blockchain. Any transactions relevant to the addresses controlled by wallets/lockboxes will be resolved. In addition, as Armory builds its own mempool by talking to the Core node, any relevant zero-confirmation transactions will be resolved by ArmoryDB.

As of v0.96.5, Armory calls ArmoryDB using a particular set of flags. The Armory Python log (`armorylog.txt`) shows attempts at executing ArmoryDB using some default parameters, assuming that `/home/snakamoto` is the user's root directory.

```
2019-01-13 13:32:17 (WARNING) -- SDM.py:396 - Spawning DB with command: /home/snakamoto/Armory/ArmoryDB --db-type="DB_FULL" --cookie --satoshi-datadir="/home/snakamoto/.bitcoin/blocks" --datadir="/home/snakamoto/Armory/" --dbdir="/home/snakamoto/Armory/databases"
2019-01-13 13:32:17 (INFO) -- ArmoryUtils.py:679 - Executing popen: ['/home/snakamoto/Armory/ArmoryDB', '--db-type="DB_FULL"', '--cookie', '--satoshi-datadir="/home/snakamoto/.bitcoin/blocks"', '--datadir="/home/snakamoto/Armory/"', '--dbdir="/home/snakamoto/Armory/databases"']
```

The flags are explained below, as seen in the Armory source code. By default, ArmoryDB works on the mainnet network.

* cookie: Create a cookie file holding a random authentication key to allow local clients to make use of elevated commands (e.g., `shutdown`). (Default: False)
* datadir: Path to the Armory data folder. (Default: Same as Armory)
* db-type: Sets the db type. Database type cannot be changed in between Armory runs. Once a database has been built with a certain type, the database will always function according to the initial type; specifying another type will do nothing. Changing the database type requires rebuilding the database. (Default: DB\_FULL)
* dbdir: Path to folder containing the Armory database file directory. If empty, a new database will be created. (Default: Same as Armory)
* satoshi-datadir: Path to blockchain data folder (blkXXXXX.dat files). (Default: Same as Armory)

The database types are as follows:

* DB\_BARE: Tracks wallet history only. Smallest DB, as the DB doesn't resolve a wallet's relevant transaction hashes until requested. (In other words, DB accesses will be relatively slow.) This was the default database type in Armory v0.94.
* DB\_FULL: Tracks wallet history and resolves all relevant transaction hashes. (In other words, the database can instantly pull up relevant transaction data). ~1GB minimum size for the database. Default database type as of v0.95.
* DB\_SUPER: Tracks the entire blockchain history. Any transaction hash can be instantly resolved into its relevant data. Among other things, this means rescans won't be necessary after making changes to one's wallets.

There are additional flags.

* checkchain: A test mode of sorts. It checks all the signatures in the blockchain. (Default: False)
* clear\_mempool: Delete all zero confirmation transactions from the database. (Default: False)
* listen-all: Listen to all incoming IPs (not just localhost). (Default: False)
* listen-port: Sets the database listening port. The database listens to external connections (e.g., from Armory) via [WebSocket](https://en.wikipedia.org/wiki/WebSocket) and can be placed behind an HTTP daemon in order to obtain remote access to ArmoryDB. (Default: 9001 (mainnet) / 19001 (testnet) / 19002 (regtest)) (TBD: Actual default WebSocket port is 7681. Need to update code?)
* ram-usage: Defines the RAM use during database scan operations. The base amount of RAM is ~400MB. One point is equal to ~128MB of RAM. Can't be lower than one point. Can be changed in between Armory runs. (Default: 50)
* rebuild: Delete all DB data, and build the database and scan the blockchain data from scratch.
* regtest: Run database against the regression test network.
* rescan: Delete all processed history data and rescan blockchain from the first block.
* rescanSSH: Delete balance and transaction count data, and rescan the data. Much faster than rescan or rebuild.
* satoshirpc-port: Set the P2P port of the Core node to which ArmoryDB will attempt to connect. (Default: Same as Armory)
* testnet: Run database against the testnet network.
* thread-count: Defines how many processing threads can be used during database builds and scans. Can't be lower than one thread. Can be changed in between Armory runs. (Default: The maximum number of available CPU threads.)
* zcthread-count: Defines the maximum number on threads the zero-confirmation (ZC) parser can create for processing incoming transcations from the Core network node. (Default: 100)

Note that the flags may be added to the Armory root data directory in an ArmoryDB config file (`armorydb.conf`). The file will set the parameters every time ArmoryDB is started. Command line flags, including flags used by Armory, will override config values. (Changing Armory's default values will require recompilation.) An example file that mirrors the default parameters used by Armory can be seen below.

```
db-type="DB_FULL"
cookie=1
satoshi-datadir="/home/snakamoto/.bitcoin/blocks""
datadir="/home/snakamoto/Armory/"
dbdir="/home/snakamoto/Armory/databases"
```

ArmoryDB works best on SSDs; Hard drives will struggle to function properly. An SSD is mandatory in supernode mode.

As always, check the source code for the most up-to-date information.

## ArmoryDB connection design
ArmoryDB *must* run alongside the Bitcoin Core node. This is because ArmoryDB does a memory map on the blockchain files. This can only be done if ArmoryDB and the node are running on the same OS and, ideally, on the same storage device. The IP address of the Core node is hardcoded (localhost) and can't be changed without recompiling Armory (and changing the design at your own risk!). Only the node's port can be changed via the `satoshirpc-port` parameter. This design may be changed in the future.

Because a memory map is done on the blockchain (an inherent design feature to LMDB), ArmoryDB can run only on 64-bit processors. Running ArmoryDB on a Raspberry Pi 3 may be possible for an enterprising hacker, although it's not recommended at all.

## Remote connections
It is possible for software to communicate with ArmoryDB remotely. Possibilities for reaching ArmoryDB include placing ArmoryDB behind an HTTP daemon/proxy or logging into the ArmoryDB machine remotely via a VPN. Talking to ArmoryDB is done via [WebSocket](https://en.wikipedia.org/wiki/WebSocket). [libwebsockets](https://libwebsockets.org/) is one library that can be used to build basic infrastructure for remote communications with ArmoryDB, and is the library used by Armory. Functionality includes but is not limited to:

* Submitting transactions to the Bitcoin network, including RBF and CPFP transactions.
* Getting information for a given Bitcoin address, including bech32 addresses.
* Getting information for a given Bitcoin transaction.
* Getting fees recommended by Bitcoin Core.

In other words, ArmoryDB can be used to power, among other things, a block explorer and other specialized online services. ArmoryDB can be directly user-facing, although this should *not* be done if ArmoryDB is controlling any funds. A more useful config is to keep ArmoryDB on a private network and have user-facing systems communicate with ArmoryDB as needed over internal connections.

Users can communicate with the remote ArmoryDB instance either by using Armory code directly or by writing their own code. See [this doc](???) for more info. (TO DO: ADD DOC) The advantage to using Armory source code is that all users need to do is set up the connection to the remote instance. However, the code is written in C++, which may not be feasible for all environments. Said environments require the user to find their own WebSocket libraries and to write the code necessary to make the appropriate calls to the remote ArmoryDB instance via [Protocol Buffer v2 messages](https://en.wikipedia.org/wiki/Protocol_Buffers).

Users should be aware of their usage model before choosing the remote ArmoryDB instance's database type. Users with a remote wallet may be okay with a full node instance and their loaded wallet. Users wishing to create public-facing services will probably wish to use the supernode mode. Note that only supernode mode is tested. Other modes are untested. In any mode other than supernode, communications will grind to a halt if attempting to interact with any addresses or transactions not in a wallet controlled by ArmoryDB.

### BIP 150/151
(TO DO - Talk about WebSocket and how BIP 150/151 is mandatory when establishing a remote connection.)

### Sample nginx config
[nginx](https://www.nginx.com/) is a web server that can handle WebSocket connections. This is one option to consider when setting up a remote connection. A sample config is included below. Note that this is a bare config designed solely to demonstrate WebSocket functionality. It should *not* be used in a production environment. **Use it only for test purposes.**

```
#user abc123;

worker_processes  1;

events {
    worker_connections  1024;
}

http {
    upstream ourtest {
        keepalive 1000;
        least_conn;
        server localhost:43924;
    }

    upstream ourmain {
        keepalive 10000;
        least_conn;
        server localhost:43923;
    }

    server {
        listen       80;
        server_name  pubsrv;

        # Websocket support.
        location / {
            root           /;
            proxy_pass http://ourmain;
            proxy_read_timeout    360;
            proxy_connect_timeout 360;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "";
        }
    }

    server {
        listen       81;
        server_name  pubtest;

        # Websocket support.
        location / {
            root           /;
            proxy_pass http://ourtest;
            proxy_read_timeout    360;
            proxy_connect_timeout 360;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "";
        }
    }
}
```

[This repo](https://github.com/trimstray/nginx-quick-reference) has some ideas for NGINX best practices. The Armory team does not endorse the link. It merely provides the link as a point of reference for those who wish to use Armory in a production environment.

## Supernode hardware requirements
The ArmoryDB supernode mode is very taxing on hardware. The following minimum specs are required.

* Processor: Intel i5 / AMD Ryzen with at least four cores (eight threads)
* RAM: 16GB
* Storage: 1TB SSD (SATA 3 or higher connection, or USB 3.x or Thunderbolt)

If possible, use a processor with more cores. Extra cores/threads will provide the greatest performance boost. Extra RAM will be very helpful too. The SSD R/W throughput should be as high as possible and installed internally on a SATA 3 or higher connection, although a dedicated USB 3.x or Thunderbolt connection will suffice.

The supernode database size will be ~1.05x the size the Bitcoin blockchain size. In Feb. 2019, the mainnet blockchain was ~230GiB. This meant the supernode DB would be ~242GiB. Therefore, a 1TB SSD is the minimum required storage device. If the blockchain continues to grow at its anticipated rate, and the supernode database design doesn't change in a significant manner, users will need to upgrade to a 2TB SSD around 2022.

If possible, avoid using a VM. While the supernode can run in a VM, the VM overhead will cause a small but noticable performance hit, especially on mainnet.

## Dependencies
* Clang (macOS) - Installed automatically by [Xcode](https://developer.apple.com/xcode/) or [Xcode Command Line Tools](https://developer.apple.com/library/archive/technotes/tn2339/_index.html)
* fcgi - Communication protocol (0.94-0.96) - [Source of goatpig's libfcgi fork](https://github.com/toshic/libfcgi)
* GNU Compiler Collection (Linux) - Install package `g++`
* LMDB - Database engine, modified to suit Armory's use cases - [LMDB page](http://symas.com/mdb/)
* Visual Studio compiler (Windows) - [Visual Studio page](https://www.visualstudio.com/)
* WebSocket - Communication protocol (0.97+) - [libwebsockets](https://libwebsockets.org/) is required to compile Armory. It is highly recommended that users compile a newer version of libwebsockets.

## Troubleshooting
Occasionally, a user may have trouble connecting to the Bitcoin Core node. Often, this is because a version of ArmoryDB from a previous run of Armory didn't shut down properly. If a user is unable to connect to the Core node, the following steps are recommended.

* Shut down Armory.
* Check the operating system's task manager 30-60 seconds later.
* If ArmoryDB is still running, shut it down manually.
* Restart Armory.

## License
Distributed under the MIT License. See the [LICENSE file](LICENSE) for more information.

## Copyright
Copyright (C) 2017-2019 goatpig
