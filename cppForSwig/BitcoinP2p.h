////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_BITCOINP2P
#define _H_BITCOINP2P

#include <string>
#include <mutex>
#include <future>
#include <set>
#include <stdint.h>

/***TODO: replace the use of BinaryData with self written class***/
#include "ThreadSafeClasses.h"
#include "BinaryData.h"
#include "EncryptionUtils.h"

#include "TxClasses.h"
#include "SocketObject.h"

//reconnect constants
#define RECONNECT_INCREMENT_MS 500

//message header
#define MESSAGE_HEADER_LEN    24
#define MAGIC_WORD_OFFSET     0
#define MESSAGE_TYPE_OFFSET   4
#define MESSAGE_TYPE_LEN      12
#define PAYLOAD_LENGTH_OFFSET 16
#define CHECKSUM_OFFSET       20

//netaddr
#define NETADDR_WITHTIME   30
#define NETADDR_NOTIME     26

#define VERSION_MINLENGTH  85
#define USERAGENT_OFFSET   80

//inv
#define INV_MAX 50000
#define INV_ENTRY_LEN 36

// Node witness
#define NODE_WITNESS 1 << 3
extern bool PEER_USES_WITNESS;

enum PayloadType
{
   Payload_tx = 1,
   Payload_version,
   Payload_verack,
   Payload_ping,
   Payload_pong,
   Payload_inv,
   Payload_getdata,
   Payload_reject,
   Payload_unknown
};

enum InvType
{
   Inv_Error = 0,
   Inv_Msg_Tx,
   Inv_Msg_Block,
   Inv_Msg_Filtered_Block,
   Inv_Witness = 1 << 30,
   Inv_Msg_Witness_Tx = Inv_Msg_Tx | Inv_Witness,
   Inv_Msg_Witness_Block = Inv_Msg_Block | Inv_Witness
};

int get_varint(uint64_t& val, uint8_t* ptr, uint32_t size);
int make_varint(const uint64_t& value, std::vector<uint8_t>& varint);


////////////////////////////////////////////////////////////////////////////////
struct BitcoinNetAddr
{
   uint64_t services_;
   char ipV6_[16]; //16 bytes long
   uint16_t port_;

   void deserialize(BinaryRefReader);
   void serialize(uint8_t* ptr) const;

   void setIPv4(uint64_t services, const sockaddr& nodeaddr)
   {
      services_ = services;
      memset(ipV6_, 0, 16);
      ipV6_[10] = (char)255;
      ipV6_[11] = (char)255;

      memcpy(ipV6_ + 12, nodeaddr.sa_data + 2, 4);
      auto ptr = (uint8_t*)nodeaddr.sa_data;
      port_ = (unsigned)ptr[0] * 256 + (unsigned)ptr[1];
   }
};

////////////////////////////////////////////////////////////////////////////////
class BitcoinP2P_Exception
{
private:
   const std::string error_;

public:
   BitcoinP2P_Exception(const std::string& e) : error_(e)
   {}

   const std::string& what(void) const { return error_; }
};

struct BitcoinMessageDeserError : public BitcoinP2P_Exception
{
   const size_t offset_;

   BitcoinMessageDeserError(const std::string& e, size_t off) :
      BitcoinP2P_Exception(e), offset_(off)
   {}
};

struct BitcoinMessageUnknown : public BitcoinP2P_Exception
{
   BitcoinMessageUnknown(const std::string& e) : BitcoinP2P_Exception(e)
   {}
};

struct PayloadDeserError : public BitcoinP2P_Exception
{
   PayloadDeserError(const std::string& e) : BitcoinP2P_Exception(e)
   {}
};

struct GetDataException : public BitcoinP2P_Exception
{
   GetDataException(const std::string& e) : BitcoinP2P_Exception(e)
   {}
};

////////////////////////////////////////////////////////////////////////////////
class Payload
{
protected:
   virtual size_t serialize_inner(uint8_t*) const = 0;
   static std::vector<size_t> processPacket(
      std::vector<uint8_t>& data, uint32_t magic_word);

public:
   struct DeserializedPayloads
   {
      std::vector<uint8_t> data_;
      std::vector<std::unique_ptr<Payload>> payloads_;
      size_t spillOffset_ = SIZE_MAX;
      int iterCount_ = 0;
   };

public:
   static std::shared_ptr<DeserializedPayloads> deserialize(
      std::vector<uint8_t>& data, uint32_t magic_word,
      std::shared_ptr<DeserializedPayloads> prevPacket);

public:
   virtual ~Payload() 
   {}

