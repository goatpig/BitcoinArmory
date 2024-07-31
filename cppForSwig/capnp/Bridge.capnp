@0x98fa84da458428ed;

# using Cxx = import "/capnp/c++.capnp";
# $Cxx.namespace("Armory::Codec::Bridge");

## RestoreWallet messages
struct RestorePrompt {
   struct WalletId {
      walletId          @0 : Text;
      backupType        @1 : UInt32;
   }

   union {
      checkWalletId     @0 : WalletId;
      getPassphrases    @1 : Void;
      decryptError      @2 : Void;
      typeError         @3 : Text;
      checksumError     @4 : List(Int32);
      checksumMismatch  @5 : List(Int32);
   }
}

struct RestoreReply {
   success @0 : Bool;
   control @1 : Text;
   privkey @2 : Text;
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
   scraddr        @12 : List(Data);
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

###############################
# CALLBACKS
###############################

struct CallbackPush {
   struct Ready {
      height @0 : UInt32;
   }

   # Structs below were empty in the original BridgeProto.proto
   struct SetupDone {}
   struct Disconnected {}

   struct Registered {
      id @0 : List(Text);
   }

   struct Refresh {
      id @0 : List(Text);
   }

   struct NewBlock {
      height @0 : UInt32;
   }

   struct Progress {
      phase             @0 : UInt32;
      progress          @1 : Float32;
      etaSec            @2 : UInt32;
      progressNumeric   @3 : UInt32;

      id                @4 : List(Text);
   }

   struct ZeroConf {
      ledger @0 : Ledger;
   }

   struct UnlockRequest {
      encryptionKeyIds @0 : List(Text);
   }

   callbackId  @0 : Text;
   referenceId @1 : UInt32;

   pushPayload :union {
      ready          @2 : Ready;
      setupDone      @3 : SetupDone;
      registered     @4 : Registered;
      refresh        @5 : Refresh;
      newBlock       @6 : NewBlock;
      disconnected   @7 : Disconnected;
      progress       @8 : Progress;
      nodeStatus     @9 : NodeStatus;
      zeroConf       @10 : ZeroConf;
      error          @11 : Text;
      cleanup        @12 : Bool;
      unlockRequest  @13 : UnlockRequest;

   }
}

###############################
# REPLIES
###############################

struct BlockchainServiceReply {
   struct FeeEstimate {
      feeByte  @0 : Float32;
      smartFee @1 : Bool;
   }

   struct LedgerHistory {
      ledger @0 : List(Ledger);
   }

   struct Tx {
      raw         @0 : Data;
      height      @1 : UInt32;
      txIndex     @2 : UInt32;
      rbf         @3 : Bool;
      chainedZc   @4 : Bool;
   }

   reply :union {
      blockTime         @0 : UInt32;
      ledgerDelegateId  @1 : Text;
      headerData        @2 : Data;
      feeEstimate       @3 : FeeEstimate;
      ledgerHistory     @4 : LedgerHistory;
      tx                @5 : Tx;
      nodeStatus        @6 : NodeStatus;
   }
}

struct WalletReply {
   struct UtxoList {
      utxo @0 : List(Utxo);
   }

   struct AddressBook {
      struct AddressBookEntry {
         srcAddr @0 : Data;
         txHash  @1 : Data;
      }

      address @0 : List(AddressBookEntry);
   }

   # Wallet Data
   struct AddressData {
      id                @0 : Int32;
      addrType          @1 : UInt32;
      isUsed            @2 : Bool;
      isChange          @3 : Bool;
      assetId           @4 : Data;
      hasPrivKey        @5 : Bool;
      useEncryption     @6 : Bool;

      prefixedHash      @7 : Data;
      publicKey         @8 : Data;
      precursorScript   @9 : Data;

      addressString     @10 : Text;
   }

   struct WalletData {
      id                   @0 : Text;
      useCount             @1 : Int64;
      lookupCount          @2 : Int64;
      watchingOnly         @3 : Bool;
      addressType          @4 : List(UInt32);
      defaultAddressType   @5 : UInt32;
      useEncryption        @6 : Bool;
      kdfMemReq            @7 : UInt32;

      label                @8 : Text;
      desc                 @9 : Text;

      addressData          @10 : List(AddressData);

      struct Comment {
         key @0 : Data;
         val @1 : Data;
      }

      comments             @11 : List(Comment);
   }

   struct MultipleWalletData {
      wallet @0 : List(WalletData);
   }

   struct BackupString {
      rootClear   @0 : List(Text);
      chainClear  @1 : List(Text);

      rootEncr    @2 : List(Text);
      chainEncr   @3 : List(Text);

      spPass      @4 : Text;
   }

   # Address Balance
   struct AddressBalanceData {
      id       @0 : Data;
      balance  @1 : BalanceAndCount;
   }

   struct AddressAndBalanceData {
      balance        @0 : List(AddressBalanceData);
      updatedAsset   @1 : List(AddressData);
   }

   struct BalanceAndCount {
      full        @0 : UInt64;
      spendable   @1 : UInt64;
      uncomfirmed @2 : UInt64;
      count       @3 : UInt64;
   }

   reply :union {
      highestUsedIndex        @0 : Int32;
      coinSelectionId         @1 : Text;
      ledgerDelegateId        @2 : Text;
      balanceAndCount         @3 : BalanceAndCount;
      addressAndBalanceData   @4 : AddressAndBalanceData;
      utxoList                @5 : UtxoList;
      addressBook             @6 : AddressBook;

      addressData             @7 : AddressData;
      walletData              @8 : WalletData;
      multipleWallets         @9 : MultipleWalletData;
      backupString            @10 : BackupString;
   }
}

struct CoinSelectionReply {
   struct UtxoList {
      utxo @0 : List(Utxo);
   }

   reply :union {
      flatFee        @0 : UInt64;
      feeByte        @1 : Float32;
      sizeEstimate   @2 : UInt32;
      utxoList       @3 : UtxoList;
   }
}

struct SignerReply {
   struct InputSignedState {
      isValid     @0 : Bool;
      m           @1 : UInt32;
      n           @2 : UInt32;

      sigCount    @3 : UInt32;

      struct PubKeySignatureState {
         pubKey   @0 : Data;
         hasSig   @1 : Bool;
      }

      signState   @4 : List(PubKeySignatureState);
   }
}

struct UtilsReply {
   reply :union {
      randomHex         @0 : Text;
      addressTypeName   @1 : Text;
      walletId          @2 : Text;
      hash              @3 : Data;
      srcAddr           @4 : Data;
   }
}

struct ScriptUtilsReply {
   reply :union {
      txInScriptType    @0 : UInt32;
      txOutScriptType   @1 : UInt32;
      addressString     @2 : Text;
      srcAddr           @3 : Data;
      pushData          @4 : Data;
      scriptData        @5 : Data;
   }
}

struct Reply {
   success        @0 : Bool;
   referenceId    @1 : UInt32;
   error          @2 : Text;

   replyPayload :union {
      service        @3 : BlockchainServiceReply;
      wallet         @4 : WalletReply;
      coinSelection  @5 : CoinSelectionReply;
      signer         @6 : SignerReply;
      utils          @7 : UtilsReply;
      scriptUtils    @8 : ScriptUtilsReply;
   }
}

struct Payload {
   union {
      reply    @0 : Reply;
      callback @1 : CallbackPush;
   }
}