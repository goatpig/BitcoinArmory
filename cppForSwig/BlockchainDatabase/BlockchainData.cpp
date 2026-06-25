////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2026, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cstring>

#include "BlockchainData.h"
#include <Utils/ArmoryConfig.h>
#include <Utils/FileUtils.h>
#include <Utils/BCTX.h>
#include <Utils/varint.h>
#include <Utils/BinaryData.h>
#include <TxClasses.h>

#include "Blockchain.h"
#include "BlockDataMap.h"

using namespace Armory;

namespace {
   std::pair<std::vector<uint8_t>, size_t> getRawBlockData(
      std::shared_ptr<BlockHeader> header)
   {
      //open block file
      auto path = FileUtils::getBlkFilename(
         Config::Pathing::blkFilePath(), header->getBlockFileNum());
      auto fileMap = FileUtils::FileMap(path, false);

      if (!Config::DBSettings::isXored()) {
         //file isn't xored, return copy of the data directly
         std::vector<uint8_t> result;
         result.resize(header->getBlockSize());
         std::memcpy((uint8_t*)&result[0],
            fileMap.ptr() + header->getOffset(),
            header->getBlockSize());
         return { std::move(result), 0 };
      } else {
         /*
         XOR chunks are 8 bytes aligned. Block data is packed tight,
         therefor the start of a block is not 8 aligned.

         Copy 8 bytes aligned data around the block, xor it, then read
         from the block start offset (ignore the bytes preceeding the block
         that were carried over for alignement purposes)
         */
         std::vector<uint8_t> xoredData;
         size_t prepad = header->getOffset() % 8;
         size_t count = (prepad + header->getBlockSize() + 7) / 8;
         xoredData.resize(count * 8);
         std::memcpy(&xoredData[0],
            fileMap.ptr() + header->getOffset() - prepad,
            header->getBlockSize() + prepad);

         auto xorkey = Config::DBSettings::getXorKey();
         for (auto& chunk : xoredData) {
            chunk ^= xorkey;
         }
         return { std::move(xoredData), prepad };
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
// exceptions
BlockchainDataException::BlockchainDataException(const std::string& err) :
   std::runtime_error{err}
{}

////////////////////////////////////////////////////////////////////////////////
// BlockchainData
BlockchainData::BlockchainData(std::shared_ptr<Blockchain> bcPtr) :
   blockchain_{bcPtr}
{}

Tx BlockchainData::getTx(const Types::TxKey& key) const
{
   auto blockId = Types::getBlockIDFromTxKey(key);
   auto txId = Types::getTxIndexFromTxKey(key);
   return getTx(blockId, txId);
}

Tx BlockchainData::getTx(const Types::BlockId& blockId, Types::TxId txId) const
{
   //grab header
   auto header = blockchain_->getHeaderById(blockId);
   if (header == nullptr) {
      throw BlockchainDataException{
         std::format("invalid block key: {}", blockId)};
   }

   if (txId >= header->getNumTx()) {
      throw BlockchainDataException(std::format(
         "[blockId {}] txid > numTx ({}, {})",
         blockId, txId, header->getNumTx()));
   }

   auto rawBlockData = getRawBlockData(header);
   try {
      auto block = BlockData::deserialize(
         &rawBlockData.first[0] + rawBlockData.second,
         header->getBlockSize(), header,
         BlockData::CheckHashes::NoChecks);

      const auto& bctx = block->getTxns()[txId];
      Tx tx{bctx->data_, bctx->size_};
      tx.setTxKey(Types::constructTxKey(blockId, txId));
      return tx;
   } catch (const BtcUtils::BlockDeserializingException&) {
      throw BlockchainDataException(std::format(
         "failed to grab tx {}|{}", blockId, txId));
   }
}

////////
Hash32 BlockchainData::getTxHashForTxKey(const Types::TxKey& txKey) const
{
   auto tx = getTx(txKey);
   return Hash32{tx.getThisHash()};
}

bool BlockchainData::isTxKeyOnMainBranch(const Types::TxKey& txKey) const
{
   auto blockID = Types::getBlockIDFromTxKey(txKey);
   auto header = blockchain_->getHeaderById(blockID);
   if (header == nullptr) {
      return false;
   }
   return header->isMainBranch();
}

////////
std::pair<std::vector<uint8_t>, size_t> BlockchainData::getRawBlockForId(
   Types::BlockId blockId) const
{
   //grab header
   auto header = blockchain_->getHeaderById(blockId);
   if (header == nullptr) {
      throw BlockchainDataException{
         std::format("invalid block key: {}", blockId)};
   }
   return getRawBlockForHeader(header);
}

std::pair<std::vector<uint8_t>, size_t> BlockchainData::getRawBlockForHeader(
   std::shared_ptr<BlockHeader> header) const
{
   return getRawBlockData(header);
}