   virtual std::vector<uint8_t> serialize(uint32_t magic_word) const;
   size_t serialize(uint32_t magic_word, void* ptr, size_t buffer_len) const;
   size_t getSerializedSize(void) const;

   virtual PayloadType type(void) const = 0;
   virtual std::string typeStr(void) const = 0;

   virtual void deserialize(uint8_t* dataptr, size_t len) = 0;
};

////
struct Payload_Unknown : public Payload
{
private:
   std::vector<uint8_t> data_;

private:
   size_t serialize_inner(uint8_t*) const;

public:
   Payload_Unknown(void)
   {}

   Payload_Unknown(uint8_t* dataptr, size_t len)
   {
      deserialize(dataptr, len);
   }

   void deserialize(uint8_t* dataptr, size_t len);

   PayloadType type(void) const { return Payload_unknown; }
   std::string typeStr(void) const { return "unknown"; }
};

////
struct Payload_Version : public Payload
{
private:
   size_t serialize_inner(uint8_t*) const;

public:
   struct version_header
   {
      uint32_t version_;
      uint64_t services_;
      int64_t timestamp_;
      BitcoinNetAddr addr_recv_;
      BitcoinNetAddr addr_from_;
      uint64_t nonce_;
   };

   version_header vheader_;
   std::string userAgent_;
   uint32_t startHeight_;

   Payload_Version() {}
   Payload_Version(uint8_t* dataptr, size_t len)
   {
      deserialize(dataptr, len);
   }

   void deserialize(uint8_t* dataptr, size_t len);

   PayloadType type(void) const { return Payload_version; }
   std::string typeStr(void) const { return "version"; }

   void setVersionHeaderIPv4(uint32_t version, uint64_t services,
      int64_t timestamp,
      const sockaddr& recvaddr,
      const sockaddr& fromAddr);
};

////
struct Payload_Verack : public Payload
{
private:
   size_t serialize_inner(uint8_t*) const { return 0; }

public:
   Payload_Verack() {}
   Payload_Verack(std::vector<uint8_t>*)
   {}

   PayloadType type(void) const { return Payload_verack; }
   std::string typeStr(void) const { return "verack"; }

   void deserialize(uint8_t*, size_t) {}
};

////
struct Payload_Ping : public Payload
{
private:
   size_t serialize_inner(uint8_t*) const;

public:
   uint64_t nonce_= UINT64_MAX;

public:
   Payload_Ping() {}

   Payload_Ping(uint8_t* dataptr, size_t len)
   {
      deserialize(dataptr, len);
   }

   void deserialize(uint8_t* dataptr, size_t len);

   PayloadType type(void) const { return Payload_ping; }
   std::string typeStr(void) const { return "ping"; }
};

////
struct Payload_Pong : public Payload
{
private:
   size_t serialize_inner(uint8_t*) const;

public:
   uint64_t nonce_ = UINT64_MAX;

public:
   Payload_Pong() {}

   Payload_Pong(uint8_t* dataptr, size_t len)
   {
      deserialize(dataptr, len);
   }

   void deserialize(uint8_t* dataptr, size_t len);

   PayloadType type(void) const { return Payload_pong; }
   std::string typeStr(void) const { return "pong"; }
};

////
struct InvEntry
{
   InvType invtype_ = Inv_Error;
   uint8_t hash[32];
};

struct Payload_Inv : public Payload
{
private:
   size_t serialize_inner(uint8_t*) const;

public:
   std::vector<InvEntry> invVector_;

public:
   Payload_Inv() {}

   Payload_Inv(uint8_t* dataptr, size_t len)
   {
      deserialize(dataptr, len);
   }

   void deserialize(uint8_t* dataptr, size_t len);

   PayloadType type(void) const { return Payload_inv; }
   std::string typeStr(void) const { return "inv"; }

   void setInvVector(std::vector<InvEntry> invvec)
   {
      invVector_ = move(invvec);
   }
};

////
struct Payload_GetData : public Payload
{
private:
   std::vector<InvEntry> invVector_;

private:
   size_t serialize_inner(uint8_t*) const;

public:
   Payload_GetData() {}

