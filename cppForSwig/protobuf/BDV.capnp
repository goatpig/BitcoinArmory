using BdvId = Text;
using Cookie = Text;
using WalletId = Text;
using LedgerId = Text;
using Hash = Text;
using Header = Data;
using Tx = Data;
using Output = Data;

##### statics #####
struct StaticRequest {
   magicWord               @0 : Text;
   cookie                  @1 : Cookie;


   union {
      unset                @2 : Void;

      register             @3 : Void;
      unregister           @4 : BdvId;
      shutdown             @5 : Void;
      shudownNode          @6 : Void;
      getNodeStatus        @7 : Void;
      getFeeSchedule       @8 : Void;
      getTopBlockHeight    @9 : Void;
      getHeadersByHash     @11: List(Hash);
      getHeadersByHeight   @12: List(UInt32);
   }
}

struct StaticReply {
   union {
      unset                @0 : Void;

      register             @1 : Void;
      unregister           @2 : Void;
      shutdown             @3 : Void;
      shutdownNode         @4 : Void;
      getNodeStatus        @5 : NodeStatus;
      getFeeSchedule       @6 : List(FeeSchedule);
      getTopBlockHeight    @7 : UInt32;
      getHeadersByHash     @8 : List(Header);
      getHeadersByHeight   @9 : List(Header);
   }
}

##### bdv #####
struct BdvRequest {
   bdvId                         @0 : BdvId;

   union {
      unset                      @1 : Void;

      registerWallet             @2 : List(Address);
      unregisterWallet           @3 : WalletId;
      goOnline                   @4 : Void;

      getLedgerDelegate          @5 : Void;
      getTxByHash                @6 : List(Hash);
      getOutputsForOutpoints     @7 : List(Outpoint);
      getOutpointsForAddress     @8 : List(Address);
      broadcast                  @9 : Data;
      rpcBroadcast               @10: Data;
      getBalanceAndCount         @11: Void;
      updateWalletsLedgerFilter  @12: List(WalletId);
      createAddressBook          @13: Void;
   }
}

struct BdvReply {
   union {
      unset                      @0 : Void;

      registerWallet             @1 : Void;
      unregisterWallet           @2 : Void;
      goOnline                   @3 : Void;

      getLedgerDelegate          @4 : LedgerId;
      getTxByHash                @5 : List(Tx);
      getOutputsForOutpoints     @6 : List(Output);
      getOutpointsForAddress     @7 : List(Outpoint);
      broadcast                  @8 : Void;
      rpcBroadcast               @9 : Void;
      getBalanceAndCount         @10: BalanceAndCount;
      updateWalletsLedgerFilter  @11: Void;
      createAddressBook          @12: AddressBook;
   }  
}

##### ledgers #####
struct PageRequest {
   from  @0 : UInt32;
   to    @1 : UInt32;
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

struct Outpoint {
   txHash   @0 : Hash;
   outputId @1 : UInt32;
}

struct Address {
   prefix   @0 : UInt8;
   body     @1 : Data;
}

struct WalletRequest {
   walletId                @0 : WalletId;

   union {
      unset                @1 : Void;

      getLedgerDelegate    @2 : Void;
      createAddressBook    @3 : Void;
      getBalanceAndCount   @4 : Void;
      getTxoutList         @5 : TxoutRequest;
      setConfTarget        @6 : UInt32;
      unregisterAddress    @7 : Address;
   }
}

struct WalletReply {
   union {
      unset                @0 : Void;

      getLedgerDelegate    @1 : LedgerId;
      createAddressBook    @2 : AddressBook;
      getBalanceAndCount   @3 : BalanceAndCount;
      getTxOutList         @4 : List(Output);
      setConfTarget        @5 : Void;
      unregisterAddress    @6 : Void;
   }
}

##### addresses #####
struct AddressRequest {
   address                 @0 : Address;

   union {
      unset                @1 : Void;

      getLedgerDelegate    @2 : Void;
      getBalanceAndCount   @3 : Void;
      getTxoutList         @4 : TxoutRequest;
   }
}

struct AddressReply {
   union {
      unset                @0 : Void;

      getLedgerDelegate    @1 : LedgerId;
      getBalanceAndCount   @2 : BalanceAndCount;
      getTxoutList         @3 : List(Output);
   }
}

##### main request/reply #####
struct Request {
   msgId       @0 : UInt64;

   union {
      static   @1 : StaticRequest;
      bdv      @2: BdvRequest;
      wallet   @3 : WalletRequest;
      address  @4 : AddressRequest;
      ledger   @5 : LedgerRequest;
   }
}

struct Reply {
   msgId @0 : UInt64;
   success @1 : Bool;
   error @2 : Text;

   union {
      static @3 : StaticReply;
      bdv @4 : BdvReply;
      wallet @5 : WalletReply;
      address @6 : AddressReply;
      ledger @7 : LedgerReply;
   }
}