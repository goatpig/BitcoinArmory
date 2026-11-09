////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <set>
#include <string>

#include <Utils/BinaryData.h>
#include <Utils/Types.h>
#include <BlockchainDatabase/txio.h>
#include <Node/RPC.h>
#include "bdmenums.h"

#define FILTER_CHANGE_FLAG "wallet_filter_changed"

namespace capnp {
   class MessageReader;
}

namespace Node {
   class Status;
}

struct BdvError;

////////////////////////////////////////////////////////////////////////////////
struct BDVAlreadyRegistered : public std::runtime_error
{
   BDVAlreadyRegistered(void) : std::runtime_error("")
   {}
};

////////////////////////////////////////////////////////////////////////////////
namespace DBClientClasses
{
   void initLibrary(void);

   struct BlockHeader
   {
      const BinaryData thisHash;
      const BinaryData prevHash;

      const Armory::Types::BlockId blockId;
      const uint32_t blockHeight;
      const uint32_t timestamp;
      const uint32_t blockSize;
      const uint32_t numTxs;
      bool           isMainBranch;

      BlockHeader(BinaryDataRef, BinaryDataRef,
         Armory::Types::BlockId,
         uint32_t, uint32_t, uint32_t, uint32_t,
         bool);
   };
}; //namespace DBClientClasses

///////////////////////////////////////////////////////////////////////////////
class NewBlockNotif
{
   using BlockIdVec = std::vector<Armory::Types::BlockId>;

private:
   uint32_t height_;
   uint32_t branchHeight_;
   BlockIdVec invalidatedBlockIds_;
   BlockIdVec newMainBranchBlockIds_;

public:
   NewBlockNotif(void);
   NewBlockNotif(uint32_t, uint32_t, BlockIdVec, BlockIdVec);

   bool isValid(void) const;
   bool isReorg(void) const;
   uint32_t getHeight(void) const;
   uint32_t getBranchHeight(void) const;

   const BlockIdVec& invalidatedBlockIds(void) const;
   const BlockIdVec& newMainBranchBlockIds(void) const;
};

////////
struct ServerPushHandler
{
   using SocketName = std::string;

   ServerPushHandler(void);
   virtual ~ServerPushHandler(void) = 0;

   bool parseCapnPayload(
      std::unique_ptr<capnp::MessageReader>, const SocketName&);

   virtual void pushRegistered(const SocketName&) const = 0;
   virtual void pushDisconnected(const SocketName&) const = 0;
   virtual void pushNodeStatus(const Node::Status&) const = 0;
   virtual void pushProgress(BDMPhase,
      const std::vector<std::string>&,
      float, unsigned, unsigned
   ) const = 0;

   virtual void pushReady(const SocketName&, const NewBlockNotif&) const = 0;
   virtual void pushNewBlock(const SocketName&, const NewBlockNotif&) const = 0;
   virtual void pushRefresh(const SocketName&, const std::set<std::string>&) const = 0;

   virtual void pushZeroConf(
      const SocketName&, const std::string&,
      std::vector<TxIOPair>,
      std::set<Armory::Types::TxHash>) const = 0;
   virtual void pushError(const BdvError&) const = 0;
};
