////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <thread>
#include <cstring>

#include "ScrAddrFilter.h"
#include <Utils/BtcUtils.h>
#include <Utils/DBUtils.h>
#include <Utils/ArmoryConfig.h>
#include <Utils/BitcoinSettings.h>

#include "lmdb_wrapper.h"
#include "Blockchain.h"
#include "StoredBlockObj.h"

using namespace Armory;
namespace {
   static constexpr uint16_t SIDESCAN_ID = 0x00FF;
}

///////////////////////////////////////////////////////////////////////////////
// ScrAddrFilter
ScrAddrFilter::ScrAddrFilter(LMDBBlockDatabase* lmdb, uint16_t sdbiKey)
   : sdbiKey_(0xFFFF - sdbiKey), lmdb_(lmdb)
{
   scanFilterAddrMap_ = std::make_shared<Threading::TransactionalMap<
      BinaryData, std::shared_ptr<AddrAndHash>>>();

   //seed SDBI if necessary
   try {
      getSDBI();
   } catch (const LmdbWrapperException&) {
      resetSDBI();
   }
}

ScrAddrFilter::~ScrAddrFilter()
{
   shutdown();
}

bool ScrAddrFilter::empty() const
{
   return scanFilterAddrMap_->empty();
}

void ScrAddrFilter::start(std::shared_future<bool> bdmReadyFut)
{
   LOGINFO << "loading known addresses";

   /* grab all scrAddr ids from db */
   {
      auto tx = lmdb_->beginTransaction(
         DB_SELECT::SCRADDR, LMDB::Mode::ReadOnly);
      auto dbIter = tx->getIterator();
      dbIter.seekToFirst();
      std::map<Types::ScrAddr, std::shared_ptr<AddrAndHash>> scrAddrMap;

      //iterate over scraddr DB
      topScrAddrID_ = 0;
      do {
         auto keyRef = dbIter.getKeyRef();
         if (keyRef.getSize() != sizeof(Types::ScrAddrId)) {
            continue;
         }
         Types::ScrAddrId scrAddrId;
         std::memcpy(&scrAddrId, keyRef.getPtr(), sizeof(Types::ScrAddrId));
         if (scrAddrId >= 0xFFFF0000) {
            //sdbi entry, ignore
            continue;
         }
         auto aah = std::make_shared<AddrAndHash>(dbIter.getValue(), scrAddrId);
         scrAddrMap.emplace(aah->scrAddr, aah);
         topScrAddrID_ = std::max(topScrAddrID_, scrAddrId);
      } while (dbIter.advanceAndRead());

      //update members
      scanFilterAddrMap_->update(scrAddrMap);
      ++topScrAddrID_;
   }
   merkleRoot_ = computeMerkleRoot();
   LOGINFO << "found " << scanFilterAddrMap_->size() << " known addresses";

   /* if bdm isn't ready, exhaust addr registration queue */
   if (bdmIsRunning() == false) {
      auto regQueue = registrationStack_.pop_all();
      for (auto batch : regQueue) {
         auto batchPtr = std::dynamic_pointer_cast<RegistrationBatch>(batch);
         if (batchPtr == nullptr) {
            //ignore unregistration batches at init time
            continue;
         }
         auto result = prepareRegistrationBatch(batchPtr);
         if (!result.empty()) {
            throw std::runtime_error(
               "no registration batch should lead to a scan at SCA start!");
         }
      }
   }

   /* start operation thread */
   thr_ = std::thread([this, fut=bdmReadyFut]{ run(fut); });
}

void ScrAddrFilter::shutdown()
{
   registrationStack_.terminate();
   if (thr_.joinable()) {
      thr_.join();
   }
}

////////
ScrAddrFilter::AddrMap ScrAddrFilter::prepareRegistrationBatch(
   std::shared_ptr<RegistrationBatch> batch)
{
   if (Config::DBSettings::getDbType() == ARMORY_DB_TYPE::Super) {
      //this class currently serves no purpose in supernode
      batch->callback(true);
      return {};
   }

   //filter out collisions, assign IDs for fresh addresses
   auto newScrAddrMap = assignScrAddrKeys(batch->scrAddrVec);
   if (newScrAddrMap.empty()) {
      //all addresses are already registered
      batch->callback(true);
      return {};
   }

   if (batch->isNew) {
      //batch is flagged as new, all addresses within it are assumed
      //clean of history. Update the map and continue
      mergeAddresses(std::move(newScrAddrMap), true);
      batch->callback(true);
      return {};
   } else if (!bdmIsRunning()) {
      //BDM isn't running, merge address set but do not update addr
      //merkle root. This will trigger a rescan of all addresses once
      //BDM is up.
      mergeAddresses(std::move(newScrAddrMap), false);
      batch->callback(true);
      return {};
   }
   return newScrAddrMap;
}

