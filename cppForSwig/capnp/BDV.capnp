@0x833fa1ae387540de;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("Armory::Codec::BDV");
using Types = import "Types.capnp";

##### statics: do not need a ready BDM #####
struct StaticRequest {
   magicWord               @0 : Text;

   union {
      unset                @1 : Void;

      register             @2 : Void;
      unregister           @3 : Void;
      start                @4 : Void;
      shutdown             @5 : Void;
      getNodeStatus        @6 : Void;
   }
}

struct StaticReply {
   union {
      unset                @0 : Void;

      getNodeStatus        @1 : Types.NodeStatus;
   }
}

##### chain: need a ready BDM, rejected until then #####
struct ChainRequest {
   union {
      unset                @0 : Void;

      getFeeSchedule       @1 : Text;
      getTopBlockHeight    @2 : Void;
      getHeadersByHeight   @3 : List(UInt32);
      getHeadersById       @4 : List(UInt32);
      broadcast            @5 : List(Data);
      rpcBroadcast         @6 : Data;
   }
}

struct ChainReply {
   union {
      unset                @0 : Void;

      getFeeSchedule       @1 : List(Types.FeeSchedule);
      getTopBlockHeight    @2 : UInt32;
      getHeadersByHeight   @3 : List(Types.Header);
      getHeadersById       @4 : List(Types.Header);
   }
}

##### bdv #####
struct BdvRequest {
   struct RegisterWalletRequest {
      walletId                   @0 : Types.WalletId;
      isNew                      @1 : Bool;
      addresses                  @2 : List(Address);
   }

   struct OutpointRequest {
      struct Body {
         txHash      @0 : Types.Hash;
         outpointIds @1 : List(UInt16);
      }

      withZc         @0 : Bool;
      outpoints      @1 : List(Body);
   }

   struct AddressOutputsRequest {
      addresses      @0 : List(Address);
      heightCutoff   @1 : UInt32;
      zcCutoff       @2 : UInt32;
   }

   union {
      unset                      @0 : Void;

      registerWallet             @1 : RegisterWalletRequest;
      unregisterWallet           @2 : Types.WalletId;
      goOnline                   @3 : Void;

      getTxsByHash               @4 : List(Types.Hash);
      getTxsByKey                @5 : List(Types.TxKey);
      getTxios                   @6 : UInt32;
   }
}

struct BdvReply {
   struct AddressOutputReply {
      struct AddressOutputs {
         addr        @0 : Address;
         outputs     @1 : List(Types.Output);
      }

      heightCutoff   @0 : UInt32;
      zcCutoff       @1 : UInt32;
      addresses      @2 : List(AddressOutputs);
   }

   union {
      unset                      @0 : Void;

      registerWallet             @1 : Void;
      unregisterWallet           @2 : Void;
      goOnline                   @3 : Void;

      getTxsByHash               @4 : List(Types.Tx);
      getTxsByKey                @5 : List(Types.Tx);
      getTxios                   @6 : List(Types.TxioPair);
   }
}

##### wallets #####
struct TxoutRequest {
   targetValue @0 : UInt64;
   zc          @1 : Bool;
   rbf         @2 : Bool;
}

struct Address {
   prefix   @0 : UInt8;
   body     @1 : Data;
}

struct WalletRequest {
   walletId                @0 : Types.WalletId;

   union {
      unset                @1 : Void;
      unregisterAddresses  @2 : List(Address);
   }
}

struct WalletReply {
   union {
      unset                @0 : Void;
      unregisterAddresses  @1 : Void;
   }
}

##### main request/reply #####
struct Request {
   msgId       @0 : UInt64;

   union {
      static   @1 : StaticRequest;
      bdv      @2 : BdvRequest;
      wallet   @3 : WalletRequest;
      chain    @4 : ChainRequest;
   }
}

struct Reply {
   msgId       @0 : UInt64;
   success     @1 : Bool;
   error       @2 : Text;

   union {
      static   @3 : StaticReply;
      bdv      @4 : BdvReply;
      wallet   @5 : WalletReply;
      chain    @6 : ChainReply;
   }
}

##### notifications #####
struct Notification {

   struct ServerError {
      code              @0 : Int32;
      errStr            @1 : Text;
      errData           @2 : Data;
   }

   struct Refresh {
      type              @0 : UInt32;
      ids               @1 : List(Text);
   }

   requestId @0 : Text;
   union {
      registrationDone  @1 : Void;
      connectionIsReady @2 : Types.NewBlockNotif;
      newBlock          @3 : Types.NewBlockNotif;
      zc                @4 : List(Types.TxioPair);
      invalidatedZc     @5 : List(Data);
      refresh           @6 : Refresh;
      nodeStatus        @7 : Types.NodeStatus;
      progress          @8 : Types.ScanProgress;
      error             @9 : ServerError;
   }
}

struct Notifications {
   notifs @0 : List(Notification);
}