   Payload_GetData(uint8_t* dataptr, size_t len)
   {
      deserialize(dataptr, len);
   }

   Payload_GetData(const InvEntry& inventry)
   {
      invVector_.push_back(inventry);
   }

   Payload_GetData(std::vector<InvEntry>&& invVec)
   {
      invVector_ = invVec;
   }

   void deserialize(uint8_t* dataptr, size_t len);

   PayloadType type(void) const { return Payload_getdata; }
   std::string typeStr(void) const { return "getdata"; }

   const std::vector<InvEntry>& getInvVector(void) const
   {
      return invVector_;
   }
};

////
struct Payload_Tx : public Payload
{
private:
   std::vector<uint8_t> rawTx_;
   BinaryData txHash_;

private:
   size_t serialize_inner(uint8_t*) const;

public:
   Payload_Tx() {}

   Payload_Tx(uint8_t* dataptr, size_t len)
   {
      deserialize(dataptr, len);
   }

   void deserialize(uint8_t* dataptr, size_t len);

   PayloadType type(void) const { return Payload_tx; }
   std::string typeStr(void) const { return "tx"; }

   const BinaryData& getHash256()
   {
      if (txHash_.getSize() == 0)
      {
         Tx thisTx(&rawTx_[0], rawTx_.size());

         txHash_ = std::move(thisTx.getThisHash());
      }

      return txHash_;
   }

   const std::vector<uint8_t>& getRawTx(void) const
   {
      return rawTx_;
   }

   void moveFrom(Payload_Tx& ptx)
   {
      rawTx_ = std::move(ptx.rawTx_);
   }

   void setRawTx(std::vector<uint8_t> rawtx)
   {
      rawTx_ = std::move(rawtx);
   }

   size_t getSize(void) const { return rawTx_.size(); }
};

////reject
class NodeUnitTest;
struct Payload_Reject : public Payload
{
   friend class NodeUnitTest;
private:
   PayloadType rejectType_;
   int8_t code_;
   std::string reasonStr_;
   std::vector<uint8_t> extra_;

private:
   size_t serialize_inner(uint8_t*) const 
   { throw std::runtime_error("not implemened"); }

public:

   Payload_Reject(void)
   {}

   Payload_Reject(uint8_t* dataptr, size_t len)
   {
      deserialize(dataptr, len);
   }

   void deserialize(uint8_t* dataptr, size_t len);

   PayloadType rejectType(void) const { return rejectType_; }
   const std::vector<uint8_t>& getExtra(void) const { return extra_; }
   const std::string& getReasonStr(void) const { return reasonStr_; }

   int8_t code (void) const { return code_; }

   PayloadType type(void) const { return Payload_reject; }
   std::string typeStr(void) const { return "reject"; }
};

////////////////////////////////////////////////////////////////////////////////
class GetDataStatus
{
private:
   bool received_ = true;
   std::string msg_;

   std::shared_ptr<std::promise<std::shared_ptr<Payload>>> prom_;
   std::shared_future<std::shared_ptr<Payload>> fut_;


public:
   GetDataStatus(const GetDataStatus&) = delete;
   
   GetDataStatus(void)
   {
      prom_ = std::make_shared<std::promise<std::shared_ptr<Payload>>>();
      fut_ = prom_->get_future();
   }

   std::shared_future<std::shared_ptr<Payload>> getFuture(void) const
   {
      return fut_;
   }

   std::shared_ptr<std::promise<std::shared_ptr<Payload>>> getPromise(void) const
   {
      return prom_;
   }

   void setMessage(const std::string& message)
   {
      msg_ = message;
   }

   std::string getMessage(void) const { return msg_; }

   bool status(void) const { return received_; }
   void setStatus(bool st) { received_ = st; }
};

////////////////////////////////////////////////////////////////////////////////
class BitcoinP2PSocket : public PersistentSocket
{
private:
   std::shared_ptr<Armory::Threading::BlockingQueue<
      std::vector<uint8_t>>> readDataStack_;

public:
   BitcoinP2PSocket(const std::string& addr, const std::string& port,
      std::shared_ptr<Armory::Threading::BlockingQueue<
      std::vector<uint8_t>>> readStack) :
      PersistentSocket(addr, port), readDataStack_(readStack)
   {}

   SocketType type(void) const override { return SocketBitcoinP2P; }