////////
std::shared_ptr<const ScrAddrFilter::AddrMap>
ScrAddrFilter::getScanFilterAddrMap() const
{
   return scanFilterAddrMap_->get();
}

////////
ScrAddrFilter::AddrMap ScrAddrFilter::assignScrAddrKeys(
   const std::vector<Types::ScrAddr>& addrVec)
{
   AddrMap result;
   auto scraddrmap = scanFilterAddrMap_->get();
   for (const auto& sa : addrVec) {
      auto iter = scraddrmap->find(sa);
      if (iter != scraddrmap->end()) {
         continue;
      }
      result.emplace(sa, std::make_shared<AddrAndHash>(sa, topScrAddrID_++));
   }
   if (result.empty()) {
      return {};
   }

   //commit fresh <db, addr> pairs to db
   auto tx = lmdb_->beginTransaction(DB_SELECT::SCRADDR, LMDB::Mode::ReadWrite);
   for (const auto& aaPair : result) {
      LMDB::DataRef keyRef{
         sizeof(Types::ScrAddrId),
         (const char*)&aaPair.second->id
      };
      tx->insert(
         LMDB::DataRef{
            sizeof(Types::ScrAddrId), (const char*)&aaPair.second->id},
         LMDB::DataRef{
            aaPair.first.getSize(), aaPair.first.getPtr()}
      );
   }
   return result;
}

void ScrAddrFilter::mergeAddresses(AddrMap addrMap, bool updateMerkleRoot)
{
   bool wasEmpty = scanFilterAddrMap_->empty();
   scanFilterAddrMap_->update(std::move(addrMap));
   if (!updateMerkleRoot) {
      /* edge case:
         if any of the merged batches skipped the merkle root update, addrMap
         has to be treated as fresh
      */
      if (merkleRoot_.valid()) {
         merkleRoot_ = Hash32{};
      }
   } else if (merkleRoot_.valid() || wasEmpty) {
      merkleRoot_ = computeMerkleRoot();
      updateAddressMerkle();
   }
}

Hash32 ScrAddrFilter::headerHashToScanFrom()
{
   auto sdbi = getSDBI();
   if (!merkleRoot_.valid() || !sdbi.metaHash.valid()) {
      merkleRoot_ = computeMerkleRoot();
      sdbi.metaHash = merkleRoot_;
      auto tx = lmdb_->beginTransaction(DB_SELECT::SCRADDR, LMDB::Mode::ReadWrite);
      lmdb_->putStoredDBInfo(DB_SELECT::SCRADDR, sdbi, sdbiKey_);
   } else if (merkleRoot_ == sdbi.metaHash) {
      return sdbi.topScannedBlkHash;
   }
   return Hash32{};
}

////////
Hash32 ScrAddrFilter::computeMerkleRoot() const
{
   std::vector<BinaryData> addrVec;
   auto scraddrmap = scanFilterAddrMap_->get();
   if (scraddrmap->empty()) {
      return Hash32{};
   }
   addrVec.reserve(scraddrmap->size());

   for (const auto& addr : *scraddrmap) {
      addrVec.emplace_back(addr.second->getHash());
   }

   Hash32 result;
   auto merkle = BtcUtils::calculateMerkleRoot(addrVec);
   std::memcpy(result.data, merkle.getPtr(), 32);
   return result;
}

void ScrAddrFilter::updateAddressMerkle()
{
   auto tx = lmdb_->beginTransaction(DB_SELECT::SCRADDR, LMDB::Mode::ReadWrite);
   auto sdbi = lmdb_->getStoredDBInfo(DB_SELECT::SCRADDR, sdbiKey_);
   sdbi.metaHash = merkleRoot_;
   lmdb_->putStoredDBInfo(DB_SELECT::SCRADDR, sdbi, sdbiKey_);
}

