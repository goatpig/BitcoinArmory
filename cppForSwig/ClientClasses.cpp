////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ClientClasses.h"
#include "WebSocketClient.h"
#include "protobuf/BDVCommand.pb.h"
#include "btc/ecc.h"

using namespace std;
using namespace ClientClasses;
using namespace ::Codec_BDVCommand;


///////////////////////////////////////////////////////////////////////////////
void ClientClasses::initLibrary()
{
   startupBIP150CTX(4);
   startupBIP151CTX();
   btc_ecc_start();
}

///////////////////////////////////////////////////////////////////////////////
//
// BlockHeader
//
///////////////////////////////////////////////////////////////////////////////
ClientClasses::BlockHeader::BlockHeader(
   const BinaryData& rawheader, unsigned height)
{
   unserialize(rawheader.getRef());
   blockHeight_ = height;
}

////////////////////////////////////////////////////////////////////////////////
void ClientClasses::BlockHeader::unserialize(uint8_t const * ptr, uint32_t size)
{
   if (size < HEADER_SIZE)
      throw BlockDeserializingException();
   dataCopy_.copyFrom(ptr, HEADER_SIZE);
   BtcUtils::getHash256(dataCopy_.getPtr(), HEADER_SIZE, thisHash_);
   difficultyDbl_ = BtcUtils::convertDiffBitsToDouble(
      BinaryDataRef(dataCopy_.getPtr() + 72, 4));
   isInitialized_ = true;
   blockHeight_ = UINT32_MAX;
}

///////////////////////////////////////////////////////////////////////////////
//
// LedgerEntry
//
///////////////////////////////////////////////////////////////////////////////
LedgerEntry::LedgerEntry(shared_ptr<::Codec_LedgerEntry::LedgerEntry> msg) :
   msgPtr_(msg), ptr_(msg.get())
{}

///////////////////////////////////////////////////////////////////////////////
LedgerEntry::LedgerEntry(BinaryDataRef bdr)
{
   auto msg = make_shared<::Codec_LedgerEntry::LedgerEntry>();
   msg->ParseFromArray(bdr.getPtr(), bdr.getSize());
   ptr_ = msg.get();
   msgPtr_ = msg;
}

///////////////////////////////////////////////////////////////////////////////
LedgerEntry::LedgerEntry(
   shared_ptr<::Codec_LedgerEntry::ManyLedgerEntry> msg, unsigned index) :
   msgPtr_(msg)
{
   ptr_ = &msg->values(index);
}

///////////////////////////////////////////////////////////////////////////////
LedgerEntry::LedgerEntry(
   shared_ptr<::Codec_BDVCommand::BDVCallback> msg, unsigned i, unsigned y) :
   msgPtr_(msg)
{
   auto& notif = msg->notification(i);
   auto& ledgers = notif.ledgers();
   ptr_ = &ledgers.values(y);
}


///////////////////////////////////////////////////////////////////////////////
const string& LedgerEntry::getID() const
{
   if (ptr_ == nullptr)
      throw runtime_error("uninitialized ledger entry");
   return ptr_->id();
}

///////////////////////////////////////////////////////////////////////////////
int64_t LedgerEntry::getValue() const
{
   if (ptr_ == nullptr)
      throw runtime_error("uninitialized ledger entry");
   return ptr_->balance();
}

///////////////////////////////////////////////////////////////////////////////
uint32_t LedgerEntry::getBlockNum() const
{
   if (ptr_ == nullptr)
      throw runtime_error("uninitialized ledger entry");
   return ptr_->txheight();
}

///////////////////////////////////////////////////////////////////////////////
BinaryDataRef LedgerEntry::getTxHash() const
{
   if (ptr_ == nullptr)
      throw runtime_error("uninitialized ledger entry");
   auto& val = ptr_->txhash();
   BinaryDataRef bdr;
   bdr.setRef(val);
   return bdr;
}

///////////////////////////////////////////////////////////////////////////////
uint32_t LedgerEntry::getIndex() const
{
   if (ptr_ == nullptr)
      throw runtime_error("uninitialized ledger entry");
   return ptr_->index();
}

///////////////////////////////////////////////////////////////////////////////
uint32_t LedgerEntry::getTxTime() const
{
   if (ptr_ == nullptr)
      throw runtime_error("uninitialized ledger entry");
   return ptr_->txtime();
}

///////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::isCoinbase() const
{
   if (ptr_ == nullptr)
      throw runtime_error("uninitialized ledger entry");
   return ptr_->iscoinbase();
}

