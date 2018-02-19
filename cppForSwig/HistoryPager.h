////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#ifndef HISTORY_PAGER_H
#define HISTORY_PAGER_H

#include <map>
#include <functional>
#include <memory>

#include "ThreadSafeClasses.h"
#include "BinaryData.h"
#include "LedgerEntry.h"
#include "BlockObj.h"

class AlreadyPagedException
{};

class HistoryPager
{
private:

   struct Page
   {
      uint32_t blockStart_;
      uint32_t blockEnd_;
      uint32_t count_;
      unsigned updateID_ = UINT32_MAX;

      TransactionalMap<BinaryData, LedgerEntry> pageLedgers_;

      Page(void) : blockStart_(UINT32_MAX), blockEnd_(UINT32_MAX), count_(0)
      {}

      Page(uint32_t count, uint32_t bottom, uint32_t top) :
         blockStart_(bottom), blockEnd_(top), count_(count)
      {}

      bool operator< (const Page& rhs) const
      {
         //history pages are order backwards
         return this->blockStart_ > rhs.blockStart_;
      }

      static bool comparator(
         const shared_ptr<Page>& a, const shared_ptr<Page>& b)
      {
         return *a < *b;
      }
   };

   bool isInitialized_ = false;
   vector<shared_ptr<Page>> pages_;
   map<uint32_t, uint32_t> SSHsummary_;

   uint32_t currentPage_ = -1;
   
   static uint32_t txnPerPage_;

public:

   HistoryPager(void) {}

   shared_ptr<map<BinaryData, LedgerEntry>> getPageLedgerMap(
      function< map<BinaryData, TxIOPair>(uint32_t, uint32_t) > getTxio,
      function< map<BinaryData, LedgerEntry>(
         const map<BinaryData, TxIOPair>&, uint32_t, uint32_t) > buildLedgers,
      uint32_t pageId, unsigned updateID, map<BinaryData, TxIOPair>* txioMap = nullptr);

   shared_ptr<map<BinaryData, LedgerEntry>> getPageLedgerMap(uint32_t pageId);

   void reset(void) { 
      pages_.clear(); 
      isInitialized_ = false;
   }

   void addPage(uint32_t count, uint32_t bottom, uint32_t top);
   void sortPages(void);
   
   bool mapHistory(
      function< map<uint32_t, uint32_t>(void) > getSSHsummary);
   
   const map<uint32_t, uint32_t>& getSSHsummary(void) const
   { return SSHsummary_; }
   
   uint32_t getPageBottom(uint32_t id) const;
   size_t   getPageCount(void) const { return pages_.size(); }
   uint32_t getCurrentPage(void) const { return currentPage_; }
   void setCurrentPage(uint32_t pageId) { currentPage_ = pageId; }
   
   uint32_t getRangeForHeightAndCount(uint32_t height, uint32_t count) const;
   uint32_t getBlockInVicinity(uint32_t blk) const;
   uint32_t getPageIdForBlockHeight(uint32_t) const;

   bool isInitiliazed(void) const
   {
      return isInitialized_;
   }
};

#endif
