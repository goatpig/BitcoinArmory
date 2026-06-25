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

#include <cstring>

#include "txio.h"
#include <Utils/BtcUtils.h>
#include <Utils/DBUtils.h>

using namespace Armory;

/////////////////////////////////////////////////////////////////////////////
// TxRef methods
TxRef::TxRef() :
   dbKey6B_{}
{}

TxRef::TxRef(BinaryDataRef bdr) :
   dbKey6B_{bdr.getSliceRef(0, 6)}
{}

////////
bool TxRef::isInitialized() const
{
   return !dbKey6B_.empty();
}

////////
bool TxRef::operator==(const BinaryData& dbkey) const
{
   return dbKey6B_ == dbkey;
}

bool TxRef::operator==(const TxRef& txr) const
{
   return dbKey6B_ == txr.dbKey6B_;
}

bool TxRef::operator>=(const BinaryData& dbkey) const
{
   return dbKey6B_ >= dbkey;
}

////////
uint32_t TxRef::getBlockHeight() const
{
   if (dbKey6B_.getSize() == 6 &&
      !dbKey6B_.startsWith(DBUtils::ZCPrefix)) {
      return DBUtils::hgtxToHeight(dbKey6B_.getSliceCopy(0, 4));
   } else {
      return UINT32_MAX;
   }
}

uint8_t TxRef::getDuplicateID() const
{
   if (dbKey6B_.getSize() == 6) {
      return DBUtils::hgtxToDupID(dbKey6B_.getSliceCopy(0, 4));
   } else {
      return UINT8_MAX;
   }
}

uint16_t TxRef::getTxIndex() const
{
   if (dbKey6B_.getSize() == 6) {
      if (!dbKey6B_.startsWith(DBUtils::ZCPrefix)) {
         return READ_UINT16_BE(dbKey6B_.getPtr() + 4);
      } else {
         return READ_UINT32_BE(dbKey6B_.getPtr() + 2);
      }
   } else {
      return UINT16_MAX;
   }
}

////////
const BinaryData& TxRef::getDBKey() const
{
   return dbKey6B_;
}

BinaryDataRef TxRef::getDBKeyRef() const
{
   return dbKey6B_.getRef();
}

BinaryData TxRef::getDBKeyOfChild(uint16_t i) const
{
   return dbKey6B_ + WRITE_UINT16_BE(i);
}

////////////////////////////////////////////////////////////////////////////////
// TxIOPair
TxIOPair::TxIOPair(Types::TxIOKey txOutKey, uint64_t amount,
   const Types::ScrAddr& scrAddr) :
   txIOKeyOfOutput_(txOutKey), amount_{amount}, scrAddr_{scrAddr}
{}

TxIOPair::TxIOPair(Types::TxIOKey txOutKey, uint64_t amount,
   const Types::ScrAddr& scrAddr, Types::TxIOKey txInKey) :
   txIOKeyOfOutput_(txOutKey), amount_{amount}, scrAddr_{scrAddr},
   txIOKeyOfInput_(txInKey)
{}

////////
Types::Amount TxIOPair::getAmount() const
{
   return amount_;
}

uint32_t TxIOPair::getTxTime() const
{
   return txTime_;
}

const Types::ScrAddr& TxIOPair::getScrAddr() const
{
   return scrAddr_;
}

////////
bool TxIOPair::hasTxIn() const
{
   return Types::isTxKeyValid(txIOKeyOfInput_);
}

bool TxIOPair::hasTxOutZC() const
{
   return Types::isThisAZCKey(txIOKeyOfOutput_);
}

bool TxIOPair::hasTxInZC() const
{
   if (!hasTxIn()) {
      return false;
   }
   return Types::isThisAZCKey(txIOKeyOfInput_);
}

////////
Types::TxKey TxIOPair::getTxKeyOfOutput() const
{
   return Types::getTxKeyFromTxIOKey(txIOKeyOfOutput_);
}

Types::TxKey TxIOPair::getTxKeyOfInput() const
{
   if (!hasTxIn()) {
      return Types::INVALID_TX_KEY;
   }
   return Types::getTxKeyFromTxIOKey(txIOKeyOfInput_);
}

////////
Types::TxIOKey TxIOPair::getTxIOKeyOfOutput() const
{
   return txIOKeyOfOutput_;
}

Types::TxIOKey TxIOPair::getTxIOKeyOfInput() const
{
   return txIOKeyOfInput_;
}

Types::TxIOId TxIOPair::getIndexOfOutput() const
{
   return Types::getTxIOIndexFromTxIOKey(txIOKeyOfOutput_);
}

Types::TxIOId TxIOPair::getIndexOfInput() const
{
   if (!hasTxIn()) {
      return UINT16_MAX;
   }
   return Types::getTxIOIndexFromTxIOKey(txIOKeyOfInput_);
}

////////
void TxIOPair::setTxIn(
   Types::TxKey keyOfInput, Types::TxId indexOfInput)
{
   txIOKeyOfInput_ = Types::constructTxIOKeyFromTxKey(
      keyOfInput, indexOfInput);
}

void TxIOPair::setTxIn(Types::TxIOKey txInKey)
{
   txIOKeyOfInput_ = txInKey;
}

////////
void TxIOPair::setTxTime(uint32_t txtime)
{
   txTime_ = txtime;
}

void TxIOPair::setRBF(bool rbf)
{
   isRBF_ = rbf;
}

void TxIOPair::setChained(bool chained)
{
   isZCChained_ = chained;
}

bool TxIOPair::isRBF() const
{
   return isRBF_;
}

bool TxIOPair::isChained() const
{
   return isZCChained_;
}

////////
void TxIOPair::merge(const TxIOPair& rhs)
{
   setTxIn(rhs.txIOKeyOfInput_);

   isRBF_         = rhs.isRBF_;
   isZCChained_   = rhs.isZCChained_;
   txTime_        = rhs.txTime_;
}
