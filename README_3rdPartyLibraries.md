# Third-party libraries in Armory

[Armory](https://github.com/goatpig/BitcoinArmory) uses several third-party libraries in order to function properly. Information on all libraries is included here.

## Libraries (git subtree)

- [bech32](https://github.com/sipa/bech32) - There are no tagged versions.  Armory currently uses the code as of [commit bdc7167](https://github.com/sipa/bech32/tree/bfc716715b5d7d853526cc6b43ff36388aaeb1c2) (committed on Sep. 18, 2017.)
- [libbtc](https://github.com/libbtc/libbtc) - There are no tagged versions. Armory currently uses the code as of [commit dc34a7a](https://github.com/libbtc/libbtc/tree/dc34a7a32a28011a386fb3e5e1712c6285567df3) (committed on May 11, 2018.)
- [libsecp256k1](https://github.com/bitcoin-core/secp256k1) - Included as part of libbtc, and used directly by Armory as necessary. There are no tagged versions. Armory currently uses the code as of [commit 7a49cac](https://github.com/bitcoin-core/secp256k1/tree/7a49cacd3937311fcb1cb36b6ba3336fca811991) (committed on Aug. 4, 2016.)
- [libwebsockets](https://libwebsockets.org/) - (Code has a commit from the master branch but should probably be redone to use a tagged branch. Fix later.)

## Libraries (direct commit to Armory)
- [Crypto++](https://www.cryptopp.com/) - A customized version of v5.6.2, with [RFC 6979](https://tools.ietf.org/html/rfc6979) added to enable [deterministic signatures](https://bitcointalk.org/index.php?topic=522428.0). (NB: The plan is for this to be removed from Armory eventually, possibly in v0.98.)
- [Google Test](https://github.com/google/googletest/) - Unknown version. Added to Armory in Oct. 2013.
- [LMDB](https://symas.com/lmdb/) - Unknown version. Added to Armory in Apr. 2014.

## Library verification

Note that Armory is moving to a model where subtrees are added directly from external repos. This allows users to confirm that the libraries were added with no modifications. Users may download Bitcoin Core, copy the file contrib/devtools/git-subtree-check.sh into the Armory root directory, and run the script on the directory to verify. An example can be decoded as follows.

- The first line indicates that the subdirectory is actually a ["tree"](https://git-scm.com/book/en/v2/Git-Internals-Git-Objects), with a SHA-1 hash of [some tree-related data](https://stackoverflow.com/questions/14790681/what-is-the-internal-format-of-a-git-tree-object).
- The second line lists the commit to Armory where the tree was last updated.
- The third line lists the latest commit from the original tree when the tree was added to Armory.
- The fourth line, if the script executes successfully, will be "GOOD".
- If, at any point, the script fails, an error message will be seen.

```
./git-subtree-check.sh cppForSwig/libbtc
cppForSwig/libbtc in HEAD currently refers to tree 00d775c3c26cada7e2c63e5c3690f52bf01cc320
cppForSwig/libbtc in HEAD was last updated in commit 2d7d7a4f886863bf75d5b9316647d3f269aae058 (tree 00d775c3c26cada7e2c63e5c3690f52bf01cc320)
cppForSwig/libbtc in HEAD was last updated to upstream commit dc34a7a32a28011a386fb3e5e1712c6285567df3 (tree 00d775c3c26cada7e2c63e5c3690f52bf01cc320)
GOOD
```
## Copyright

Copyright (C) 2018 goatpig
