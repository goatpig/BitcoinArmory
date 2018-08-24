////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018, goatpig.                                              //
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
#include "BDVCodec.h"
#include "nodeRPC.h"

namespace AsyncClient
{
   class BlockDataViewer;
};
   
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
class RemoteCallback
{
public:
   RemoteCallback(void) {}
   virtual ~RemoteCallback(void) = 0;

   virtual void run(BDMAction action, void* ptr, int block = 0) = 0;
   virtual void progress(
      BDMPhase phase,
      const vector<string> &walletIdVec,
      float progress, unsigned secondsRem,
      unsigned progressNumeric
   ) = 0;
   virtual void disconnected(void) = 0;

   bool processNotifications(shared_ptr<::Codec_BDVCommand::BDVCallback>);
};

///////////////////////////////////////////////////////////////////////////////
namespace ClientClasses
{

   ///////////////////////////////////////////////////////////////////////////////
   struct FeeEstimateStruct
   {
      std::string error_;
      float val_ = 0;
      bool isSmart_ = false;

      FeeEstimateStruct(float val, bool isSmart, const std::string& error) :
         val_(val), isSmart_(isSmart), error_(error)
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
            throw runtime_error("uninitialized BlockHeader");
         return dataCopy_.getPtr();
      }
      size_t        getSize(void) const {
         if (!isInitialized_)
            throw runtime_error("uninitialized BlockHeader");
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
      shared_ptr<::google::protobuf::Message> msgPtr_;
      const ::Codec_LedgerEntry::LedgerEntry* ptr_ = nullptr;

   public:
      LedgerEntry(BinaryDataRef bdr);
      LedgerEntry(shared_ptr<::Codec_LedgerEntry::LedgerEntry>);
      LedgerEntry(shared_ptr<::Codec_LedgerEntry::ManyLedgerEntry>,
         unsigned);
      LedgerEntry(shared_ptr<::Codec_BDVCommand::BDVCallback>,
         unsigned, unsigned);

      const string&       getID(void) const;
      int64_t             getValue(void) const;
      uint32_t            getBlockNum(void) const;
      BinaryDataRef       getTxHash(void) const;
      uint32_t            getIndex(void) const;
      uint32_t            getTxTime(void) const;
      bool                isCoinbase(void) const;
      bool                isSentToSelf(void) const;
      bool                isChangeBack(void) const;
      bool                isOptInRBF(void) const;
      bool                isChainedZC(void) const;
      bool                isWitness(void) const;

      vector<BinaryDataRef> getScrAddrList(void) const;
   };

   ////////////////////////////////////////////////////////////////////////////
   class NodeChainState
   {
   private:
      shared_ptr<::google::protobuf::Message> msgPtr_;
      const ::Codec_NodeStatus::NodeChainState* ptr_;

   public:
      NodeChainState(shared_ptr<::Codec_NodeStatus::NodeStatus>);

      unsigned getTopBlock(void) const;
      ChainStatus state(void) const;
      float getBlockSpeed(void) const;

      float getProgressPct(void) const;
      uint64_t getETA(void) const;
      unsigned getBlocksLeft(void) const;
   };

   ////////////////////////////////////////////////////////////////////////////
   class NodeStatusStruct
   {
      friend class ::RemoteCallback;

   private:
      shared_ptr<::google::protobuf::Message> msgPtr_;
      const ::Codec_NodeStatus::NodeStatus* ptr_;
      
   private:
      NodeStatusStruct(shared_ptr<::Codec_BDVCommand::BDVCallback>, unsigned);

   public:
      NodeStatusStruct(BinaryDataRef);
      NodeStatusStruct(shared_ptr<::Codec_NodeStatus::NodeStatus>);

      NodeStatus status(void) const;
      bool isSegWitEnabled(void) const;
      RpcStatus rpcStatus(void) const;
      NodeChainState chainState(void) const;
   };

   ////////////////////////////////////////////////////////////////////////////
   class ProgressData
   {
      friend class ::RemoteCallback;

   private:
      shared_ptr<::google::protobuf::Message> msgPtr_;
      const ::Codec_NodeStatus::ProgressData* ptr_;

   private:
      ProgressData(shared_ptr<::Codec_BDVCommand::BDVCallback>, unsigned);

   public:
      ProgressData(BinaryDataRef);

      BDMPhase phase(void) const;
      double progress(void) const;
      unsigned time(void) const;
      unsigned numericProgress(void) const;
      vector<string> wltIDs(void) const;
   };
};

#endif
