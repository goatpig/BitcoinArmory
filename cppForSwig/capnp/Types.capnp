@0x803fc46fed7846c4;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("Armory::Codec::Types");

## base types ##
using Hash              = Data;
using TxKey             = Data;
using ScrAddr           = Data;

using WalletId          = Text;
using AccountId         = Text;
using SignerId          = Text;
using DelegateId        = Text;
using BdvId             = UInt64;
using CallbackId        = Text;
using CoinSelectionId   = Text;

using Height      = UInt32;
using CoinAmount  = UInt64;

## block data ##
struct Header {
   rawData     @0 : Data;
   height      @1 : UInt32;
   dupId       @2 : UInt8;
}

struct NewBlockNotif {
   height            @0 : UInt32;
   branchHeight      @1 : UInt32;
}

## tx data ##
struct Output {
   value       @0 : CoinAmount;
   txHeight    @1 : Height;
   txIndex     @2 : UInt32;
   txOutIndex  @3 : UInt16;
   txHash      @4 : Hash;
   script      @5 : Data;
   spenderHash @6 : Hash;
}

struct Outpoint {
   txHash      @0 : Hash;
   index       @1 : UInt16;
}

struct Tx {
   body        @0 : Data;
   height      @1 : UInt32;
   dupId       @2 : UInt8;
   index       @3 : UInt32;
   isChainZc   @4 : Bool;
   isRbf       @5 : Bool;
}

## bitcoin node & db status ##
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
      offsync  @2;
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

struct ScanProgress {
   phase             @0 : UInt32;
   progress          @1 : Float32;
   time              @2 : UInt32;
   numericProgress   @3 : UInt32;
   ids               @4 : List(Text);
}

struct FeeSchedule {
   target   @0 : UInt32;
   feeByte  @1 : Float32;
   smartFee @2 : Bool;
}

## ledgers ##
struct PageRequest {
   #these are page ids
   first @0 : UInt32;
   last  @1 : UInt32;
}

struct TxLedger {
   struct LedgerEntry {
      balance        @0 : Int64;

      txHeight       @1 : Height;
      txHash         @2 : Hash;
      txOutIndex     @3 : UInt16;
      txTime         @4 : UInt32;

      isCoinbase     @5 : Bool;
      isChangeBack   @6 : Bool;
      isSTS          @7 : Bool;
      isOptInRBF     @8 : Bool;
      isChainedZC    @9 : Bool;
      isWitness      @10: Bool;

      walletId       @11: WalletId;
      scrAddrs       @12: List(Data);
   }

   ledgers           @0 : List(LedgerEntry);
}

struct TxioPair {
   amount   @0 : UInt64;
   txOut    @1 : Data;
   txIn     @2 : Data;
   txTime   @3 : UInt32;

   fromSelf @4 : Bool;
   coinbase @5 : Bool;
   rbf      @6 : Bool;
   multisig @7 : Bool;
}

## balances ##
struct BalanceAndCount {
   full        @0 : CoinAmount;
   spendable   @1 : CoinAmount;
   unconfirmed @2 : CoinAmount;
   txnCount    @3 : UInt32;
}

## address book ##
struct AddressBook {
   struct Entry {
      scrAddr  @0 : ScrAddr;
      txHashes @1 : List(Hash);
   }

   entries     @0 : List(Entry);
}

struct CombinedBalanceAndCount {
   struct AddressBalances {
      scrAddr  @0 : ScrAddr;
      balances @1 : BalanceAndCount;
   }

   id          @0 : WalletId;
   balances    @1 : BalanceAndCount;
   addresses   @2 : List(AddressBalances);
}