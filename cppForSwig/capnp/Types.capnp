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

###############################
# DATA TYPES
###############################

struct Ledger {
   value          @0 : Int64;
   hash           @1 : Data;
   id             @2 : Text;
   height         @3 : UInt32;
   txIndex        @4 : UInt32;
   txTime         @5 : UInt32;
   coinbase       @6 : Bool;
   sentToSelf     @7 : Bool;
   changeBack     @8 : Bool;
   chainedZc      @9 : Bool;
   witness        @10 : Bool;
   rbf            @11 : Bool;
   scrAddr        @12 : List(Data);
}

struct NodeStatus {
   struct NodeChainStatus {
      chainState  @0 : UInt32;
      blockSpeed  @1 : Float32;
      progressPct @2 : Float32;
      eta         @3 : UInt64;
      blocksLeft  @4 : UInt32;
   }

   isValid           @0 : Bool;
   nodeState         @1 : UInt32;
   isSegwitEnabled   @2 : Bool;
   rpcState          @3 : UInt32;
   chainStatus       @4 : NodeChainStatus;
}

struct Utxo {
   txHash      @0 : Data;
   txOutIndex  @1 : UInt32;

   value       @2 : UInt64;
   txHeight    @3 : UInt32;
   txIndex     @4 : UInt32;

   script      @5 : Data;
   scrAddr     @6 : Data;
}
