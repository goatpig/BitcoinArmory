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

#include <set>
#include <map>
#include <Utils/BinaryData.h>
#include <Utils/Types.h>

class LMDBBlockDatabase;
class TxIOPair;

////////////////////////////////////////////////////////////////////////////////
//
// NOTE: Legacy comment from etotheipi, kept for historical value, doesn't
//       apply modern db design, as no very little processing is done in
//       this class now
//
// ScrAddrObj
//
// This class is only for scanning the blockchain (information only).  It has
// no need to keep track of the public and private keys of various addresses,
// which is done by the python code leveraging this class.
//
// I call these as "scraddresses".  In most contexts, it represents an
// "address" that people use to send coins per-to-person, but it could actually
// represent any kind of TxOut script.  Multisig, P2SH, or any non-standard,
// unusual, escrow, whatever "address."  While it might be more technically
// correct to just call this class "Script" or "TxOutScript", I felt like
// "address" is a term that will always exist in the Bitcoin ecosystem, and
// frequently used even when not preferred.
//
// Similarly, we refer to the member variable scraddr_ as a "scradder". It
// is actually a reduction of the TxOut script to a form that is identical
// regardless of whether pay-to-pubkey or pay-to-pubkey-hash is used.
//
////////////////////////////////////////////////////////////////////////////////
class ScrAddrObj
{
private:
   const Armory::Types::ScrAddr scrAddr_; //includes the prefix byte!
   const Armory::Types::ScrAddrId id_;

   std::map<Armory::Types::TxIOKey, TxIOPair> txioCache_;

private:
   void updateTxIOCache(
      LMDBBlockDatabase*, const std::set<Armory::Types::BlockId>&,
      Armory::Types::BlockId, Armory::Types::BlockId);

public:
   ScrAddrObj(const Armory::Types::ScrAddr&, Armory::Types::ScrAddrId);

   const Armory::Types::ScrAddr& getScrAddr(void) const;
   std::map<Armory::Types::TxIOKey, TxIOPair> getTxios(
      LMDBBlockDatabase*, const std::set<Armory::Types::BlockId>&,
      Armory::Types::BlockId, Armory::Types::BlockId);
};
