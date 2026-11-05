////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BCTX.h"
#include "BtcUtils.h"

using namespace Armory;

////////////////////////////////////////////////////////////////////////////////
// BCTX
BCTX::BCTX(const uint8_t* data, size_t size) :
   data_(data), size_(size)
{}

BCTX::BCTX(const BinaryDataRef& bdr) :
   data_(bdr.getPtr()), size_(bdr.getSize())
{}

////////
const BinaryData& BCTX::getHash() const
{
   if (txHash_.empty()) {
      if (usesWitness_) {
         BinaryData noWitData;
         BinaryDataRef version(data_, 4);

         // witness data starts right after the last txout, the first
         // witness tells where without assuming there is a txout
         size_t witnessOffset = witnesses_.empty() ?
            size_ - 4 : witnesses_.front().first;
         BinaryDataRef txinout(data_ + 6, witnessOffset - 6);
         BinaryDataRef locktime(data_ + size_ - 4, 4);

         noWitData.append(version);
         noWitData.append(txinout);
         noWitData.append(locktime);

         BtcUtils::getHash256(noWitData, txHash_);
      } else {
         BinaryDataRef hashdata{data_, size_};
         BtcUtils::getHash256(hashdata, txHash_);
      }
   }
   return txHash_;
}

BinaryData&& BCTX::moveHash()
{
   getHash();
   return std::move(txHash_);
}

////////
BinaryDataRef BCTX::getTxInRef(unsigned inputId) const
{
   if (inputId >= txins_.size()) {
      throw std::range_error("txin index overflow");
   }
   auto txinIter = txins_.cbegin() + inputId;
   return BinaryDataRef{
      data_ + (*txinIter).first,
      (*txinIter).second
   };
}

BinaryDataRef BCTX::getTxOutRef(unsigned outputId) const
{
   if (outputId >= txouts_.size()) {
      throw std::range_error("txout index overflow");
   }
   auto txoutIter = txouts_.cbegin() + outputId;

   return BinaryDataRef{
      data_ + (*txoutIter).first,
      (*txoutIter).second
   };
}

////////
std::shared_ptr<BCTX> BCTX::parse(BinaryRefReader brr, unsigned id)
{
   return parse(brr.getCurrPtr(), brr.getSizeRemaining(), id);
}

std::shared_ptr<BCTX> BCTX::parse(const uint8_t* data, size_t len, unsigned id)
{
   std::vector<size_t> offsetIns, offsetOuts, offsetsWitness;
   auto txlen = BtcUtils::TxCalcLength(
      data, len,
      &offsetIns, &offsetOuts, &offsetsWitness);
   auto txPtr = std::make_shared<BCTX>(data, txlen);
   txPtr->version_ = READ_UINT32_LE(data);

   //check the marker and flag for witness transaction
   txPtr->usesWitness_ = BtcUtils::checkSwMarker(data + 4);

   //convert offsets to offset + size pairs
   txPtr->txins_.reserve(offsetIns.size() - 1);
   for (unsigned int y = 0; y < offsetIns.size() - 1; y++) {
      txPtr->txins_.emplace_back(OffsetAndSize{
         offsetIns[y],
         offsetIns[y + 1] - offsetIns[y]
      });
   }

   txPtr->txouts_.reserve(offsetOuts.size() - 1);
   for (unsigned int y = 0; y < offsetOuts.size() - 1; y++) {
      txPtr->txouts_.emplace_back(OffsetAndSize{
         offsetOuts[y],
         offsetOuts[y + 1] - offsetOuts[y]
      });
   }

   if (txPtr->usesWitness_) {
      txPtr->witnesses_.reserve(offsetsWitness.size() - 1);
      for (unsigned int y = 0; y < offsetsWitness.size() - 1; y++) {
         txPtr->witnesses_.emplace_back(OffsetAndSize{
            offsetsWitness[y],
            offsetsWitness[y + 1] - offsetsWitness[y]
         });
      }
   }

   txPtr->lockTime_ = READ_UINT32_LE(data + offsetsWitness.back());
   if (id != UINT32_MAX) {
      txPtr->isCoinbase_ = (id == 0);
   } else if (txPtr->txins_.size() == 1) {
      auto txinref = txPtr->getTxInRef(0);
      auto bdr = txinref.getSliceRef(0, 32);
      if (bdr == BtcUtils::EmptyHash) {
         txPtr->isCoinbase_ = true;
      }
   }
   return txPtr;
}