///////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::isSentToSelf() const
{
   if (ptr_ == nullptr)
      throw runtime_error("uninitialized ledger entry");
   return ptr_->issts();
}

///////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::isChangeBack() const
{
   if (ptr_ == nullptr)
      throw runtime_error("uninitialized ledger entry");
   return ptr_->ischangeback();
}

///////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::isOptInRBF() const
{
   if (ptr_ == nullptr)
      throw runtime_error("uninitialized ledger entry");
   return ptr_->optinrbf();
}

///////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::isChainedZC() const
{
   if (ptr_ == nullptr)
      throw runtime_error("uninitialized ledger entry");
   return ptr_->ischainedzc();
}

///////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::isWitness() const
{
   if (ptr_ == nullptr)
      throw runtime_error("uninitialized ledger entry");
   return ptr_->iswitness();
}

///////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::operator==(const LedgerEntry& rhs)
{
   if (getTxHash() != rhs.getTxHash())
      return false;

   if (getIndex() != rhs.getIndex())
      return false;

   return true;
}

///////////////////////////////////////////////////////////////////////////////
//
// RemoteCallback
//
///////////////////////////////////////////////////////////////////////////////
RemoteCallback::~RemoteCallback(void)
{}

///////////////////////////////////////////////////////////////////////////////
bool RemoteCallback::processNotifications(
   shared_ptr<BDVCallback> callback)
{
   for(int i = 0; i<callback->notification_size(); i++)
   {
      auto& notif = callback->notification(i);

      switch (notif.type())
      {
      case NotificationType::continue_polling:
         break;

      case NotificationType::newblock:
      {
         if (!notif.has_height())
            break;

         unsigned int newblock = notif.height();
         if (newblock != 0)
            run(BDMAction::BDMAction_NewBlock, &newblock, newblock);

         break;
      }

      case NotificationType::zc:
      {
         if (!notif.has_ledgers())
            break;

         auto& ledgers = notif.ledgers();

         vector<LedgerEntry> leVec;
         for (int y = 0; y < ledgers.values_size(); y++)
         {
            LedgerEntry le(callback, i, y);
            leVec.push_back(move(le));
         }

         run(BDMAction::BDMAction_ZC, &leVec, 0);

         break;
      }

      case NotificationType::invalidated_zc:
      {

         if (!notif.has_ids())
            break;

         auto& ids = notif.ids();
         set<BinaryData> idSet;

         for (int y = 0; y < ids.value_size(); y++)
         {
            auto& id_str = ids.value(y).data();
            BinaryData id_bd((uint8_t*)id_str.c_str(), id_str.size());
            idSet.emplace(id_bd);
         }

         run(BDMAction::BDMAction_InvalidatedZC, &idSet, 0);

         break;
      }

      case NotificationType::refresh:
      {
         if (!notif.has_refresh())
            break;

         auto& refresh = notif.refresh();
         auto refreshType = (BDV_refresh)refresh.refreshtype();
         
         vector<BinaryData> bdVec;
         for (int y = 0; y < refresh.id_size(); y++)
         {
            auto& str = refresh.id(y);
            BinaryData bd; bd.copyFrom(str);
            bdVec.push_back(move(bd));
         }

         if (refreshType != BDV_filterChanged)
         {
            run(BDMAction::BDMAction_Refresh, (void*)&bdVec, 0);
         }
         else
         {
            vector<BinaryData> bdvec;
            bdvec.push_back(BinaryData("wallet_filter_changed"));
            run(BDMAction::BDMAction_Refresh, (void*)&bdvec, 0);
         }

         break;
      }

      case NotificationType::ready:
      {
         if (!notif.has_height())
            break;

         unsigned int topblock = notif.height();
         run(BDMAction::BDMAction_Ready, nullptr, topblock);

         break;
      }

      case NotificationType::progress:
      {
         if (!notif.has_progress())
            break;

         ProgressData pd(callback, i);
         progress(pd.phase(), pd.wltIDs(), pd.progress(),
            pd.time(), pd.numericProgress());

         break;
      }

      case NotificationType::terminate:
      {
         //shut down command from server
         return false;
      }

      case NotificationType::nodestatus:
      {
         if (!notif.has_nodestatus())
            break;

         ::ClientClasses::NodeStatusStruct nss(callback, i);

         run(BDMAction::BDMAction_NodeStatus, &nss, 0);
         break;
      }

      case NotificationType::error:
      {
         if (!notif.has_error())
            break;

         auto& msg = notif.error();

         BDV_Error_Struct bdvErr;
         bdvErr.errorStr_ = move(msg.error());
         bdvErr.errType_ = (BDV_ErrorType)msg.type();
         bdvErr.extraMsg_ = move(msg.extra());

         run(BDMAction::BDMAction_BDV_Error, &bdvErr, 0);
         break;
      }

      default:
         continue;
      }
   }

   return true;
}

