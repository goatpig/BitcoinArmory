# BIP150KeyManager
`BIP150KeyManager` a binary compiled alongside `ArmoryDB`. The binary is required only by people who wish to use ArmoryDB as a server (fullnode or supernode). Recall that [BIP 150](BIP150_151.md) is a protocol that allows clients and servers to authenticate each other via ECDSA (secp256k1). The client and server have "identity keys" that are presented to the other side. There needs to be a way to manage the keys in a secure manner. BIP150KeyManager is a binary that can be used to manage the keys.

Users who simply wish to use the Armory wallet frontend need not read any further. The information is only for advanced users.

## Options
The following command line options are used by BIP150KeyManager.

* datadir - Set the directory where the relevant file resides. (Default: The same directory where BIP150KeyManager resides.)
* server - Use the `server.peers` file. This option or the *client* option must be chosen, but not both.
* client - Use the `client.peers` file. This option or the *server* option must be chosen, but not both.
* show-my-key - Display the compressed client/server identity public key.
* show-keys - Show the compressed identity public keys that will be used when authenticating the other side. Will be displayed as a hex string.
* add-key - Add an identity public key that will be used when authenticating the other side. The key must be a hex string, and the key may be compressed or uncompressed. The keys will be stored in their compressed form.

## Details
By default, ArmoryDB generates a wallet file called `server.peers`. Among other things, the file contains the relevant BIP 150 identity keys: The server keys (public & private) and any keys the server will use for authentication (public). Clients will have the `client.peers` file, which is essentially the same file type.

## License
Distributed under the MIT License. See the [LICENSE file](LICENSE) for more information.

## Copyright
Copyright (C) 2019 goatpig
