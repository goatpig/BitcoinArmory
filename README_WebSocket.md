# Armory and WebSocket
This document is a brief guide to setting up a connection to a remote ArmoryDB instance via a WebSocket interface.

## Server Information
TBD

## Client Information
The client side is straightforward. The "glue" is the AsyncClient::BlockDataViewer() class. Any communication with the remote ArmoryDB instance must go through this class. Any call that can be made to the BDV object will send a WebSocket message to the remote ArmoryDB instance. The call must include a function lambda. The lambda is what will actually process the returned data.

Note that the data will be returned as part of a ReturnMessage template object. A get() call is used to obtain the data from the ReturnMessage object. The idea behind the design is to allow errors reported by ArmoryDB to propagate back to the client via the try/catch mechanism. If no catch is necessary after the get() call, there were no errors on the remote ArmoryDB side.

Occasionally, the remote ArmoryDB instance will want to asynchronously send data to the client. An instance of the RemoteCallback class (or, more specifically, a class derived from it) is the class that will actually process the data. All possible actions are covered by the BDMAction class. Examples include new blocks and zero-confirmation transactions associated with a given wallet arriving.

## Miscellaneous
(Need to organize these thoughts)

The advantage to using Armory source code is that all users need to do is set up the connection to the remote instance. However, the code is written in C++, which may not be feasible for all environments. Said environments require the user to find their own WebSocket libraries and to write the code necessary to make the appropriate calls to the remote ArmoryDB instance via [Protocol Buffer v2 messages](https://en.wikipedia.org/wiki/Protocol_Buffers).

## License
Distributed under the MIT License. See the [LICENSE file](LICENSE) for more information.

## Copyright
Copyright (C) 2019 goatpig
