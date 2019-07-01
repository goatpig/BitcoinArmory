////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "WalletManager.h"

#ifdef _WIN32
#include "leveldb_windows_port\win32_posix\dirent_win32.h"
#else
#include "dirent.h"
#endif

using namespace std;

PythonSigner::~PythonSigner()
{}

////////////////////////////////////////////////////////////////////////////////
////
//// WalletManager
////
////////////////////////////////////////////////////////////////////////////////
void WalletManager::loadWallets()
{
   auto getBDVLambda = [this](void)->SwigClient::BlockDataViewer&
   {
      return this->getBDVObj();
   };

   //list .lmdb files in folder
   DIR *dir;
   dir = opendir(path_.c_str());
   if (dir == nullptr)
   {
      LOGERR << "invalid datadir path";
      throw runtime_error("invalid datadir path");
   }

   vector<string> walletPaths;

   struct dirent *ent;
   while ((ent = readdir(dir)) != nullptr)
   {
      auto dirname = ent->d_name;
      if (strlen(dirname) > 5)
      {
         auto endOfPath = ent->d_name + strlen(ent->d_name) - 5;
         if (strcmp(endOfPath, ".lmdb") == 0)
         {
            stringstream ss;
            ss << path_ << "/" << dirname;
            walletPaths.push_back(ss.str());
         }
      }
   }

   closedir(dir);

   unique_lock<mutex> lock(mu_);
   
   cout << ".loading wallets" << endl;
   //read the files
   for (auto& wltPath : walletPaths)
   {
      try
      {
         auto wltPtr = AssetWallet::loadMainWalletFromFile(wltPath);
         WalletContainer wltCont(wltPtr->getID(), getBDVLambda);
         wltCont.wallet_ = wltPtr;

         wallets_.insert(make_pair(wltPtr->getID(), wltCont));
         cout << ".loaded wlt " << wltPtr->getID() << endl;
      }
      catch (exception& e)
      {
         stringstream ss;
         ss << "Failed to open wallet with error:" << endl << e.what();
         LOGERR << ss.str();
         cout << ss.str() << endl;
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::duplicateWOWallet(
   const SecureBinaryData& pubRoot,
   const SecureBinaryData& chainCode,
   unsigned chainLength)
{
   cout << "-duplicating wallet" << endl;
   auto root = pubRoot;
   auto cc = chainCode;

   auto newWO = AssetWallet_Single::createFromPublicRoot_Armory135(
      path_, root, cc, chainLength);

   auto getBDVLambda = [this](void)->SwigClient::BlockDataViewer&
   {
      return this->getBDVObj();
   };

   WalletContainer wltCont(newWO->getID(), getBDVLambda);
   wltCont.wallet_ = newWO;

   unique_lock<mutex> lock(mu_);
   wallets_.insert(make_pair(newWO->getID(), wltCont));
   cout << "-done duplicating wallet " << newWO->getID() << endl;
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::synchronizeWallet(const string& id, unsigned chainLength)
{
   WalletContainer* wltCtr;

   {
      unique_lock<mutex> lock(mu_);

      auto wltIter = wallets_.find(id);
      if (wltIter == wallets_.end())
         throw runtime_error("invalid id");

      wltCtr = &wltIter->second;
   }

   auto wltSingle = dynamic_pointer_cast<AssetWallet_Single>(wltCtr->wallet_);
   if (wltSingle == nullptr)
      throw runtime_error("invalid wallet ptr");

   wltSingle->extendPublicChainToIndex(
      wltSingle->getMainAccountID(), chainLength);
}

////////////////////////////////////////////////////////////////////////////////
SwigClient::BlockDataViewer& WalletManager::getBDVObj(void)
{
   if (!bdv_.isValid())
      throw runtime_error("bdv object is not valid");

   return bdv_;
}

////////////////////////////////////////////////////////////////////////////////
WalletContainer& WalletManager::getCppWallet(const string& id)
{
   unique_lock<mutex> lock(mu_);

   auto wltIter = wallets_.find(id);
   if (wltIter == wallets_.end())
   {
      cout << "failed to grab wallet id: " << id << endl;
      cout << "have the following ids:" << endl;
      for(auto& wallet : wallets_)
      {
         cout << wallet.first << endl;
         if(strcmp(wallet.first.c_str(), id.c_str()) == 0)
         {   
            cout << "match!" << endl;
            return wallet.second;
         }
      }

      throw runtime_error("invalid id");
   }

   return wltIter->second;
}

////////////////////////////////////////////////////////////////////////////////
////
//// WalletContainer
////
////////////////////////////////////////////////////////////////////////////////
void WalletContainer::registerWithBDV(bool isNew)
{
   reset();

   auto wltSingle = dynamic_pointer_cast<AssetWallet_Single>(wallet_);
   if (wltSingle == nullptr)
      throw runtime_error("invalid wallet ptr");

   auto addrSet = wltSingle->getAddrHashSet();
   auto& bdv = getBDVlambda_();

   //convert set to vector
   vector<BinaryData> addrVec;
   addrVec.insert(addrVec.end(), addrSet.begin(), addrSet.end());

   auto&& swigWlt = bdv.instantiateWallet(wltSingle->getID());
   swigWlt.registerAddresses(addrVec, isNew);

   swigWallet_ = make_shared<SwigClient::BtcWallet>(swigWlt);
}

////////////////////////////////////////////////////////////////////////////////
int WalletContainer::detectHighestUsedIndex()
{
   int topIndex = 0;
   for (auto addrCountPair : countMap_)
   {
      auto& addr = addrCountPair.first;
      auto& ID = wallet_->getAssetIDForAddr(addr);
      auto asset = wallet_->getAssetForID(ID.first);
      if (asset->getIndex() > topIndex)
         topIndex = asset->getIndex();
   }

   return topIndex;
}

////////////////////////////////////////////////////////////////////////////////
////
//// CoinSelectionInstance
////
////////////////////////////////////////////////////////////////////////////////
CoinSelectionInstance::CoinSelectionInstance(
   WalletContainer* const walletContainer, 
   const vector<AddressBookEntry>& addrBook, unsigned topHeight) :
   cs_(getFetchLambdaFromWalletContainer(walletContainer), addrBook,
      walletContainer->spendableBalance_, topHeight),
   walletPtr_(walletContainer->wallet_),
   spendableBalance_(walletContainer->spendableBalance_)
{}

////////////////////////////////////////////////////////////////////////////////
CoinSelectionInstance::CoinSelectionInstance(
   std::shared_ptr<AssetWallet> const walletPtr,
   std::function<std::vector<UTXO>(uint64_t)> getUtxoLbd,
   const vector<AddressBookEntry>& addrBook, 
   uint64_t spendableBalance, unsigned topHeight) :
   cs_(getFetchLambdaFromWallet(walletPtr, getUtxoLbd), addrBook, spendableBalance, topHeight),
   walletPtr_(walletPtr), spendableBalance_(spendableBalance)
{}

////////////////////////////////////////////////////////////////////////////////
CoinSelectionInstance::CoinSelectionInstance(
   SwigClient::Lockbox* const lockbox, 
   unsigned M, unsigned N, uint64_t balance, unsigned topHeight) :
   cs_(getFetchLambdaFromLockbox(lockbox, M, N), 
      vector<AddressBookEntry>(), balance, topHeight),
   walletPtr_(nullptr),
   spendableBalance_(balance)
{}

////////////////////////////////////////////////////////////////////////////////
function<vector<UTXO>(uint64_t)> CoinSelectionInstance
   ::getFetchLambdaFromWalletContainer(WalletContainer* const walletContainer)
{
   if (walletContainer == nullptr)
      throw runtime_error("null wallet container ptr");

   auto fetchLbd = [walletContainer](uint64_t val)->vector<UTXO>
   {
      auto&& vecUtxo = walletContainer->getSpendableTxOutListForValue(val);
      decorateUTXOs(walletContainer->wallet_, vecUtxo);

      return vecUtxo;
   };

   return fetchLbd;
}

////////////////////////////////////////////////////////////////////////////////
function<vector<UTXO>(uint64_t)> CoinSelectionInstance
::getFetchLambdaFromWallet(shared_ptr<AssetWallet> const walletPtr,
   function<vector<UTXO>(uint64_t)> lbd)
{
   if (walletPtr == nullptr)
      throw runtime_error("null wallet ptr");

   auto fetchLbd = [walletPtr, lbd](uint64_t val)->vector<UTXO>
   {
      auto&& vecUtxo = lbd(val);
      decorateUTXOs(walletPtr, vecUtxo);

      return vecUtxo;
   };

   return fetchLbd;
}

////////////////////////////////////////////////////////////////////////////////
function<vector<UTXO>(uint64_t)> CoinSelectionInstance
   ::getFetchLambdaFromLockbox(  
      SwigClient::Lockbox* const lockbox, unsigned M, unsigned N)
{
   if (lockbox == nullptr)
      throw runtime_error("null lockbox ptr");

   auto fetchLbd = [lockbox, M, N](uint64_t val)->vector<UTXO>
   {
      auto&& vecUtxo = lockbox->getSpendableTxOutListForValue(val);

      unsigned sigSize = M * 73;
      unsigned scriptSize = N * 66 + 3;

      for (auto& utxo : vecUtxo)
      {
         utxo.witnessDataSizeBytes_ = 0;
         utxo.isInputSW_ = false;

         utxo.txinRedeemSizeBytes_ = sigSize;

         if (BtcUtils::getTxOutScriptType(utxo.getScript()) == TXOUT_SCRIPT_P2SH)
            utxo.txinRedeemSizeBytes_ += scriptSize;
      }

      return vecUtxo;
   };

   return fetchLbd;
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::decorateUTXOs(
   shared_ptr<AssetWallet> const walletPtr, vector<UTXO>& vecUtxo)
{
   if (walletPtr == nullptr)
      throw runtime_error("nullptr wallet");

   for (auto& utxo : vecUtxo)
   {
      auto&& scrAddr = utxo.getRecipientScrAddr();
      auto& ID = walletPtr->getAssetIDForAddr(scrAddr);
      auto addrPtr = walletPtr->getAddressEntryForID(ID.first);

      utxo.txinRedeemSizeBytes_ = 0;
      utxo.witnessDataSizeBytes_ = 0;
      utxo.isInputSW_ = false;

      while (true)
      {
         utxo.txinRedeemSizeBytes_ += addrPtr->getInputSize();

         try
         {
            utxo.witnessDataSizeBytes_ += addrPtr->getWitnessDataSize();
            utxo.isInputSW_ = true;
         }
         catch (runtime_error&)
         {}

         auto addrNested = dynamic_pointer_cast<AddressEntry_Nested>(addrPtr);
         if (addrNested == nullptr)
            break;

         addrPtr = addrNested->getPredecessor();
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::selectUTXOs(vector<UTXO>& vecUtxo, 
   uint64_t fee, float fee_byte, unsigned flags)
{
   uint64_t spendableVal = 0;
   for (auto& utxo : vecUtxo)
      spendableVal += utxo.getValue();

   //sanity check
   checkSpendVal(spendableVal);

   //decorate coin control selection
   decorateUTXOs(walletPtr_, vecUtxo);

   state_utxoVec_ = vecUtxo;

   PaymentStruct payStruct(recipients_, fee, fee_byte, flags);
   selection_ = move(
      cs_.getUtxoSelectionForRecipients(payStruct, vecUtxo));
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::selectUTXOs(uint64_t fee, float fee_byte, 
   unsigned flags)
{
   //sanity check
   checkSpendVal(spendableBalance_);

   state_utxoVec_.clear();
   PaymentStruct payStruct(recipients_, fee, fee_byte, flags);
   selection_ = move(
      cs_.getUtxoSelectionForRecipients(payStruct, vector<UTXO>()));
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::updateState(
   uint64_t fee, float fee_byte, unsigned flags)
{
   PaymentStruct payStruct(recipients_, fee, fee_byte, flags);
   selection_ = move(
      cs_.getUtxoSelectionForRecipients(payStruct, state_utxoVec_));
}

////////////////////////////////////////////////////////////////////////////////
unsigned CoinSelectionInstance::addRecipient(
   const BinaryData& hash, uint64_t value)
{
   unsigned id = 0;
   if (recipients_.size() != 0)
   {
      auto iter = recipients_.rbegin();
      id = iter->first + 1;
   }

   addRecipient(id, hash, value);
   return id;
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::addRecipient(
   unsigned id, const BinaryData& hash, uint64_t value)
{
   if (hash.getSize() == 0)
      throw CoinSelectionException("empty script hash");

   recipients_.insert(make_pair(id, createRecipient(hash, value)));
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<ScriptRecipient> CoinSelectionInstance::createRecipient(
   const BinaryData& hash, uint64_t value)
{
   shared_ptr<ScriptRecipient> rec;
   auto scrType = *hash.getPtr();

   const auto p2pkh_byte = NetworkConfig::getPubkeyHashPrefix();
   const auto p2sh_byte = NetworkConfig::getScriptHashPrefix();

   if (scrType == p2pkh_byte)
   {
      rec = make_shared<Recipient_P2PKH>(
         hash.getSliceRef(1, hash.getSize() - 1), value);
   }
   else if (scrType == p2sh_byte)
   {
      rec = make_shared<Recipient_P2SH>(
         hash.getSliceRef(1, hash.getSize() - 1), value);
   }
   else if(scrType == SCRIPT_PREFIX_P2WPKH)
   {
      auto&& hashVal = hash.getSliceCopy(1, hash.getSize() - 1);
      rec = make_shared<Recipient_P2WPKH>(
         hashVal, value);
   }
   else if (scrType == SCRIPT_PREFIX_P2WSH)
   {
      auto&& hashVal = hash.getSliceCopy(1, hash.getSize() - 1);
      rec = make_shared<Recipient_P2WSH>(
         hashVal, value);
   }
   else
   {
      throw ScriptRecipientException("unexpected script type");
   }

   return rec;
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::updateRecipient(
   unsigned id, const BinaryData& hash, uint64_t value)
{
   recipients_.erase(id);
   
   addRecipient(id, hash, value);
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::updateOpReturnRecipient(
   unsigned id, const BinaryData& message)
{
   recipients_.erase(id);

   auto recipient = make_shared<Recipient_OPRETURN>(message);
   recipients_.insert(make_pair(id, recipient));
}


////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::removeRecipient(unsigned id)
{
   recipients_.erase(id);
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::resetRecipients()
{
   recipients_.clear();
}

////////////////////////////////////////////////////////////////////////////////
uint64_t CoinSelectionInstance::getSpendVal() const
{
   uint64_t total = 0;
   for (auto& recPair : recipients_)
      total += recPair.second->getValue();

   return total;
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::checkSpendVal(uint64_t spendableBalance) const
{
   auto total = getSpendVal();
   if (total == 0 || total > spendableBalance)
   {
      throw CoinSelectionException("Invalid spend value");
   }
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::processCustomUtxoList(
   const vector<BinaryData>& serializedUtxos,
   uint64_t fee, float fee_byte, unsigned flags)
{
   if (serializedUtxos.size() == 0)
      throw CoinSelectionException("empty custom utxo list!");

   vector<UTXO> utxoVec;

   for (auto& serializedUtxo : serializedUtxos)
   {
      UTXO utxo;
      utxo.unserialize(serializedUtxo);
      utxoVec.push_back(move(utxo));
   }
   
   selectUTXOs(utxoVec, fee, fee_byte, flags);
}

////////////////////////////////////////////////////////////////////////////////
uint64_t CoinSelectionInstance::getFeeForMaxValUtxoVector(
   const vector<BinaryData>& serializedUtxos, float fee_byte)
{
   auto txoutsize = 0;
   for (auto& rec : recipients_)
      txoutsize += rec.second->getSize();

   vector<UTXO> utxoVec;
   if (serializedUtxos.size() > 0)
   {
      for (auto& rawUtxo : serializedUtxos)
      {
         UTXO utxo;
         utxo.unserialize(rawUtxo);
         utxoVec.push_back(move(utxo));
      }

      //decorate coin control selection
      decorateUTXOs(walletPtr_, utxoVec);
   }

   return cs_.getFeeForMaxVal(txoutsize, fee_byte, utxoVec);
}

////////////////////////////////////////////////////////////////////////////////
uint64_t CoinSelectionInstance::getFeeForMaxVal(float fee_byte)
{
   vector<BinaryData> utxos;
   return getFeeForMaxValUtxoVector(utxos, fee_byte);
}

////////////////////////////////////////////////////////////////////////////////
////
//// UniversalSigner
////
////////////////////////////////////////////////////////////////////////////////
UniversalSigner::UniversalSigner(const string& signerType)
{
   if (signerType != "Bcash")
      signer_ = make_unique<Signer>();
   else
      signer_ = make_unique<Signer_BCH>();

   signer_->setFlags(SCRIPT_VERIFY_SEGWIT);

   auto feed = make_shared<ResolverFeed_Universal>(this);
   signer_->setFeed(feed);
}

////////////////////////////////////////////////////////////////////////////////
UniversalSigner::~UniversalSigner()
{}


