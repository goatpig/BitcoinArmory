@0x833fa1ae387540de;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("Armory::Codec::BDV");
using Types = import "Types.capnp";

using Cookie = Text;

##### statics #####
struct StaticRequest {
   magicWord               @0 : Text;
   cookie                  @1 : Cookie;

   union {
      unset                @2 : Void;

      register             @3 : Void;
      unregister           @4 : Types.BdvId;
      shutdown             @5 : Void;
      shutdownNode         @6 : Void;
      getNodeStatus        @7 : Void;
      getFeeSchedule       @8 : Text;
      getTopBlockHeight    @9 : Void;
      getHeadersByHash     @10: List(Types.Hash);
      getHeadersByHeight   @11: List(UInt32);
      broadcast            @12: List(Data);
      rpcBroadcast         @13: Data;
   }
}

struct StaticReply {
   union {
      unset                @0 : Void;

      register             @1 : Types.BdvId;
      unregister           @2 : Void;
      shutdown             @3 : Void;
      shutdownNode         @4 : Void;
      getNodeStatus        @5 : Types.NodeStatus;
      getFeeSchedule       @6 : List(Types.FeeSchedule);
      getTopBlockHeight    @7 : UInt32;
      getHeadersByHash     @8 : List(Types.Header);
      getHeadersByHeight   @9 : List(Types.Header);
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

   bdvId                         @0 : Types.BdvId;

   union {
      unset                      @1 : Void;

      registerWallet             @2 : RegisterWalletRequest;
      unregisterWallet           @3 : Types.WalletId;
      goOnline                   @4 : Void;

      getLedgerDelegate          @5 : Void;
      getTxByHash                @6 : List(Types.Hash);
      getOutputsForOutpoints     @7 : OutpointRequest;
      getOutputsForAddress       @8 : AddressOutputsRequest;
      updateWalletsLedgerFilter  @9 : List(Types.WalletId);
      getCombinedBalances        @10: Void;
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

      getLedgerDelegate          @4 : Types.DelegateId;
      getTxByHash                @5 : List(Types.Tx);
      getOutputsForOutpoints     @6 : List(Types.Output);
      getOutputsForAddress       @7 : AddressOutputReply;
      updateWalletsLedgerFilter  @8 : Void;
      getCombinedBalances        @9 : List(Types.CombinedBalanceAndCount);
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
   bdvId                   @0 : Types.BdvId;
   walletId                @1 : Types.WalletId;

   union {
      unset                @2 : Void;

      getLedgerDelegate    @3 : Void;
      createAddressBook    @4 : Void;
      getBalanceAndCount   @5 : UInt32;
      getOutputs           @6 : TxoutRequest;
      setConfTarget        @7 : UInt32;
      unregisterAddresses  @8 : List(Address);
   }
}

struct WalletReply {
   union {
      unset                @0 : Void;

      getLedgerDelegate    @1 : Types.DelegateId;
      createAddressBook    @2 : Types.AddressBook;
      getBalanceAndCount   @3 : Types.BalanceAndCount;
      getOutputs           @4 : List(Types.Output);
      setConfTarget        @5 : Void;
      unregisterAddresses  @6 : Void;
   }
}

##### addresses #####
struct AddressRequest {
   address                 @0 : Address;
   bdvId                   @1 : Types.BdvId;

   union {
      unset                @2 : Void;

      getLedgerDelegate    @3 : Types.WalletId;
      getBalanceAndCount   @4 : Void;
      getOutputs           @5 : TxoutRequest;
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
   struct BlockData {
      height            @0 : UInt32;
      branchHeight      @1 : UInt32;
   }

   struct ServerError {
      code              @0 : Int32;
      errStr            @1 : Text;
      errData           @2 : Data;
   }

   struct Refresh {
      type              @0 : UInt32;
      ids               @1 : List(Data);
   }

   requestId @0 : Text;
   union {
      terminate         @1 : Void;
      continuePolling   @2 : Void;
      ready             @3 : BlockData;
      newBlock          @4 : BlockData;
      zc                @5 : Types.TxLedger;
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