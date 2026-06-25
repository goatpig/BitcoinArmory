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

#include <algorithm>

#include "HistoryPager.h"
#include <BlockchainDatabase/txio.h>
#include "LedgerEntry.h"

using namespace Armory;
using namespace Armory::Ledgers;

uint32_t HistoryPager::txnPerPage_ = 100;

namespace {
   void addPage(std::vector<std::shared_ptr<HistoryPager::Page>>& pages,
      uint32_t count, uint32_t bottom, uint32_t top)
   {
      pages.emplace_back(std::make_shared<HistoryPager::Page>(count, bottom, top));
   }

   void sortPages(std::vector<std::shared_ptr<HistoryPager::Page>>& pages)
   {
      std::sort(pages.begin(), pages.end(), HistoryPager::Page::comparator);
   }
}

////////////////////////////////////////////////////////////////////////////////
// Page
HistoryPager::Page::Page() :
   blockStart(UINT32_MAX), blockEnd(UINT32_MAX), count(0)
{}

HistoryPager::Page::Page(uint32_t count, uint32_t bottom, uint32_t top) :
   blockStart(bottom), blockEnd(top), count(count)
{}

bool HistoryPager::Page::operator<(const Page& rhs) const
{
   //history pages are order backwards
   return this->blockStart > rhs.blockStart;
}

bool HistoryPager::Page::comparator(
   const std::shared_ptr<Page>& a,
   const std::shared_ptr<Page>& b)
{
   return *a < *b;
}

////////////////////////////////////////////////////////////////////////////////
// HistoryPager
HistoryPager::HistoryPager()
{
   isInitialized_ = std::make_shared<std::atomic<bool>>();
   isInitialized_->store(false, std::memory_order_relaxed);
}

void HistoryPager::reset()
{
   isInitialized_->store(false, std::memory_order_relaxed);
   pages_.store(nullptr);
}

bool HistoryPager::isInitiliazed() const
{
   return isInitialized_->load(std::memory_order_relaxed);
}

////////
const std::map<uint32_t, uint32_t>& HistoryPager::getSSHsummary() const
{
   return SSHsummary_;
}


