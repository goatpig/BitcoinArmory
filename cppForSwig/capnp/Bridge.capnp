@0x98fa84da458428ed;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("Armory::Codec::Bridge");

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