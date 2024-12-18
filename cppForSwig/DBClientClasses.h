////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2024, goatpig.                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_CLIENTCLASSES
#define _H_CLIENTCLASSES

#include <exception>
#include <string>
#include <functional>

#include "BinaryData.h"
#include "SocketObject.h"
#include "nodeRPC.h"

#define FILTER_CHANGE_FLAG "wallet_filter_changed"

namespace AsyncClient {
   class BlockDataViewer;
};

namespace capnp {
   class MessageReader;
}

///////////////////////////////////////////////////////////////////////////////
struct NoArmoryDBExcept : public std::runtime_error
{
   NoArmoryDBExcept(void) : runtime_error("")
   {}
};

struct BDVAlreadyRegistered : public std::runtime_error
{
   BDVAlreadyRegistered(void) : std::runtime_error("")
   {}
};

///////////////////////////////////////////////////////////////////////////////
namespace DBClientClasses
{
   void initLibrary(void);

   ///////////////////////////////////////////////////////////////////////////////
   struct FeeEstimateStruct
   {
      std::string error_;
      float val_ = 0;
      bool isSmart_ = false;

      FeeEstimateStruct(float val, bool isSmart, const std::string& error) :
         error_(error), val_(val), isSmart_(isSmart)
      {}

      FeeEstimateStruct(void)
      {}
   };

   ///////////////////////////////////////////////////////////////////////////////
   class BlockHeader
   {
      friend class Blockchain;
      friend class testBlockHeader;
      friend class BlockData;

   private:

      void unserialize(uint8_t const * ptr, uint32_t size);
      void unserialize(BinaryDataRef const & str)
      {
         unserialize(str.getPtr(), str.getSize());
      }

   public:

      BlockHeader(void) {}
      BlockHeader(const BinaryData&, unsigned);

      uint32_t           getVersion(void) const { return READ_UINT32_LE(getPtr()); }
      BinaryData const & getThisHash(void) const { return thisHash_; }
      BinaryData         getPrevHash(void) const { return BinaryData(getPtr() + 4, 32); }
      BinaryData         getMerkleRoot(void) const { return BinaryData(getPtr() + 36, 32); }
      BinaryData         getDiffBits(void) const { return BinaryData(getPtr() + 72, 4); }
      uint32_t           getTimestamp(void) const { return READ_UINT32_LE(getPtr() + 68); }
      uint32_t           getNonce(void) const { return READ_UINT32_LE(getPtr() + 76); }
      uint32_t           getBlockHeight(void) const { return blockHeight_; }

      //////////////////////////////////////////////////////////////////////////
      BinaryDataRef  getThisHashRef(void) const { return thisHash_.getRef(); }
      BinaryDataRef  getPrevHashRef(void) const { return BinaryDataRef(getPtr() + 4, 32); }
      BinaryDataRef  getMerkleRootRef(void) const { return BinaryDataRef(getPtr() + 36, 32); }
      BinaryDataRef  getDiffBitsRef(void) const { return BinaryDataRef(getPtr() + 72, 4); }

      //////////////////////////////////////////////////////////////////////////
      uint8_t const * getPtr(void) const {
         if (!isInitialized_)
            throw std::runtime_error("uninitialized BlockHeader");
         return dataCopy_.getPtr();
      }
      size_t        getSize(void) const {
         if (!isInitialized_)
            throw std::runtime_error("uninitialized BlockHeader");
         return dataCopy_.getSize();
      }
      bool            isInitialized(void) const { return isInitialized_; }

      void clearDataCopy() { dataCopy_.resize(0); }

   private:
      BinaryData     dataCopy_;
      bool           isInitialized_ = false;
      // Specific to the DB storage
      uint32_t       blockHeight_ = UINT32_MAX;

      // Derived properties - we expect these to be set after construct/copy
      BinaryData     thisHash_;
      double         difficultyDbl_ = 0.0;
   };

