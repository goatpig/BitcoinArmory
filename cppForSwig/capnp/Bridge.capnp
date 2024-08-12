@0x98fa84da458428ed;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("Armory::Codec::Bridge");

using Types = import "Types.capnp";

###############################
# WalletData
###############################

struct WalletData {
   struct AddressData {
      index             @0 : Int32;
      addrType          @1 : UInt32;
      isUsed            @2 : Bool;
      isChange          @3 : Bool;
      assetId           @4 : Data;
      hasPrivKey        @5 : Bool;
      usesEncryption    @6 : Bool;

      prefixedHash      @7 : Types.Hash;
      publicKey         @8 : Data;
      precursorScript   @9 : Data;

      addressString     @10: Text;
   }

   struct Comment {
      key @0 : Data;
      val @1 : Text;
   }

   ##
   id                   @0 : Types.WalletId;
   useCount             @1 : Int64;
   lookupCount          @2 : Int64;
   watchingOnly         @3 : Bool;
   addressTypes         @4 : List(UInt32);
   defaultAddressType   @5 : UInt32;
   usesEncryption       @6 : Bool;
   kdfMemReq            @7 : UInt32;

   label                @8 : Text;
   desc                 @9 : Text;

   addressData          @10: List(AddressData);
   comments             @11: List(Comment);
}

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
# CALLBACKS
###############################

struct Notification {
   #callbackId is set if this notification is the result
   #of a RPC request that provided said id
   callbackId        @0 : Text;

   union {
      unset          @1 : Void;
      ready          @2 : Types.Height;
      setupDone      @3 : Void;
      registered     @4 : List(Text);
      refresh        @5 : List(Text);
      newBlock       @6 : Types.Height;
      disconnected   @7 : Void;
      progress       @8 : Types.ScanProgress;
      nodeStatus     @9 : Types.NodeStatus;
      zeroConfs      @10: Types.TxLedger;
      error          @11: Text;
      cleanup        @12: Void;
      unlockRequest  @13: List(Data);
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
# Blockchain Service
###############################

struct BlockchainServiceRequest {
   struct RegisterWallet {
      id    @0 : Text;
      isNew @1 : Bool;
   }

   struct HistoryPageRequest {
      delegateId  @0 : Types.DelegateId;
      pageId      @1 : UInt32;
   }

   struct HistoryForWalletSelection {
      walletId @0 : List(Text);
      order    @1 : Text;
   }

   union {
      unset                         @0 : Void;

      shutdown                      @1 : Void;
      setupDb                       @2 : Void;
      goOnline                      @3 : Void;
      getNodeStatus                 @4 : Types.NodeStatus;
      loadWallets                   @5 : Void;
      registerWallets               @6 : Void;

      registerWallet                @7 : RegisterWallet;
      broadcastTx                   @8 : List(Types.Tx);
      getTxsByHash                  @9 : List(Types.Hash);
      getHeadersByHeight            @10: List(Types.Height);
      getBlockTimeByHeight          @11: UInt32;
      getFeeSchedule                @12: Text;

      getLedgerDelegate             @13: Void;
      getDelegateForWalletSelection @14: HistoryForWalletSelection;
      updateWalletsLedgerFilter     @15: List(Types.WalletId);
   }
}

struct BlockchainServiceReply {
   struct TxData {
      raw         @0 : Data;
      hash        @1 : Types.Hash;
      height      @2 : Types.Height;
      txIndex     @3 : UInt32;
      rbf         @4 : Bool;
      chainedZc   @5 : Bool;
   }

   # reply
   union {
      unset                         @0 : Void;

      getNodeStatus                 @1 : Types.NodeStatus;
      loadWallets                   @2 : List(WalletData);
      getTxsByHash                  @3 : List(TxData);
      getHeadersByHeight            @4 : List(Types.Header);
      getBlockTimeByHeight          @5 : UInt32;
      getFeeSchedule                @6 : List(Types.FeeSchedule);
      getLedgerDelegate             @7 : Types.DelegateId;
      getDelegateForWalletSelection @8 : Types.DelegateId;
   }
}

###############################
# Wallet
###############################
struct WalletRequest {
   struct AddressRequest {
      union {
         new         @0 : Void;
         change      @1 : Void;
         peekChange  @2 : Void;
      }
   }

   struct ExtendAddressPool {
      count       @0 : UInt32;
      callbackId  @1 : Types.CallbackId;
   }

   struct SetAddressTypeFor {
      assetId       @0 : Data;
      addressType   @1 : UInt32;
   }

   struct OutputRequest {
      union {
         value @0 : Types.CoinAmount;
         zc    @1 : Void;
         rbf   @2 : Void;
      }
   }

   struct SetComment {
      hashKey @0 : Data;
      comment @1 : Text;
   }

