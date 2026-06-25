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

#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <atomic>
#include <functional>
#include <memory>

#include <Utils/ThreadSafeClasses.h>
#include <Utils/Types.h>
#include <Utils/BinaryData.h>
#include "BlockObj.h"

namespace Armory
{
   namespace ZeroConf
   {
      class ZeroConfContainer;
   }
   class Blockchain;
   struct Hash32;
}
class LMDBBlockDatabase;
struct StoredDBInfo;
class AddrAndHash;

////////////////////////////////////////////////////////////////////////////////
enum class AddressBatchType : int
{
   Register,
   Unregister
};

struct AddressBatch
{
   const AddressBatchType type;

   AddressBatch(AddressBatchType);
   virtual ~AddressBatch(void) = 0;
};

////
struct RegistrationBatch : public AddressBatch
{
   using Callback = std::function<void(bool)>;

   const std::vector<std::string> walletIDs;
   const std::vector<Armory::Types::ScrAddr> scrAddrVec;
   const bool isNew;
   const Callback callback;

   RegistrationBatch(const std::vector<std::string>&,
      std::vector<Armory::Types::ScrAddr>, bool, const Callback&);
};

////
struct UnregistrationBatch : public AddressBatch
{
   std::set<Armory::Types::ScrAddr> scrAddrSet;
   std::function<void(void)> callback;

   UnregistrationBatch(void);
};

////////////////////////////////////////////////////////////////////////////////
class AddrAndHash
{
private:
   mutable BinaryData addrHash_;

public:
   const Armory::Types::ScrAddr scrAddr;
   const Armory::Types::ScrAddrId id;
   unsigned scannedHeight = 0;

public:
   AddrAndHash(const Armory::Types::ScrAddr&, Armory::Types::ScrAddrId);

   const BinaryData& getHash(void) const;
   bool operator<(const AddrAndHash&) const;
   bool operator<(const BinaryDataRef&) const;
};

using ScrAddrIdMap = std::unordered_map<
   Armory::Types::ScrAddr, Armory::Types::ScrAddrId,
   BinaryData::Hasher, BinaryData::IsEqual>;

////////////////////////////////////////////////////////////////////////////////
class ScrAddrFilter
{
   /***
   This class keeps track of all registered scrAddr to be scanned by the DB.
   If the DB isn't running in supernode, this class also acts as a helper to
   filter transactions, which is required in order to save only relevant ssh

   The transaction filter isn't exact however. It gets more efficient as it
   encounters more UTxO.

   The basic principle of the filter is that it expect to have a complete
   list of UTxO's starting a given height, usually where the DB picked up
   at initial load. It can then guarantee a TxIn isn't spending a tracked
   UTxO by checking the UTxO DBkey instead of fetching the entire stored TxOut.
   If the DBkey carries a height lower than the cut off, the filter will
   fail to give a definitive answer, in which case the TxOut script will be
   pulled from the DB, using the DBkey, as it would have otherwise.

   Registering addresses while the BDM isn't initialized will return instantly
   Otherwise, the following steps are taken:

   1) Check ssh entries in the DB for this scrAddr. If there is none, this
   DB never saw this address (full/lite node). Else mark the top scanned block.

   -- Non supernode operations --
   2.a) If the address is new, create an empty ssh header for that scrAddr
   in the DB, marked at the current top height
   2.b) If the address isn't new, scan it from its last seen block, or its
   block creation, or 0 if none of the above is available. This will create
   the ssh entries for the address, which will have the current top height as
   its scanned height.
   --

   3) Add address to scrAddrMap_

   4) Signal the wallet that the address is ready. Wallet object will take it
   up from there.
   ***/

public:
   using AddrMap = std::map<Armory::Types::ScrAddr, std::shared_ptr<AddrAndHash>>;

private:
   const uint16_t sdbiKey_;
   LMDBBlockDatabase *const lmdb_;

   std::shared_ptr<Armory::Threading::TransactionalMap<
      Armory::Types::ScrAddr, std::shared_ptr<AddrAndHash>>> scanFilterAddrMap_;

   Armory::Threading::BlockingQueue<
      std::shared_ptr<AddressBatch>> registrationStack_;

   std::thread thr_;
   Armory::Types::ScrAddrId topScrAddrID_ = 0;
   Armory::Hash32 merkleRoot_;

public:
   std::mutex mergeLock_;

private:
   void run(std::shared_future<bool>);
   AddrMap prepareRegistrationBatch(std::shared_ptr<RegistrationBatch>);
   void mergeAddresses(AddrMap, bool);
   AddrMap assignScrAddrKeys(const std::vector<Armory::Types::ScrAddr>&);

   Armory::Hash32 computeMerkleRoot(void) const;
   void updateAddressMerkle(void);
   StoredDBInfo getSDBI(void) const;
   void cleanUpSdbis(void);

public:
   ScrAddrFilter(LMDBBlockDatabase*, uint16_t);
   virtual ~ScrAddrFilter(void);

   ////
   void start(std::shared_future<bool>);
   void shutdown(void);
   bool empty(void) const;
   void resetSDBI(void);

   ////
   std::shared_ptr<const AddrMap> getScanFilterAddrMap(void) const;
   std::shared_ptr<Armory::Threading::TransactionalMap<
      BinaryData, std::shared_ptr<AddrAndHash>>> getZcFilterMapPtr(void) const;

   ////
   ScrAddrIdMap getScrAddrIds(void) const;
   void pushAddressBatch(std::shared_ptr<AddressBatch>);
   void updateScannedHash(const Armory::Hash32&);
   Armory::Hash32 headerHashToScanFrom(void);

   std::set<BinaryData> getMissingHashes(void) const;
   void putMissingHashes(const std::set<BinaryData>&);

   ////
   void unregisterAddresses(
      const std::set<Armory::Types::ScrAddr>&,
      const std::function<void(void)>&);

//virtuals
protected:
   virtual std::shared_ptr<ScrAddrFilter> getNew(unsigned) = 0;
   virtual Armory::Hash32 applyBlockRangeToDB(uint32_t,
      const std::vector<std::string>&, bool) = 0;
   virtual std::shared_ptr<Armory::Blockchain> blockchain(void) const = 0;
   virtual bool bdmIsRunning(void) const = 0;
};
