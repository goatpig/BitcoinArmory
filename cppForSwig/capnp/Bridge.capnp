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

struct RestoreWalletPayload {
   root        @0 : List(Text);
   secondary   @1 : List(Text);
   spPass      @2 : Text;
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

   # pushPayload
   union {
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

struct CallbackReply
{
   success     @0 : Bool;
   referenceId @1 : UInt32;

   # Single oneof entry in BridgeProto.proto:
   passphrase  @2 : Text;
}

###############################
# REQUEST
###############################

struct BlockchainService {
   struct LoadWallets {
      callbackId @0 : Text;
   }
   struct RegisterWallet {
      id    @0 : Text;
      isNew @1 : Bool;
   }

   struct BroadcastTx {
      rawTx @0 : List(Data); 
   }
   struct GetTxByHash {
      txHash @0 : Data;
   }
   struct GetHeaderByHeight {
      height @0 : UInt32;
   }
   struct GetBlockTimeByHeight {
      height @0 : UInt32;
   }
   struct EstimateFee {
      blocks @0 : UInt32;
      strat  @1 : Text;
   }

   struct UpdateWalletsLedgerFilter {
      walletId @0 : List(Text);
   }

   struct GetHistoryPageForDelegate {
      delegateId  @0 : Text;
      pageId      @1 : UInt32;
   }

   struct GetHistoryForWalletSelection {
      walletId @0 : List(Text);
      order    @1 : Text;
   }

   
   union {
      shutdown             @0 : Bool;
      setupDb              @1 : Bool;
      goOnline             @2 : Bool;
      getNodeStatus        @3 : Bool;
      loadWallets          @4 : LoadWallets;
      registerWallets      @5 : Bool;

      registerWallet       @6 : RegisterWallet;
      broadcastTx          @7 : BroadcastTx;
      getTxByHash          @8 : GetTxByHash;
      getHeaderByHeight    @9 : GetHeaderByHeight;
      getBlockTimeByHeight @10 : GetBlockTimeByHeight;
      estimateFee          @11 : EstimateFee;

      getLedgerDelegateIdForWallets @12 : Bool;
      updateWalletsLedgerFilter     @13 : UpdateWalletsLedgerFilter;
      getHistoryPageForDelegate     @14 : GetHistoryPageForDelegate;
      getHistoryForWalletSelection  @15 : GetHistoryForWalletSelection;
   }
}

struct Wallet {
   struct GetNewAddress {
      type @0 : UInt32;
   }
   struct GetChangeAddress {
      type @0 : UInt32;
   }
   struct PeekChangeAddress {
      type @0 : UInt32;
   }

   struct ExtendAddressPool {
      count       @0 : UInt32;
      callbackId  @1 : Text;
   }

   struct SetAddressTypeFor {
      assetId       @0 : Data;
      addressType   @1 : UInt32;
   }

   struct GetLedgerDelegateIdForScrAddr {
      hash @0 : Data;
   }

   struct SetupNewCoinSelectionInstance {
      height @0 : UInt32;
   }
   struct GetUtxosForValue {
      value @0 : UInt64;
   }

   struct SetComment {
      hashKey @0 : Data;
      comment @1 : Text;
   }
   struct SetLabels {
      title        @0 : Text;
      description  @1 : Text;
   }

   struct CreateBackupString {
      callbackId @0 : Text;
   }

   
   id @0 : Text;
   # method
   union {
      getNewAddress                 @1 : GetNewAddress;
      getChangeAddress              @2 : GetChangeAddress;
      peekChangeAddress             @3 : PeekChangeAddress;

      getHighestUsedIndex           @4 : Bool;
      extendAddressPool             @5 : ExtendAddressPool;

      createBackupString            @6 : CreateBackupString;
      delete                        @7 : Bool;
      getData                       @8 : Bool;

      getAddrCombinedList           @9 : Bool;
      setAddressTypeFor             @10 : SetAddressTypeFor;

      getLedgerDelegateIdForScrAddr @11 : GetLedgerDelegateIdForScrAddr;
      getBalanceAndCount            @12: Bool;

      setupNewCoinSelectionInstance @13 : SetupNewCoinSelectionInstance;
      getUtxosForValue              @14 : GetUtxosForValue;
      getSpendableZcList            @15 : Bool;
      getRbfTxOutList               @16 : Bool;

      createAddressBook             @17 : Bool;
      setComment                    @18 : SetComment;
      setLabels                     @19 : SetLabels;
   }
}

struct CoinSelection {
   struct SetRecipient {
      address  @0 : Text;
      value    @1 : UInt64;
      id       @2 : UInt32;
   }

   struct SelectUTXOs {
      flags @0 : UInt32;
      # fee
   union {
         flatFee @1 : UInt64;
         feeByte @2 : Float32;
      }
   }

   struct ProcessCustomUtxoList {
      utxos @0 : List(Utxo);
      flags @1 : UInt32;
      # fee
   union {
            flatFee @2 : UInt64;
            feeByte @3 : Float32;
      }
   }

