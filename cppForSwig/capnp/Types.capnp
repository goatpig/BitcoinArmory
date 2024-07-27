@0x803fc46fed7846c4;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("Armory::Codec::Types");

using Hash     = Data;
using Header   = Data;
using Tx       = Data;

struct Output {
   value       @0 : UInt64;
   txHeight    @1 : UInt32;
   txIndex     @2 : UInt32;
   txOutIndex  @3 : UInt16;
   txHash      @4 : Hash;
   script      @5 : Data;
}

struct Outpoint {
   txHash      @0 : Hash;
   index       @1 : UInt16;
}