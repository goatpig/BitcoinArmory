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

#include <Utils/Types.h>
#include <Utils/BinaryData.h>

////////////////////////////////////////////////////////////////////////////////
class TxRef
{
private:
   BinaryData dbKey6B_;

public:
   TxRef(void);
   TxRef(BinaryDataRef);

   bool isInitialized(void) const;
   const BinaryData& getDBKey(void) const;
   BinaryDataRef getDBKeyRef(void) const;
   BinaryData getDBKeyOfChild(uint16_t) const;
   uint16_t getTxIndex(void) const;
   uint32_t getBlockHeight(void) const;
   uint8_t getDuplicateID(void) const;

   bool operator==(const BinaryData&) const;
   bool operator==(const TxRef&) const;
   bool operator>=(const BinaryData&) const;
};

////////////////////////////////////////////////////////////////////////////////
class TxIOPair
{
public:
   TxIOPair(Armory::Types::TxIOKey, Armory::Types::Amount,
      const Armory::Types::ScrAddr&);
   TxIOPair(Armory::Types::TxIOKey, Armory::Types::Amount,
      const Armory::Types::ScrAddr&, Armory::Types::TxIOKey);

   bool hasTxOutZC(void) const;
   bool hasTxIn(void) const;
   bool hasTxInZC(void) const;

   Armory::Types::Amount getAmount(void) const;
   const Armory::Types::ScrAddr& getScrAddr(void) const;
   Armory::Types::TxKey getTxKeyOfOutput(void) const;
   Armory::Types::TxKey getTxKeyOfInput(void) const;
   Armory::Types::TxIOKey getTxIOKeyOfOutput(void) const;
   Armory::Types::TxIOKey getTxIOKeyOfInput(void) const;
   Armory::Types::TxIOId getIndexOfOutput(void) const;
   Armory::Types::TxIOId getIndexOfInput(void) const;

   void setTxIn(Armory::Types::TxKey, Armory::Types::TxId);
   void setTxIn(Armory::Types::TxIOKey);
   void merge(const TxIOPair&);

   void setTxTime(uint32_t);
   void setRBF(bool);
   void setChained(bool);

   uint32_t getTxTime(void) const;
   bool isRBF(void) const;
   bool isChained(void) const;

private:
   const Armory::Types::TxIOKey txIOKeyOfOutput_;
   const Armory::Types::Amount amount_;
   const Armory::Types::ScrAddr scrAddr_;
   Armory::Types::TxIOKey txIOKeyOfInput_ = Armory::Types::INVALID_TXIO_KEY;

   //ZC only members
   uint32_t txTime_ = UINT32_MAX;
   bool isRBF_ = false;
   bool isZCChained_ = false;
};
