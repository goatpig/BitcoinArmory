@0x833fa1ae387540de;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("Armory::Codec::BDV");
using Types = import "Types.capnp";

##### statics #####
struct StaticRequest {
   magicWord               @0 : Text;

   union {
      unset                @1 : Void;

      register             @2 : Void;
      unregister           @3 : Void;
      shutdown             @4 : Void;
      shutdownNode         @5 : Void;
      getNodeStatus        @6 : Void;
      getFeeSchedule       @7 : Text;
      getTopBlockHeight    @8 : Void;
      getHeadersByHash     @9 : List(Types.Hash);
      getHeadersByHeight   @10: List(UInt32);
      broadcast            @11: List(Data);
      rpcBroadcast         @12: Data;
   }
}

struct StaticReply {
   union {
      unset                @0 : Void;

      getNodeStatus        @1 : Types.NodeStatus;
      getFeeSchedule       @2 : List(Types.FeeSchedule);
      getTopBlockHeight    @3 : UInt32;
      getHeadersByHash     @4 : List(Types.Header);
      getHeadersByHeight   @5 : List(Types.Header);
   }
}

##### bdv #####
struct BdvRequest {
   enum WalletType {
      unset    @0;
      wallet   @1;
      lockbox  @2;
   }

   struct RegisterWalletRequest {
      walletId                   @0 : Types.WalletId;
      isNew                      @1 : Bool;
      addresses                  @2 : List(Address);
      walletType                 @3 : WalletType;
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

      getLedgerDelegate          @4 : Void;
      getTxsByHash               @5 : List(Types.Hash);
      getTxsByKey                @6 : List(Types.TxKey);
      getOutputsForOutpoints     @7 : OutpointRequest;
      getOutputsForAddress       @8 : AddressOutputsRequest;
      updateWalletsLedgerFilter  @9 : List(Types.WalletId);
      getCombinedBalances        @10: Void;
      getTxios                   @11: UInt32;
      getBlockTimestamps         @12: List(UInt32);
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

   struct BlockTimestamp {
      height         @0 : UInt32;
      timestamp      @1 : UInt32;
   }

   union {
      unset                      @0 : Void;

      registerWallet             @1 : Void;
      unregisterWallet           @2 : Void;
      goOnline                   @3 : Void;

      getLedgerDelegate          @4 : Types.DelegateId;
      getTxsByHash               @5 : List(Types.Tx);
      getTxsByKey                @6 : List(Types.Tx);
      getOutputsForOutpoints     @7 : List(Types.Output);
      getOutputsForAddress       @8 : AddressOutputReply;
      updateWalletsLedgerFilter  @9 : Void;
      getCombinedBalances        @10: List(Types.CombinedBalanceAndCount);
      getTxios                   @11: List(Types.TxioPair);
   }
}

##### ledgers #####
struct LedgerRequest {
   ledgerId             @0 : Types.DelegateId;

   union {
      unset             @1 : Void;

      getPageCount      @2: Void;
      getHistoryPages   @3: Types.PageRequest;
   }
}

struct LedgerReply {
   union {
      unset             @0 : Void;

      getPageCount      @1 : UInt32;
      getHistoryPages   @2 : List(Types.TxLedger);
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

      getLedgerDelegate    @2 : Void;
      getBalanceAndCount   @3 : UInt32;
      getOutputs           @4 : TxoutRequest;
      setConfTarget        @5 : UInt32;
      unregisterAddresses  @6 : List(Address);
   }
}

struct WalletReply {
   union {
      unset                @0 : Void;

      getLedgerDelegate    @1 : Types.DelegateId;
      getBalanceAndCount   @2 : Types.BalanceAndCount;
      getOutputs           @3 : List(Types.Output);
      setConfTarget        @4 : Void;
      unregisterAddresses  @5 : Void;
   }
}

##### addresses #####
struct AddressRequest {
   address                 @0 : Address;

   union {
      unset                @1 : Void;

      getLedgerDelegate    @2 : Types.WalletId;
      getBalanceAndCount   @3 : Void;
      getOutputs           @4 : TxoutRequest;
   }
}

struct AddressReply {
   union {
      unset                @0 : Void;

      getLedgerDelegate    @1 : Types.DelegateId;
      getBalanceAndCount   @2 : Types.BalanceAndCount;
      getOutputs           @3 : List(Types.Output);
   }
}

##### main request/reply #####
struct Request {
   msgId       @0 : UInt64;

   union {
      static   @1 : StaticRequest;
      bdv      @2 : BdvRequest;
      wallet   @3 : WalletRequest;
      address  @4 : AddressRequest;
      ledger   @5 : LedgerRequest;
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
      address  @6 : AddressReply;
      ledger   @7 : LedgerReply;
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
      terminate         @1 : Void;
      continuePolling   @2 : Void;
      ready             @3 : Types.NewBlockNotif;
      newBlock          @4 : Types.NewBlockNotif;
      zc                @5 : List(Types.TxioPair);
      invalidatedZc     @6 : List(Data);
      refresh           @7 : Refresh;
      nodeStatus        @8 : Types.NodeStatus;
      progress          @9 : Types.ScanProgress;
      error             @10: ServerError;
   }
}

struct Notifications {
   notifs @0 : List(Notification);
}