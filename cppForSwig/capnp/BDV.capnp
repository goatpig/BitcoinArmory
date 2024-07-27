@0x833fa1ae387540de;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("Armory::Codec::BDV");

using Types = import "Types.capnp";

using BdvId = Text;
using Cookie = Text;
using WalletId = Text;
using LedgerId = Text;

##### statics #####
struct ChainStatus
{
   enum ChainState {
      unknown  @0;
      syncing  @1;
      ready    @2;
   }

   chainState  @0 : ChainState;
   blockSpeed  @1 : Float32;
   progress    @2 : Float32;
   eta         @3 : UInt64;
   blocksLeft  @4 : UInt32;
}

struct NodeStatus {
   enum NodeState
   {
      offline  @0;
      online   @1;
      offSync  @2;
   }

   enum RpcState
   {
      disabled @0;
      badAuth  @1;
      online   @2;
      error28  @3;
   }

   node        @0 : NodeState;
   rpc         @1 : RpcState;
   isSW        @2 : Bool;
   chain       @3 : ChainStatus;
}

struct FeeSchedule {
   target   @0 : UInt32;
   feeByte  @1 : Float32;
   smartFee @2 : Bool;
}

struct StaticRequest {
   magicWord               @0 : Text;
   cookie                  @1 : Cookie;


   union {
      unset                @2 : Void;

      register             @3 : Void;
      unregister           @4 : BdvId;
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

      register             @1 : BdvId;
      unregister           @2 : Void;
      shutdown             @3 : Void;
      shutdownNode         @4 : Void;
      getNodeStatus        @5 : NodeStatus;
      getFeeSchedule       @6 : List(FeeSchedule);
      getTopBlockHeight    @7 : UInt32;
      getHeadersByHash     @8 : List(Types.Header);
      getHeadersByHeight   @9 : List(Types.Header);
   }
}

##### bdv #####
struct BalanceAndCount {
   full        @0 : UInt64;
   spendable   @1 : UInt64;
   unconfirmed @2 : UInt64;
   txnCount    @3 : UInt32;
}

struct AddressBook {
   struct Entry {
      scrAddr  @0 : Data;
      txHashes @1 : List(Types.Hash);
   }

   entries     @0 : List(Entry);
}

struct BdvRequest {
   struct RegisterWalletRequest {
      walletId                   @0 : Text;
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

   bdvId                         @0 : BdvId;

   union {
      unset                      @1 : Void;

      registerWallet             @2 : RegisterWalletRequest;
      unregisterWallet           @3 : WalletId;
      goOnline                   @4 : Void;

      getLedgerDelegate          @5 : Void;
      getTxByHash                @6 : List(Types.Hash);
      getOutputsForOutpoints     @7 : OutpointRequest;
      getOutputsForAddress       @8 : List(Address);
      updateWalletsLedgerFilter  @9 : List(WalletId);
      createAddressBook          @10: Void;
   }
}

struct BdvReply {
   union {
      unset                      @0 : Void;

      registerWallet             @1 : Void;
      unregisterWallet           @2 : Void;
      goOnline                   @3 : Void;

      getLedgerDelegate          @4 : LedgerId;
      getTxByHash                @5 : List(Types.Tx);
      getOutputsForOutpoints     @6 : List(Types.Output);
      getOutputsForAddress       @7 : List(Types.Output);
      updateWalletsLedgerFilter  @8 : Void;
      createAddressBook          @9 : AddressBook;
   }
}

##### ledgers #####
struct PageRequest {
   from  @0 : UInt32;
   to    @1 : UInt32;
}

struct HistoryPage {
   struct LedgerEntry {
      balance        @0 : Int64;

      txHeight       @1 : UInt32;
      txHash         @2 : Types.Hash;
      txOutIndex     @3 : UInt16;
      txTime         @4 : UInt32;

      isCoinbase     @5 : Bool;
      isChangeBack   @6 : Bool;
      isSTS          @7 : Bool;
      isOptInRBF     @8 : Bool;
      isChainedZC    @9 : Bool;
      isWitness      @10: Bool;

      walletId       @11: Text;
      scrAddrs       @12: List(Data);
   }

   ledgers           @0 : List(LedgerEntry);
}

struct LedgerRequest {
   ledgerId             @0 : LedgerId;

   union {
      unset             @1 : Void;

      getPageCount      @2: Void;
      getHistoryPages   @3: PageRequest;
   }
}

struct LedgerReply {
   union {
      unset             @0 : Void;

      getPageCount      @1 : UInt32;
      getHistoryPages   @2 : List(HistoryPage);
   }
}

##### wallets #####
struct TxoutRequest {
   spendable   @0 : Bool;
   rbf         @1 : Bool;
   zc          @2 : Bool;
   targetValue @3 : UInt64;
}

struct Address {
   prefix   @0 : UInt8;
   body     @1 : Data;
}

struct WalletRequest {
   bdvId                   @0 : BdvId;
   walletId                @1 : WalletId;

   union {
      unset                @2 : Void;

      getLedgerDelegate    @3 : Void;
      createAddressBook    @4 : Void;
      getBalanceAndCount   @5 : Void;
      getOutputs           @6 : TxoutRequest;
      setConfTarget        @7 : UInt32;
      unregisterAddresses  @8 : List(Address);
   }
}

struct WalletReply {
   union {
      unset                @0 : Void;

      getLedgerDelegate    @1 : LedgerId;
      createAddressBook    @2 : AddressBook;
      getBalanceAndCount   @3 : BalanceAndCount;
      getOutputs           @4 : List(Types.Output);
      setConfTarget        @5 : Void;
      unregisterAddresses  @6 : Void;
   }
}

##### addresses #####
struct AddressRequest {
   address                 @0 : Address;
   bdvId                   @1 : BdvId;

   union {
      unset                @2 : Void;

      getLedgerDelegate    @3 : Void;
      getBalanceAndCount   @4 : Void;
      getOutputs           @5 : TxoutRequest;
   }
}

struct AddressReply {
   union {
      unset                @0 : Void;

      getLedgerDelegate    @1 : LedgerId;
      getBalanceAndCount   @2 : BalanceAndCount;
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