///////////////////////////////////////////////////////////////////////////////
//
// NodeStatusStruct
//
///////////////////////////////////////////////////////////////////////////////
::ClientClasses::NodeStatusStruct::NodeStatusStruct(BinaryDataRef bdr)
{
   auto msg = make_shared<::Codec_NodeStatus::NodeStatus>();
   msg->ParseFromArray(bdr.getPtr(), bdr.getSize());
   ptr_ = msg.get();
   msgPtr_ = msg;
}

///////////////////////////////////////////////////////////////////////////////
::ClientClasses::NodeStatusStruct::NodeStatusStruct(
   shared_ptr<::Codec_NodeStatus::NodeStatus> msg)
{
   msgPtr_ = msg;
   ptr_ = msg.get();
}

///////////////////////////////////////////////////////////////////////////////
::ClientClasses::NodeStatusStruct::NodeStatusStruct(
   shared_ptr<::Codec_BDVCommand::BDVCallback> msg, unsigned i) :
   msgPtr_(msg)
{
   auto& notif = msg->notification(i);
   ptr_ = &notif.nodestatus();
}

///////////////////////////////////////////////////////////////////////////////
NodeStatus ClientClasses::NodeStatusStruct::status() const
{
   return (NodeStatus)ptr_->status();
}

///////////////////////////////////////////////////////////////////////////////
bool ::ClientClasses::NodeStatusStruct::isSegWitEnabled() const
{
   return ptr_->segwitenabled();
}

///////////////////////////////////////////////////////////////////////////////
RpcStatus ClientClasses::NodeStatusStruct::rpcStatus() const
{
   return (RpcStatus)ptr_->rpcstatus();
}

///////////////////////////////////////////////////////////////////////////////
::ClientClasses::NodeChainState 
   ClientClasses::NodeStatusStruct::chainState() const
{
   auto msg = dynamic_pointer_cast<::Codec_NodeStatus::NodeStatus>(msgPtr_);
   return NodeChainState(msg);
}

///////////////////////////////////////////////////////////////////////////////
//
// NodeChainState
//
///////////////////////////////////////////////////////////////////////////////
::ClientClasses::NodeChainState::NodeChainState(
   shared_ptr<::Codec_NodeStatus::NodeStatus> msg) :
   msgPtr_(msg)
{
   ptr_ = &msg->chainstate();
}

///////////////////////////////////////////////////////////////////////////////
/*unsigned ::ClientClasses::NodeChainState::getTopBlock() const
{
   return ptr_->
}*/

///////////////////////////////////////////////////////////////////////////////
//
// ProgressData
//
///////////////////////////////////////////////////////////////////////////////
::ClientClasses::ProgressData::ProgressData(BinaryDataRef bdr)
{
   auto msg = make_shared<::Codec_NodeStatus::ProgressData>();
   ptr_ = msg.get();
   msgPtr_ = msg;
}

///////////////////////////////////////////////////////////////////////////////
::ClientClasses::ProgressData::ProgressData(
   shared_ptr<::Codec_BDVCommand::BDVCallback> msg, unsigned i) :
   msgPtr_(msg)
{
   auto& notif = msg->notification(i);
   ptr_ = &notif.progress();
}

///////////////////////////////////////////////////////////////////////////////
BDMPhase ClientClasses::ProgressData::phase() const
{
   return (BDMPhase)ptr_->phase();
}

///////////////////////////////////////////////////////////////////////////////
double ClientClasses::ProgressData::progress() const
{
   return ptr_->progress();
}

///////////////////////////////////////////////////////////////////////////////
unsigned ClientClasses::ProgressData::time() const
{
   return ptr_->time();
}

///////////////////////////////////////////////////////////////////////////////
unsigned ClientClasses::ProgressData::numericProgress() const
{
   return ptr_->numericprogress();
}

///////////////////////////////////////////////////////////////////////////////
vector<string> ClientClasses::ProgressData::wltIDs() const
{
   vector<string> vec;
   for (int i = 0; i < ptr_->id_size(); i++)
      vec.push_back(ptr_->id(i));

   return vec;
}