   void pushPayload(
      std::unique_ptr<Socket_WritePayload>,
      std::shared_ptr<Socket_ReadPayload>) override;
   void respond(std::vector<uint8_t>&) override;
};

////////////////////////////////////////////////////////////////////////////////
class BitcoinNodeInterface
{
protected:
   const uint32_t magic_word_;
   std::atomic<bool> run_;
      
   //new block notification queue
   std::shared_ptr<Armory::Threading::BlockingQueue<
      std::vector<InvEntry>>> invBlockStack_;

   //callback lambdas
   std::function<void(std::vector<InvEntry>&)> invTxLambda_;
   std::function<void(std::unique_ptr<Payload>)> getTxDataLambda_;
   std::function<void(void)> nodeStatusLambda_;

protected:
   void processGetTx(std::unique_ptr<Payload>);

public:
   struct getDataPayload
   {
      std::unique_ptr<Payload> payload_;
   };
   
   Armory::Threading::TransactionalMap<
      BinaryData, std::shared_ptr<getDataPayload>> getDataPayloadMap_;

public:
   BitcoinNodeInterface(uint32_t magic_word, bool watcher);

   //virtuals
   virtual ~BitcoinNodeInterface(void) = 0;

   virtual void connectToNode(bool async) = 0;
   virtual void shutdown(void);

   virtual bool connected(void) const = 0;
   virtual void sendMessage(std::unique_ptr<Payload>) = 0;

   //locals
   bool isSegWit(void) const { return PEER_USES_WITNESS; }
   uint32_t getMagicWord(void) const { return magic_word_; }
   std::shared_ptr<Armory::Threading::BlockingQueue<std::vector<InvEntry>>> 
      getInvBlockStack(void) const { return invBlockStack_; }

   void processInvTx(std::vector<InvEntry>);   
   void processInvBlock(std::vector<InvEntry>);
  
   void registerInvTxLambda(std::function<void(std::vector<InvEntry>)> func);
   void registerNodeStatusLambda(std::function<void(void)> lbd);
   void registerGetTxCallback(
      const std::function<void(std::unique_ptr<Payload>)>&);

   void requestTx(std::vector<InvEntry>);
};

////////////////////////////////////////////////////////////////////////////////
class BitcoinP2P : public BitcoinNodeInterface
{
private:
   const std::string addr_;
   const std::string port_;

   struct sockaddr node_addr_;
   std::unique_ptr<BitcoinP2PSocket> socket_;

   std::mutex connectMutex_, pollMutex_, writeMutex_;
   std::unique_ptr<std::promise<bool>> connectedPromise_ = nullptr;
   std::unique_ptr<std::promise<bool>> verackPromise_ = nullptr;
   std::atomic<bool> nodeConnected_;

   //to pass payloads between the poll thread and the processing one
   std::shared_ptr<Armory::Threading::BlockingQueue<
      std::vector<uint8_t>>> dataStack_;

   std::exception_ptr select_except_ = nullptr;
   std::exception_ptr process_except_ = nullptr;

   std::future<bool> shutdownFuture_;

   uint32_t topBlock_ = UINT32_MAX;

public:
   static const std::map<std::string, PayloadType> strToPayload_;

private:
   void init(void);
   void connectLoop(void);

   void processDataStackThread(void);
   void processPayload(std::vector<std::unique_ptr<Payload>>);

   void checkServices(std::unique_ptr<Payload>);
   
   void gotVerack(void);
   void returnVerack(void);

   void replyPong(std::unique_ptr<Payload>);

   void processInv(std::unique_ptr<Payload>);
   void processGetData(std::unique_ptr<Payload>);
   void processReject(std::unique_ptr<Payload>);

   int64_t getTimeStamp() const;

   void callback(void)
   {
      if (nodeStatusLambda_)
         nodeStatusLambda_();
   }

   void sendMessage(std::vector<std::unique_ptr<Payload>>);

public:
   BitcoinP2P(
      const std::string& addr, const std::string& port, 
      uint32_t magic_word, bool watcher);
   ~BitcoinP2P();

   //virtuals
   void connectToNode(bool async) override;
   void shutdown(void) override;

   void sendMessage(std::unique_ptr<Payload>) override;
   bool connected(void) const override;
   void updateNodeStatus(bool connected);
};

#endif
