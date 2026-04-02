////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <deque>

#include "BlockDataMap.h"
#include "bdmenums.h"
#include "Progress.h"

class LMDBBlockDatabase;
class BlockDataManager;
class ScrAddrFilter;
class UnresolvedHashException {};

typedef std::function<void(BDMPhase, double, unsigned, unsigned)> ProgressCallback;

namespace Armory
{
   class Blockchain;
   class ReorganizationState;
}

/////////////////////////////////////////////////////////////////////////////
class DatabaseBuilder
{
private:
   std::shared_ptr<BlockFiles> blockFiles_;
   std::shared_ptr<Armory::Blockchain> blockchain_;
   LMDBBlockDatabase* db_;
   std::shared_ptr<ScrAddrFilter> scrAddrFilter_;

   const ProgressCallback progress_;
   BlockOffset topBlockOffset_;

   unsigned checkedTransactions_ = 0;
   const bool forceRescanSSH_;

private:
   void loadBlockHeadersFromDB(const ProgressCallback&);
   std::deque<std::shared_ptr<BlockHeader>> addBlocksToDB(
      const BlockDataLoader::BlockDataCopy&);
   void parseBlockFile(
      const BlockDataLoader::BlockDataCopy&,
      const std::function<bool(
         const uint8_t* data, size_t size, size_t offset
      )>&
   );

   Armory::ReorganizationState updateBlocksInDB(
      const ProgressCallback&,
      std::shared_ptr<BlockDataLoader> = nullptr);
   BinaryData initTransactionHistory(int32_t);
   BinaryData scanHistory(int32_t, bool, bool);
   void undoHistory(Armory::ReorganizationState&);

   void resetHistory(void);
   void verifyTransactions(void);
   void commitAllTxHints(
      const std::vector<std::shared_ptr<BlockData>>&,
      const std::set<unsigned>&);
   void commitAllStxos(
      const std::vector<std::shared_ptr<BlockData>>&,
      const std::set<unsigned>&);

   //void repairTxFilters(const std::set<unsigned>&);
   //void reprocessTxFilter(std::shared_ptr<BlockDataFileMap>, unsigned);

   void cycleDatabases(void);

public:
   DatabaseBuilder(std::shared_ptr<BlockFiles>,
      BlockDataManager&,
      const ProgressCallback&, bool);

   bool init(void);
   Armory::ReorganizationState update(void);

   void verifyChain(void);
   unsigned getCheckedTxCount(void) const { return checkedTransactions_; }

   //void verifyTxFilters(void);
   void checkTxHintsIntegrity(void);
};