   struct SetLabels {
      title        @0 : Text;
      description  @1 : Text;
   }

   id                               @0 : Types.WalletId;
   union {
      unset                         @1 : Void;

      getAddress                    @2 : AddressRequest;
      getHighestUsedIndex           @3 : Void;
      extendAddressPool             @4 : ExtendAddressPool;

      createBackupString            @5 : Types.CallbackId;
      delete                        @6 : Void;
      getData                       @7 : WalletData;

      getAddrCombinedList           @8 : Void;
      setAddressTypeFor             @9 : SetAddressTypeFor;

      getLedgerDelegateIdForSrcAddr @10: Types.ScrAddr;
      getBalanceAndCount            @11: Void;

      setupNewCoinSelectionInstance @12: Types.Height;
      getUtxos                      @13: OutputRequest;

      createAddressBook             @14: Void;
      setComment                    @15: SetComment;
      setLabels                     @16: SetLabels;
   }
}

####
struct WalletReply {
   struct BackupString {
      rootClear   @0 : List(Text);
      chainClear  @1 : List(Text);

      rootEncr    @2 : List(Text);
      chainEncr   @3 : List(Text);

      spPass      @4 : Text;
   }

   # Address Balance
   struct AddressBalanceData {
      scrAddr  @0 : Types.ScrAddr;
      balances @1 : Types.BalanceAndCount;
   }

   struct AddressAndBalanceData {
      balances       @0 : List(AddressBalanceData);
      updatedAssets  @1 : List(WalletData.AddressData);
   }

   # reply
   union {
      unset                         @0 : Void;

      getAddress                    @1 : WalletData.AddressData;
      getHighestUsedIndex           @2 : Int32;
      extendAddressPool             @3 : WalletData;
      createBackupString            @4 : BackupString;
      getData                       @5 : WalletData;
      getAddressCombinedList        @6 : AddressAndBalanceData;
      setAddressTypeFor             @7 : WalletData.AddressData;
      getLedgerDelegateIdForSrcAddr @8 : Types.DelegateId;
      getBalanceAndCount            @9 : Types.BalanceAndCount;
      setupNewCoinSelectionInstance @10: Text;
      getUtxos                      @11: List(Types.Output);
      createAddressBook             @12: Types.AddressBook;
   }
}

###############################
# Coin Selection
###############################

struct CoinSelectionRequest {
   struct SetRecipient {
      address  @0 : Text;
      value    @1 : UInt64;
      id       @2 : UInt32;
   }

   struct SelectUTXOs {
      flags       @0 : UInt32;
      union {
         flatFee  @1 : UInt64;
         feeByte  @2 : Float32;
      }
   }

   struct CustomUtxoList {
      utxos       @0 : List(Types.Output);
      flags       @1 : UInt32;
      union {
         flatFee  @2 : UInt64;
         feeByte  @3 : Float32;
      }
   }

   struct FeeForMaxVal {
      utxos    @0 : List(Types.Output);
      feeByte  @1 : Float32;
   }

   id @0 : Text;
   union {
      unset                   @1 : Void;

      cleanup                 @2 : Void;
      reset                   @3 : Void;

      setRecipient            @4 : SetRecipient;
      selectUtxos             @5 : SelectUTXOs;

      getUtxoSelection        @6 : Void;
      getFlatFee              @7 : Void;
      getFeeByte              @8 : Void;
      getSizeEstimate         @9 : Void;

      processCustomUtxoList   @10: CustomUtxoList;
      getFeeForMaxVal         @11: FeeForMaxVal;
   }
}

struct CoinSelectionReply {
   union {
      unset             @0 : Void;

      selectUtxos       @1 : List(Types.Output);
      getFlatFee        @2 : Types.CoinAmount;
      getFeeByte        @3 : Float32;
      getSizeEstimate   @4 : UInt32;
      getFeeForMaxVal   @5 : Types.CoinAmount;
   }
}


###############################
# Signer
###############################

struct SignerRequest {
   struct AddSpenderByOutpoint {
      hash     @0 : Types.Hash;
      txOutId  @1 : UInt32;
      sequence @2 : UInt32;
   }

   struct PopulateUtxo {
      hash     @0 : Types.Hash;
      script   @1 : Data;
      txOutId  @2 : UInt32;
      value    @3 : Types.CoinAmount;
   }

   struct AddRecipient{
      script   @0 : Data;
      value    @1 : Types.CoinAmount;
   }

   struct SignTx {
      walletId    @0 : Types.WalletId;
      callbackId  @1 : Text;
   }

   id                         @0 : Text;
   union {
      unset                   @1 : Void;

      getNew                  @2 : Void;
      cleanup                 @3 : Void;

      setVersion              @4 : UInt32;
      setLockTime             @5 : UInt32;