std::shared_ptr<const std::map<BinaryData, Entry>>
HistoryPager::getPageLedgerMap(
   std::function<std::map<BinaryData, TxIOPair>(uint32_t, uint32_t) > getTxio,
   std::function<std::map<BinaryData, Entry>(
      const std::map<BinaryData, TxIOPair>&, uint32_t, uint32_t) > buildLedgers,
   uint32_t pageId, unsigned updateID,
   std::map<BinaryData, TxIOPair>* txioMap)
{
   if (!isInitialized_->load(std::memory_order_relaxed)) {
      LOGERR << "Uninitialized history";
      throw std::runtime_error("Uninitialized history");
   }

   auto pagesLocal = pages_.load(std::memory_order_acquire);
   if (pagesLocal == nullptr) {
      return nullptr;
   }

   if (pageId >= pagesLocal->size()) {
      return nullptr;
   }

   auto& page = (*pagesLocal)[pageId];
   if (updateID != UINT32_MAX && page->updateID == updateID) {
      //already loaded this page
      return page->pageLedgers.get();
   }

   page->pageLedgers.clear();

   //load page's block range from ssh and build ledgers
   if (txioMap != nullptr) {
      *txioMap = getTxio(page->blockStart, page->blockEnd);
      page->pageLedgers.update(
         buildLedgers(*txioMap, page->blockStart, page->blockEnd));
   } else {
      auto txio = getTxio(page->blockStart, page->blockEnd);
      page->pageLedgers.update(
         buildLedgers(txio, page->blockStart, page->blockEnd));
   }

   if (updateID != UINT32_MAX) {
      page->updateID = updateID;
   }
   return page->pageLedgers.get();
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<const std::map<BinaryData, Entry>>
HistoryPager::getPageLedgerMap(uint32_t pageId)
{
   if (!isInitialized_->load(std::memory_order_relaxed)) {
      LOGERR << "Uninitialized history";
      throw std::runtime_error("Uninitialized history");
   }

   auto pagesLocal = pages_.load(std::memory_order_acquire);
   if (pagesLocal == nullptr) {
      return nullptr;
   }

   if (pageId >= pagesLocal->size()) {
      return nullptr;
   }
   auto& page = (*pagesLocal)[pageId];

   if (!page->pageLedgers.empty()) {
      //already loaded this page
      return page->pageLedgers.get();
   } else {
      return nullptr;
   }
}

////////////////////////////////////////////////////////////////////////////////
bool HistoryPager::mapHistory(
   std::function<std::map<uint32_t, uint32_t>()> getSSHsummary)
{
   //grab the ssh summary for the pager. This is a map, referencing the amount
   //of txio per block for the given address.
   std::map<uint32_t, uint32_t> newSummary;
   try {
      newSummary = move(getSSHsummary());
   } catch (const AlreadyPagedException&) {
      return false;
   }

   reset();
   SSHsummary_.clear();
   SSHsummary_ = std::move(newSummary);
   auto newPages = std::make_shared<std::vector<std::shared_ptr<Page>>>();
   if (SSHsummary_.empty()) {
      addPage(*newPages, 0, 0, UINT32_MAX);
      pages_.store(newPages, std::memory_order_release);
      isInitialized_->store(true, std::memory_order_relaxed);
      return true;
   }

   auto histIter = SSHsummary_.crbegin();
   uint32_t threshold = 0;
   uint32_t top = UINT32_MAX;
   while (histIter != SSHsummary_.crend()) {
      threshold += histIter->second;
      if (threshold > txnPerPage_) {
         addPage(*newPages, threshold, histIter->first, top);
         threshold = 0;
         top = histIter->first - 1;
      }
      ++histIter;
   }

   if (threshold != 0) {
      addPage(*newPages, threshold, 0, top);
   }

   //sort pages canonically then store
   sortPages(*newPages);
   pages_.store(newPages, std::memory_order_release);

   //mark as initialized
   isInitialized_->store(true, std::memory_order_relaxed);
   return true;
}

////////////////////////////////////////////////////////////////////////////////
uint32_t HistoryPager::getPageBottom(uint32_t id) const
{
   if (!isInitialized_->load(std::memory_order_relaxed)) {
      return 0;
   }
   auto pagesLocal = pages_.load(std::memory_order_acquire);
   if (pagesLocal == nullptr) {
      return 0;
   }
   if (id < pagesLocal->size()) {
      return (*pagesLocal)[id]->blockStart;
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
size_t HistoryPager::getPageCount(void) const
{
   if (!isInitialized_->load(std::memory_order_relaxed)) {
      return 0;
   }

   auto pagesLocal = pages_.load(std::memory_order_acquire);
   if (pagesLocal == nullptr) {
      return 0;
   }
   return pagesLocal->size();
}

////////////////////////////////////////////////////////////////////////////////
uint32_t HistoryPager::getRangeForHeightAndCount(
   uint32_t height, uint32_t count) const
{
   if (!isInitialized_->load(std::memory_order_relaxed)) {
      LOGERR << "Uninitialized history";
      throw std::runtime_error("Uninitialized history");
   }

   auto pagesLocal = pages_.load(std::memory_order_acquire);
   if (pagesLocal == nullptr) {
      return 0;
   }

   uint32_t total = 0;
   uint32_t top = 0;
   for (const auto& page : *pagesLocal) {
      if (page->blockEnd > height) {
         total += page->count;
         top = page->blockEnd;
         if (total > count) {
            break;
         }
      }
   }
   return top;
}

////////////////////////////////////////////////////////////////////////////////
uint32_t HistoryPager::getBlockInVicinity(uint32_t blk) const
{
   if (!isInitialized_->load(std::memory_order_relaxed)) {
      LOGERR << "Uninitialized history";
      throw std::runtime_error("Uninitialized history");
   }

   uint32_t blkDiff = UINT32_MAX;
   uint32_t blkHeight = UINT32_MAX;
   for (auto& txioRange : SSHsummary_) {
      //look for txio summary with closest block
      uint32_t diff = std::abs(int(txioRange.first - blk));
      if (diff == 0) {
         return txioRange.first;
      } else if (diff < blkDiff) {
         blkHeight = txioRange.first;
         blkDiff = diff;
      }
   }
   return blkHeight;
}

////////
uint32_t HistoryPager::getPageIdForBlockHeight(uint32_t blk) const
{
   if (!isInitialized_->load(std::memory_order_relaxed)) {
      LOGERR << "Uninitialized history";
      throw std::runtime_error("Uninitialized history");
   }

   unsigned i = 0;
   auto pagesLocal = pages_.load(std::memory_order_acquire);
   if (pagesLocal == nullptr) {
      return 0;
   }
   for (const auto& page : *pagesLocal) {
      if (blk >= page->blockStart && blk <= page->blockEnd) {
         return i;
      }
      ++i;
   }
   return 0;
}
