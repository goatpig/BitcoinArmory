////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _TXHASHFILTERS_H_
#define _TXHASHFILTERS_H_

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <unordered_map>

#include "BinaryData.h"

////////////////////////////////////////////////////////////////////////////////
struct TxFilterException : public std::runtime_error
{
   TxFilterException(const std::string& err) : std::runtime_error(err)
   {}
};

////////////////////////////////////////////////////////////////////////////////
struct TxHashHints
{
   const BinaryData hash_;

   //map<blockId, set<tx id>>
   std::map<uint32_t, std::set<uint32_t>> filterHits_= {};

   TxHashHints(const BinaryData& hash) :
      hash_(hash)
   {}
};

////
struct TxHashHintsComparator
{
   using is_transparent = std::true_type;

   bool operator()(const TxHashHints& left, const TxHashHints& right) const
   {
      return left.hash_ < right.hash_;
   }

   bool operator()(const TxHashHints& left, const BinaryData& right) const
   {
      return left.hash_ < right;
   }

   bool operator()(const BinaryData& left, const TxHashHints& right) const
   {
      return left < right.hash_;
   }
};

////////////////////////////////////////////////////////////////////////////////
struct BlockHashVector
{
public:
   bool isValid_{false};
   uint32_t blockKey_;
   size_t len_ = SIZE_MAX;

   std::vector<uint32_t> filterVector_;
   const uint8_t* filterPtr_ = nullptr;

public:
   //tors
   BlockHashVector(uint32_t);
   BlockHashVector(const BlockHashVector&);
   BlockHashVector(BlockHashVector&&);

   //get
   bool isValid(void) const { return isValid_; }
   uint32_t getBlockKey(void) const { return blockKey_; }

   std::set<uint32_t> compare(const BinaryData&) const;
   std::set<uint32_t> compare(uint32_t) const;

   //set
   void update(const BinaryData&);
   void update(const std::vector<BinaryData>&);
   void reserve(size_t);

   //io
   void serialize(BinaryWriter&) const;
   static BlockHashVector deserialize(const uint8_t*);
};

////////////////////////////////////////////////////////////////////////////////
struct BlockHashMap
{
public:
   bool isValid_{false};
   const uint32_t blockKey_;
   size_t len_{0};

   std::unordered_map<uint32_t, std::set<uint32_t>> filterMap_;

public:
   //tors
   BlockHashMap(uint32_t);
   BlockHashMap(const BlockHashMap&);
   BlockHashMap(BlockHashMap&&);

   //get
   bool isValid(void) const { return isValid_; }
   uint32_t getBlockKey(void) const { return blockKey_; }

   std::set<uint32_t> compare(const BinaryData&) const;
   std::set<uint32_t> compare(uint32_t) const;

   //set
   void update(const BinaryData&);
   void update(const std::vector<BinaryData>&);

   //io
   static BlockHashMap deserialize(const uint8_t*);
};

////////////////////////////////////////////////////////////////////////////////
enum class TxFilterPoolMode
{
   Auto,
   Bucket_Vector,
   Bucket_Map,
   Pool_Map
};

using TxHashHintsSet = std::set<TxHashHints, TxHashHintsComparator>;

////
class TxFilterPoolWriter
{
   //bucket filter for transactions hash lookup
   //each bucket represents one blk file

private:
   const BinaryDataRef dataRef_;
   std::map<uint32_t, BlockHashVector> pool_;

public:
   //tors
   TxFilterPoolWriter(void);
   TxFilterPoolWriter(std::map<uint32_t, BlockHashVector>&);
   TxFilterPoolWriter(const TxFilterPoolWriter&);
   TxFilterPoolWriter(TxFilterPoolWriter&&);
   TxFilterPoolWriter(BinaryDataRef);

   //helpers
   bool isValid(void) const;
   void update(const std::map<uint32_t, BlockHashVector>&);
   void update(const std::map<uint32_t, std::shared_ptr<BlockHashVector>>&);
   void update(const std::map<uint32_t, BlockHashMap>&);

   //io
   void serialize(BinaryWriter&) const;
};

class TxFilterPoolReader
{
private:
   const BinaryDataRef dataRef_;

   std::map<uint32_t, BlockHashMap> poolMap_;
   std::unordered_map<uint32_t,
      std::map<uint32_t, std::set<uint32_t>>> fullMap_;

public:
   //tors
   TxFilterPoolReader(void);
   TxFilterPoolReader(std::map<uint32_t, BlockHashVector>&);
   TxFilterPoolReader(const TxFilterPoolReader&);
   TxFilterPoolReader(TxFilterPoolReader&&);
   TxFilterPoolReader(BinaryDataRef, TxFilterPoolMode);

   //helpers
   bool isValid(void) const;

   //getters
   std::map<uint32_t, std::set<uint32_t>> compare(const BinaryData&) const;

   //multithreaded search
   static std::map<uint32_t, TxHashHintsSet> scanHashes(
      uint32_t, const std::function<BinaryDataRef(uint32_t)>&,
      const std::set<BinaryData>&, TxFilterPoolMode);
};
#endif