      addSpenderByOutpoint    @6 : AddSpenderByOutpoint;
      populateUtxo            @7 : PopulateUtxo;
      addRecipient            @8 : AddRecipient;

      toTxSigCollect          @9 : UInt32;
      fromTxSigCollect        @10: Text;

      signTx                  @11: SignTx;
      getSignedTx             @12: Void;
      getUnsignedTx           @13: Void;
      getSignedStateForInput  @14: UInt32;

      resolve                 @15: Types.WalletId;
      addSupportingTx         @16: Types.Tx;

      fromType                @17: Void;
      canLegacySerialize      @18: Void;
   }
}

struct SignerReply {
   struct InputSignedState {
      struct PubKeySignatureState {
         pubKey   @0 : Data;
         hasSig   @1 : Bool;
      }

      isValid     @0 : Bool;
      mCount      @1 : UInt32;
      nCount      @2 : UInt32;

      sigCount    @3 : UInt32;
      signStates  @4 : List(PubKeySignatureState);
   }

   union {
      unset                   @0 : Void;

      getNew                  @1 : Text;
      toTxSigCollect          @2 : Text;
      getSignedTx             @3 : Types.Tx;
      getUnsignedTx           @4 : Types.Tx;
      getSignedStateForInput  @5 : InputSignedState;
      fromType                @6 : UInt32;
      canLegacySerialize      @7 : Bool;
   }
}

###############################
# Utils
###############################

struct UtilsRequest {
   struct CreateWalletStruct
   {
      lookup            @0 : UInt32;
      passphrase        @1 : Text;
      controlPassphrase @2 : Text;
      extraEntropy      @3 : Data;

      label             @4 : Text;
      description       @5 : Text;
   }

   union {
      unset                @0 : Void;

      generateRandomHex    @1 : UInt32;
      getHash160           @2 : Data;
      getNameForAddrType   @3 : Int32;
      createWallet         @4 : CreateWalletStruct;
   }
}

struct UtilsReply {
   union {
      unset                @0 : Void;

      generateRandomHex    @1 : Text;
      getHash160           @2 : Types.Hash;
      getNameForAddrType   @3 : Text;
      createWallet         @4 : Types.WalletId;
   }
}

###############################
# Script Utils
###############################

struct ScriptUtilsRequest {
   script                        @0 : Data;

   union {
      unset                      @1 : Void;

      getTxInScriptType          @2 : Types.Hash;
      getTxOutScriptType         @3 : Void;
      getSrcAddrForScript        @4 : Void;
      getLastPushDataInScript    @5 : Void;
      getTxOutScriptForScrAddr   @6 : Types.ScrAddr;
      getAddrStrForScrAddr       @7 : Void;
      getScrAddrForAddrStr       @8 : Text;
   }
}

struct ScriptUtilsReply {
   union {
      unset                      @0 : Void;

      getTxInScriptType          @1 : UInt32;
      getTxOutScriptType         @2 : UInt32;
      getSrcAddrForScript        @3 : Types.ScrAddr;
      getLastPushDataInScript    @4 : Data;
      getTxOutScriptForScrAddr   @5 : Data;
      getAddrStrForScrAddr       @6 : Text;
      getScrAddrForAddrStr       @7 : Types.ScrAddr;
   }
}

###############################
# Ledger Delegates
###############################
struct LedgerDelegateRequest {
   id                @0 : Types.DelegateId;

   union {
      unset          @1 : Void;

      getPageCount   @2 : Void;
      getPages       @3 : Types.PageRequest;
   }
}

struct LedgerDelegateReply {
   union {
      unset          @0 : Void;

      getPageCount   @1 : UInt32;
      getPages       @2 : List(Types.TxLedger);
   }
}

###############################
# Request/Reply
###############################

struct ToBridge {
   referenceId @0 : UInt64;

   # method
   union {
      unset          @1 : Void;

      service        @2 : BlockchainServiceRequest;
      wallet         @3 : WalletRequest;
      coinSelection  @4 : CoinSelectionRequest;
      signer         @5 : SignerRequest;
      utils          @6 : UtilsRequest;
      scriptUtils    @7 : ScriptUtilsRequest;
      delegate       @8 : LedgerDelegateRequest;
   }
}

struct RpcReply {
   success           @0 : Bool;
   referenceId       @1 : UInt64;
   error             @2 : Text;

   # replyPayload
   union {
      unset          @3 : Void;

      service        @4 : BlockchainServiceReply;
      wallet         @5 : WalletReply;
      coinSelection  @6 : CoinSelectionReply;
      signer         @7 : SignerReply;
      utils          @8 : UtilsReply;
      scriptUtils    @9 : ScriptUtilsReply;
      delegate       @10: LedgerDelegateReply;
   }
}

struct FromBridge {
   union {
      unset          @0 : Void;

      reply          @1 : RpcReply;
      notification   @2 : Notification;
   }
}