void ScrAddrFilter::updateScannedHash(const Hash32& hash)
{
   auto tx = lmdb_->beginTransaction(DB_SELECT::SCRADDR, LMDB::Mode::ReadWrite);
   auto sdbi = getSDBI();
   sdbi.topScannedBlkHash = hash;
   lmdb_->putStoredDBInfo(DB_SELECT::SCRADDR, sdbi, sdbiKey_);
}

void ScrAddrFilter::resetSDBI()
{
   auto tx = lmdb_->beginTransaction(DB_SELECT::SCRADDR, LMDB::Mode::ReadWrite);
   StoredDBInfo sdbi;
   try {
      sdbi = std::move(lmdb_->getStoredDBInfo(DB_SELECT::SCRADDR, sdbiKey_));
   } catch (const LmdbWrapperException&) {
      sdbi.magicBytes = Config::BitcoinSettings::getMagicBytes();
      sdbi.armoryType = Config::DBSettings::getDbType();
   }
   sdbi.metaHash = Hash32{};
   sdbi.topScannedBlkHash = Hash32{};
   lmdb_->putStoredDBInfo(DB_SELECT::SCRADDR, sdbi, sdbiKey_);
}

////////
StoredDBInfo ScrAddrFilter::getSDBI() const
{
   auto tx = lmdb_->beginTransaction(DB_SELECT::SCRADDR, LMDB::Mode::ReadOnly);
   return lmdb_->getStoredDBInfo(DB_SELECT::SCRADDR, sdbiKey_);
}

////////
std::set<BinaryData> ScrAddrFilter::getMissingHashes() const
{
   return lmdb_->getMissingHashes(sdbiKey_);
}

void ScrAddrFilter::putMissingHashes(const std::set<BinaryData>& hashSet)
{
   auto tx = lmdb_->beginTransaction(DB_SELECT::TXFILTERS, LMDB::Mode::ReadWrite);
   lmdb_->putMissingHashes(hashSet, sdbiKey_);
}

////////
void ScrAddrFilter::pushAddressBatch(std::shared_ptr<AddressBatch> batch)
{
   registrationStack_.push_back(std::move(batch));
}

void ScrAddrFilter::run(std::shared_future<bool> bdmReadyFut)
{
   if (bdmReadyFut.get() == false) {
      return;
   }

   while (true) {
      std::shared_ptr<AddressBatch> batch;
      try {
         batch = std::move(registrationStack_.pop_front());
      } catch (const Threading::StopBlockingLoop&) {
         //end loop condition
         break;
      }


      switch (batch->type)
      {
         case AddressBatchType::Register:
         {
            auto batchPtr = std::dynamic_pointer_cast<RegistrationBatch>(batch);
            if (batchPtr == nullptr) {
               throw std::runtime_error("unexpected batch ptr type");
            }

            //prepare the batch, it will also wrap up registration for edge case.
            //if the returned addr map is empty, there's nothing else to do.
            auto newScrAddrMap = prepareRegistrationBatch(batchPtr);
            if (newScrAddrMap.empty()) {
               break;
            }

            /* BDM is initialized and maintenance thread is running, scan the batch */
            LOGINFO << "Starting address registration process";

            //prepare for side scan
            auto saf = getNew(SIDESCAN_ID);
            saf->mergeAddresses(newScrAddrMap, false);
            auto scanFromHeader = blockchain()->getGenesisHeader();
            Hash32 scannedHash;

            while (true) {
               //run the side scan
               scannedHash = saf->applyBlockRangeToDB(
                  scanFromHeader->getBlockHeight(), batchPtr->walletIDs, true);
               if (!scannedHash.valid()) {
                  break;
               }

               //lock the merge mutex and check the side scan top hash matches
               //main scrAddr set top hash
               std::unique_lock<std::mutex> lock(mergeLock_);
               auto sdbi = getSDBI();
               if (!sdbi.topScannedBlkHash.valid() && !sdbi.metaHash.valid()) {
                  //edge case: main scrAddr db is varying, set top hash to
                  //side scan one and proceed with address merge
                  sdbi.topScannedBlkHash = scannedHash;
                  auto tx = lmdb_->beginTransaction(DB_SELECT::SCRADDR, LMDB::Mode::ReadWrite);
                  lmdb_->putStoredDBInfo(DB_SELECT::SCRADDR, sdbi, sdbiKey_);
               }
               if (sdbi.topScannedBlkHash == scannedHash) {
                  mergeAddresses(std::move(newScrAddrMap), true);
                  break;
               } else {
                  //main scrAddr set is scanned up to a different block,
                  //let's try and catch up
                  scanFromHeader = blockchain()->getHeaderByHash(scannedHash);
                  if (!scanFromHeader->isMainBranch()) {
                     //TODO: deal with this edge case
                     LOGERR << "there was a reorg during a side scan, "
                        << "idk how to deal with this yet";
                     throw std::runtime_error("reorg during side scan, implement me!");
                  }
               }
            }

            //cleanup side scan context
            saf->cleanUpSdbis();

            //was the scan successful?
            if (!scannedHash.valid()) {
               //no, fire callback and exit thread
               batchPtr->callback(false);
               break;
            }

            //notify
            for (const auto& wID : batchPtr->walletIDs) {
               LOGINFO << "Completed scan of wallet " << wID;
            }
            batchPtr->callback(true);
            break;
         }

         case AddressBatchType::Unregister:
         {
            auto batchPtr = std::dynamic_pointer_cast<UnregistrationBatch>(batch);
            if (batchPtr == nullptr) {
               throw std::runtime_error("unexpected batch ptr type");
            }

            /*
            TODO: need to remove unregistered addresses from filter fed to zc parser

            std::set<BinaryData> scrAddrSet;
            scrAddrSet.insert(
               batchPtr->scrAddrSet_.begin(), batchPtr->scrAddrSet_.end());
            updateAddrMap(scrAddrSet, 0, true);
            */
            if (batchPtr->callback) {
               batchPtr->callback();
            }
            break;
         }
      }
   }
}