   ////////////////////////////////////////////////////////////////////////////
   class LedgerEntry
   {
   private:
      const std::string id_;
      const int64_t     value_;
      const uint32_t    blockHeight_;
      const BinaryData  txHash_;
      const uint32_t    txOutIndex_;
      const uint32_t    timestamp_; //seconds
      const bool        isCoinbase_;
      const bool        isSentToSelf_;
      const bool        isChangeBack_;
      const bool        isOptInRBF_;
      const bool        isChainedZC_;
      const bool        isWitness_;

      const std::vector<BinaryData> scrAddrList_;

   public:
      LedgerEntry(const std::string& id, int64_t value, uint32_t blockHeight,
         BinaryData& txHash, uint32_t txOutIndex, uint32_t timestamp,
         bool isCoinbase, bool isSentToSelf, bool isChangeBack,
         bool isOptInRBF, bool isChainedZC, bool isWitness,
         std::vector<BinaryData>& scrAddrList);

      const std::string&  getID(void) const;
      int64_t             getValue(void) const;
      uint32_t            getBlockHeight(void) const;
      BinaryDataRef       getTxHash(void) const;
      uint32_t            getTxOutIndex(void) const;
      uint32_t            getTxTime(void) const;
      bool                isCoinbase(void) const;
      bool                isSentToSelf(void) const;
      bool                isChangeBack(void) const;
      bool                isOptInRBF(void) const;
      bool                isChainedZC(void) const;
      bool                isWitness(void) const;

      const std::vector<BinaryData>& getScrAddrList(void) const;

      bool operator==(const LedgerEntry& rhs);
   };
   using HistoryPage = std::vector<LedgerEntry>;

   ////////////////////////////////////////////////////////////////////////////
   class NodeChainStatus
   {
   private:
      const CoreRPC::ChainState chainState_;
      const float blockSpeed_;
      const float progressPct_;
      const uint64_t etaSeconds_;
      const unsigned blocksLeft_;

   public:
      NodeChainStatus(void);
      NodeChainStatus(CoreRPC::ChainState, float, float, uint64_t, unsigned);
      NodeChainStatus(NodeChainStatus&&) = default;

      CoreRPC::ChainState state(void) const;
      float getBlockSpeed(void) const;

      float getProgressPct(void) const;
      uint64_t getETA(void) const;
      unsigned getBlocksLeft(void) const;
   };

   ////////////////////////////////////////////////////////////////////////////
   class NodeStatus
   {
   private:
      const CoreRPC::NodeState nodeState_;
      const CoreRPC::RpcState rpcState_;
      const bool isSegWitEnabled_;
      const NodeChainStatus nodeChainStatus_;

   public:
      NodeStatus(CoreRPC::NodeState, CoreRPC::RpcState, bool, NodeChainStatus&);

      CoreRPC::NodeState state(void) const;
      bool isSegWitEnabled(void) const;
      CoreRPC::RpcState rpcState(void) const;
      const NodeChainStatus& chainStatus(void) const;
   };
}; //namespace DBClientClasses

///////////////////////////////////////////////////////////////////////////////
struct BdmNotification
{
   const BDMAction action_;

   unsigned height_;
   unsigned branchHeight_ = UINT32_MAX;

   std::set<BinaryData> invalidatedZc_;
   std::vector<std::shared_ptr<DBClientClasses::LedgerEntry>> ledgers_;

   std::vector<BinaryData> ids_;

   std::shared_ptr<DBClientClasses::NodeStatus> nodeStatus_;
   BDV_Error_Struct error_;

   std::string requestID_;

   BdmNotification(BDMAction action) :
      action_(action)
   {}
};

///////////////////////////////////////////////////////////////////////////////
class RemoteCallback
{
public:
   RemoteCallback(void) {}
   virtual ~RemoteCallback(void) = 0;

   virtual void run(BdmNotification) = 0;
   virtual void progress(
      BDMPhase phase,
      const std::vector<std::string> &walletIdVec,
      float progress, unsigned secondsRem,
      unsigned progressNumeric
   ) = 0;
   virtual void disconnected(void) = 0;

   bool processNotifications(std::unique_ptr<capnp::MessageReader>);
};

#endif
