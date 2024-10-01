@0x9d2a278145f7bab5;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("Armory::Codec::Signer");

using Types = import "Types.capnp";


enum StackItemType
{
   undefined   @0;
   pushData    @1;
   opCode      @2;
   singleSig   @3;
   multiSig    @4;
   script      @5;
}

struct StackItemSingleSig {
   script      @0 : Data;
   pubkey      @1 : Data;
}

struct StackItemMultiSig {
   struct SigData {
      index @0 : UInt32;
      sig   @1 : Data;
   }

   script      @0 : Data;
   sigData     @1 : List(SigData);
}

struct StackItem {
   type              @0 : StackItemType;
   id                @1 : UInt32;

   union {
      stackData      @2 : Data;
      opCode         @3 : UInt8;
      multiSigData   @4 : StackItemMultiSig;
      singleSigData  @5 : StackItemSingleSig;
   }
}

struct BIP32PublicRoot {
   xpub        @0 : Text;
   fingerprint @1 : UInt32;
   path        @2 : List(UInt32);
}

struct PubkeyBIP32Path {
   pubkey      @0 : Data;
   fingerprint @1 : UInt32;
   path        @2 : List(UInt32);
}

struct ScriptSpender {
   versionMax     @0 : UInt32;
   versionMin     @1 : UInt32;

   legacyStatus   @2 : UInt8;
   segwitStatus   @3 : UInt8;
   sigHashType    @4 : UInt32;
   sequence       @5 : UInt32;

   isP2sh         @6 : Bool;
   isCsv          @7 : Bool;
   isCltv         @8 : Bool;

   union {
      utxo        @9 : Types.Output;
      outpoint    @10 : Types.Outpoint;
   }

   sigScript      @11 : Data;
   witnessData    @12 : Data;

   legacyStack    @13 : List(StackItem);
   witnessStack   @14 : List(StackItem);

   bip32Paths     @15 : List(PubkeyBIP32Path);
}

struct Recipient {
   script      @0 : Data;
   groupId     @1 : UInt32;
   bip32Paths  @2 : List(PubkeyBIP32Path);
}

struct Signer {
   flags          @0 : UInt32;
   txVersion      @1 : UInt32;
   locktime       @2 : UInt32;

   spenders       @3 : List(ScriptSpender);
   recipients     @4 : List(Recipient);
   supportingTxs  @5 : List(Data);
   bip32Roots     @6 : List(BIP32PublicRoot);
}