////////
ScrAddrIdMap ScrAddrFilter::getScrAddrIds() const
{
   ScrAddrIdMap result;
   auto scrAddrMap = scanFilterAddrMap_->get();
   for (auto& scrAddr : *scrAddrMap) {
      if (scrAddr.first.empty()) {
         continue;
      }
      result.emplace(scrAddr.first, scrAddr.second->id);
   }
   return result;
}

////////
void ScrAddrFilter::cleanUpSdbis()
{
   auto key = StoredDBInfo::getDBKey(sdbiKey_);
   auto tx = lmdb_->beginTransaction(DB_SELECT::SCRADDR, LMDB::Mode::ReadWrite);
   tx->erase(LMDB::DataRef{key.getSize(), key.getPtr()});
}

////////
void ScrAddrFilter::unregisterAddresses(
   const std::set<Types::ScrAddr>& scrAddrSet,
   const std::function<void(void)>& callback)
{
   /*
   Remove addresses from the ScrAddrFilter zcFilter map
   */
   auto batch = std::make_shared<UnregistrationBatch>();
   batch->scrAddrSet.insert(scrAddrSet.begin(), scrAddrSet.end());
   batch->callback = callback;
   pushAddressBatch(std::move(batch));
}

std::shared_ptr<Threading::TransactionalMap<
   Types::ScrAddr, std::shared_ptr<AddrAndHash>>>
ScrAddrFilter::getZcFilterMapPtr() const
{
   return scanFilterAddrMap_;
}

///////////////////////////////////////////////////////////////////////////////
// AddressBatch
AddressBatch::AddressBatch(AddressBatchType t) :
   type(t)
{}

AddressBatch::~AddressBatch()
{}

////////
RegistrationBatch::RegistrationBatch(
   const std::vector<std::string>& wltIds,
   std::vector<Types::ScrAddr> addrVec,
   bool isnew, const RegistrationBatch::Callback& cb) :
   AddressBatch(AddressBatchType::Register),
   walletIDs{wltIds}, scrAddrVec{std::move(addrVec)},
   isNew(isnew), callback(cb)
{}

UnregistrationBatch::UnregistrationBatch() :
   AddressBatch(AddressBatchType::Unregister)
{}

///////////////////////////////////////////////////////////////////////////////
// AddrAndHash
AddrAndHash::AddrAndHash(const Types::ScrAddr& addr,
   Types::ScrAddrId scrAddrId) :
   scrAddr(addr), id(scrAddrId)
{}

const BinaryData& AddrAndHash::getHash() const
{
   if (addrHash_.empty()) {
      addrHash_ = std::move(BtcUtils::getHash256(scrAddr));
   }
   return addrHash_;
}

bool AddrAndHash::operator<(const AddrAndHash& rhs) const
{
   return this->scrAddr < rhs.scrAddr;
}

bool AddrAndHash::operator<(const BinaryDataRef& rhs) const
{
   return this->scrAddr.getRef() < rhs;
}
