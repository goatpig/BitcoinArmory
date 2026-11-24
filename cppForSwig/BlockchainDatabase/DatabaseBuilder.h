////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <deque>

#include "BlockObj.h"
#include "BlockDataMap.h"
#include "Progress.h"

class LMDBBlockDatabase;
class BlockDataManager;
class ScrAddrFilter;
class ScannerContext;
class UnresolvedHashException {};

namespace Armory
{
   class Blockchain;
   class ReorganizationState;

   namespace Database
   {
      class Builder
      {
      private:
         std::shared_ptr<BlockFiles> blockFiles_;
         std::shared_ptr<Blockchain> blockchain_;
         LMDBBlockDatabase* db_;
         std::shared_ptr<ScrAddrFilter> scrAddrFilter_;
         std::unique_ptr<ScannerContext> scannerCtx_;

         const BDMProgress::ReportFunc progress_;
         unsigned checkedTransactions_ = 0;

      public:
         //for test coverage purposes
         std::pair<Hash32, Hash32> lastScanRange;

      private:
         void loadBlockHeadersFromDB(const BDMProgress::ReportFunc&);
         std::set<uint32_t> addBlocksToDB(BlockDataLoader::BlockDataCopy);
         void parseBlockFile(BlockDataLoader::BlockDataCopy,
            const std::function<bool(const uint8_t*, size_t, size_t)>&
         );

         BlockOffset parseForNewHeaders(const BDMProgress::ReportFunc&);
         void parseForNewBlocks(const BlockOffset&, const BDMProgress::ReportFunc&);

         Hash32 initTransactionHistory(const ReorganizationState&);
         Hash32 scanHistory(const ReorganizationState&, bool, bool);

         void verifyTransactions(void);
         void cycleDatabases(void);

      public:
         Builder(BlockDataManager&, const BDMProgress::ReportFunc&);

         bool init(void);
         ReorganizationState update(void);
         void mergeContext(ScannerContext&);

         void verifyChain(void);
         void checkTxHintsIntegrity(void);
         unsigned getCheckedTxCount(void) const;
      };
   } // namespace BlockchainData
} // namespace Armory