   struct GetFeeForMaxVal {
      utxos    @0 : List(Utxo);
      feeByte  @1 : Float32;
   }
   
   id @0 : Text;
   # method
   union {
      cleanup                 @1 : Bool;
      reset                   @2 : Bool;

      setRecipient            @3 : SetRecipient;
      selectUtxos             @4 : SelectUTXOs;

      getUtxoSelection        @5 : Bool;
      getFlatFee              @6 : Bool;
      getFeeByte              @7 : Bool;
      getSizeEstimate         @8 : Bool;

      processCustomUtxoList   @9 : ProcessCustomUtxoList;
      getFeeForMaxVal         @10 : GetFeeForMaxVal;
   }
}

struct Signer {
   struct SetVersion {
      version @0 : UInt32;
   }
   struct SetLockTime {
      lockTime @0 : UInt32;
   }

   struct AddSpenderByOutpoint {
      hash     @0 : Data;
      txOutId  @1 : UInt32;
      sequence @2 : UInt32;
   }

   struct PopulateUtxo {
      hash     @0 : Data;
      script   @1 : Data;
      txOutId  @2 : UInt32;
      value    @3 : UInt64;
   }

   struct AddRecipient{
      script   @0 : Data;
      value    @1 : UInt64;
   }

   struct ToTxSigCollect {
      ustxType @0 : UInt32;
   }
   struct FromTxSigCollect{
      txSigCollect @0 : Text;
   }

   struct SignTx {
      walletId    @0 : Text;
      callbackId  @1 : Text;
   }
   struct GetSignedStateForInput {
      inputId @0 : UInt32;
   }

   struct Resolve {
      walletId @0 : Text;
   }
   struct AddSupportingTx {
      rawTx @0 : Data;
   }

   id @0 : Text;
   # method
   union {
      getNew                  @1 : Bool;
      cleanup                 @2 : Bool;

      setVersion              @3 : SetVersion;
      setLockTime             @4 : SetLockTime;

      addSpenderByOutpoint    @5 : AddSpenderByOutpoint;
      populateUtxo            @6 : PopulateUtxo;
      addRecipient            @7: AddRecipient;

      toTxSigCollect          @8 : ToTxSigCollect;
      fromTxSigCollect        @9 : FromTxSigCollect;

      signTx                  @10 : SignTx;
      getSignedTx             @11 : Bool;
      getUnsignedTx           @12 : Bool;
      getSignedStateForInput  @13 : GetSignedStateForInput;

      resolve                 @14 : Resolve;
      addSupportingTx         @15 : AddSupportingTx;

      fromType                @16 : Bool;
      canLegacySerialize      @17 : Bool;
   }
}

struct Utils {
   struct GenerateRandomHex {
      length @0 : UInt32;
   }
   struct GetHash160 {
      data @0 : Data;
   }

   struct GetScrAddrForAddrStr {
      address @0 : Text;
   }
   struct GetNameForAddrType {
      addressType @0 : Int32;
   }

   struct CreateWalletStruct
   {
      lookup            @0 : UInt32;
      passphrase        @1 : Text;
      controlPassphrase @2 : Text;
      extraEntropy      @3 : Data;

      label             @4 : Text;
      description       @5 : Text;
   }

   # method
   union {
      generateRandomHex    @0 : GenerateRandomHex;
      getHash160           @1 : GetHash160;

      getScrAddrForAddrstr @2 : GetScrAddrForAddrStr;
      getNameForAddrType   @3 : GetNameForAddrType;

      createWallet         @4 : CreateWalletStruct;
   }
}

struct ScriptUtils {

   struct GetTxInScriptType {
      hash @0 : Data;
   }

   
   script @0 : Data;
   # method
   union {
      getTxinScriptType          @1 : GetTxInScriptType;
      getTxOutScriptType         @2 : Bool;

      getScrAddrForScript        @3 : Bool;
      getLastPushDataInScript    @4 : Bool;
      getTxOutScriptForScrAddr   @5 : Bool;
      getAddrstrForScrAddr       @6 : Bool;
   }
}


struct Request {
   referenceId @0 : UInt32;

   # method
   union {
      service        @1 : BlockchainService;
      wallet         @2 : Wallet;
      coinSelection  @3 : CoinSelection;
      signer         @4 : Signer;
      utils          @5 : Utils;
      scriptUtils    @6 : ScriptUtils;
      callbackReply  @7 : CallbackReply;

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

   # reply
   union {
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

   # reply
   union {
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

   # reply
   union {
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
   # reply
   union {
      randomHex         @0 : Text;
      addressTypeName   @1 : Text;
      walletId          @2 : Text;
      hash              @3 : Data;
      srcAddr           @4 : Data;
   }
}

struct ScriptUtilsReply {
   # reply
   union {
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

   # replyPayload
   union {
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