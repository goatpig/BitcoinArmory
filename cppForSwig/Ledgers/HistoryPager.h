////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <map>
#include <functional>
#include <memory>

#include <Utils/ThreadSafeClasses.h>

class AlreadyPagedException
{};

class BinaryData;
class Blockchain;
class TxIOPair;

namespace Armory
{
   namespace Ledgers
   {
      class Entry;

      class HistoryPager
      {
      private:
         struct Page
         {
            uint32_t blockStart;
            uint32_t blockEnd;
            uint32_t count;
            unsigned updateID = UINT32_MAX;
            Threading::TransactionalMap<BinaryData, Entry> pageLedgers;

            Page(void);
            Page(uint32_t, uint32_t, uint32_t);

            bool operator<(const Page&) const;
            static bool comparator(
               const std::shared_ptr<Page>&,
               const std::shared_ptr<Page>&
            );
         };

         std::shared_ptr<std::atomic<bool>> isInitialized_;
         std::shared_ptr<std::vector<std::shared_ptr<Page>>> pages_;
         std::map<uint32_t, uint32_t> SSHsummary_;
         static uint32_t txnPerPage_;

      public:
         HistoryPager(void);
         void reset(void);
         bool isInitiliazed(void) const;

         std::shared_ptr<const std::map<BinaryData, Entry>>
         getPageLedgerMap(
            std::function<std::map<BinaryData, TxIOPair>(uint32_t, uint32_t)>,
            std::function<std::map<BinaryData, Entry>(
               const std::map<BinaryData, TxIOPair>&, uint32_t, uint32_t)>,
            uint32_t, unsigned, std::map<BinaryData, TxIOPair>* = nullptr);
         std::shared_ptr<const std::map<BinaryData, Entry>>
         getPageLedgerMap(uint32_t);

         void addPage(std::vector<std::shared_ptr<Page>>&,
            uint32_t, uint32_t, uint32_t);
         void sortPages(std::vector<std::shared_ptr<Page>>&);

         bool mapHistory(std::function<std::map<uint32_t, uint32_t>(void)>);
         const std::map<uint32_t, uint32_t>& getSSHsummary(void) const;

         uint32_t getPageBottom(uint32_t) const;
         size_t   getPageCount(void) const;
         uint32_t getRangeForHeightAndCount(uint32_t, uint32_t) const;
         uint32_t getBlockInVicinity(uint32_t) const;
         uint32_t getPageIdForBlockHeight(uint32_t) const;
      };
   } //namespace Ledgers
} //namespace Armory
