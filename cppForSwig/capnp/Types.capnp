@0x803fc46fed7846c4;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("Armory::Codec::Types");

## base types ##
using Hash              = Data;
using TxKey             = UInt64;
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
   thisHash    @0 : Hash;
   prevHash    @1 : Hash;
   timestamp   @2 : UInt32;
   blockSize   @3 : UInt32;
   numTxs      @4 : UInt32;
   blockId     @5 : UInt32;
   height      @6 : UInt32;
   mainBranch  @7 : Bool;
}

struct NewBlockNotif {
   height            @0 : UInt32;
   branchHeight      @1 : UInt32;
   invalidatedIds    @2 : List(UInt32);
   newMainBranchIds  @3 : List(UInt32);
}

## tx data ##
struct Output {
   value       @0 : CoinAmount;
   txHeight    @1 : Height;
   txIndex     @2 : UInt16;
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
   raw         @0 : Data;
   key         @1 : TxKey;
   isChainedZc @2 : Bool;
   isRbf       @3 : Bool;
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

      walletId       @10: WalletId;
      scrAddrs       @11: List(Data);
   }

   ledgers           @0 : List(LedgerEntry);
}

struct TxioPair {
   amount   @0 : UInt64;
   txOut    @1 : UInt64;
   txIn     @2 : UInt64;
   txTime   @3 : UInt32;
   scrAddr  @4 : Data;

   rbf      @5 : Bool;
   chained  @6 : Bool;
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
