////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <mutex>
#include <future>
#include <set>
#include <stdint.h>

/***TODO: replace the use of BinaryData with self written class***/
#include "Utils/ThreadSafeClasses.h"
#include "Utils/BinaryData.h"

#include "TxClasses.h"
#include <Network/SocketObject.h>

class NodeUnitTest;

namespace Node
{
   class NodeException
   {
   private:
      const std::string error_;

   public:
      NodeException(const std::string&);
      const std::string& what(void) const;
   };

   namespace Core
   {
      namespace P2P
      {
         using MagicWordType = uint32_t;
         enum class PayloadType : int
         {
            Tx = 1,
            Version,
            VerAck,
            Ping,
            Pong,
            Inv,
            GetData,
            Reject,
            Unknown
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

         extern const std::map<std::string_view, PayloadType> typeToPayload;

         ///////////////////////////////////////////////////////////////////////
         struct NetAddr
         {
            uint64_t services;
            char ipV6[16]; //16 bytes long
            uint16_t port;

            void deserialize(BinaryRefReader);
            void serialize(uint8_t* ptr) const;
            void setIPv4(uint64_t, const sockaddr&);

         };

         //////////////////////////////////////////////////////////////////////////
         struct MessageDeserError : public NodeException
         {
            const size_t offset;
            MessageDeserError(const std::string&, size_t);
         };

         struct MessageUnknown : public NodeException
         {
            MessageUnknown(const std::string&);
         };

         struct PayloadDeserError : public NodeException
         {
            PayloadDeserError(const std::string&);
         };

         struct GetDataException : public NodeException
         {
            GetDataException(const std::string&);
         };

         ///////////////////////////////////////////////////////////////////////
         class Payload
         {
         protected:
            virtual size_t serializeInner(uint8_t*) const = 0;
            static std::vector<size_t> processPacket(
               std::vector<uint8_t>&, MagicWordType);

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
               std::vector<uint8_t>&, MagicWordType,
               std::shared_ptr<DeserializedPayloads>);

         public:
            virtual ~Payload(void) = 0;

            virtual std::vector<uint8_t> serialize(MagicWordType) const;
            size_t serialize(MagicWordType, void*, size_t) const;
            size_t getSerializedSize(void) const;

            virtual PayloadType type(void) const = 0;
            virtual std::string_view typeStr(void) const = 0;
            virtual void deserialize(const uint8_t*, size_t) = 0;
         };
         using PayloadPtr = std::shared_ptr<Payload>;

         ////
         class Payload_Unknown : public Payload
         {
         private:
            std::vector<uint8_t> data_;

         private:
            size_t serializeInner(uint8_t*) const override;

         public:
            Payload_Unknown(void);
            Payload_Unknown(const uint8_t*, size_t);

            void deserialize(const uint8_t*, size_t) override;
            PayloadType type(void) const override;
            std::string_view typeStr(void) const override;
         };

         ////
         class Payload_Version : public Payload
         {
         private:
            size_t serializeInner(uint8_t*) const override;

         public:
            struct VersionHeader
            {
               uint32_t version_;
               uint64_t services_;
               int64_t timestamp_;
               NetAddr addr_recv_;
               NetAddr addr_from_;
               uint64_t nonce_;
            };

            VersionHeader vheader_;
            std::string userAgent_;
            uint32_t startHeight_;

            Payload_Version(void);
            Payload_Version(const uint8_t*, size_t);

            void deserialize(const uint8_t*, size_t) override;
            PayloadType type(void) const override;
            std::string_view typeStr(void) const override;

            void setVersionHeaderIPv4(uint32_t, uint64_t, int64_t,
               const sockaddr&, const sockaddr&);
         };

         ////
         class Payload_Verack : public Payload
         {
         private:
            size_t serializeInner(uint8_t*) const override;

         public:
            Payload_Verack(void);
            Payload_Verack(std::vector<uint8_t>*);

            PayloadType type(void) const override;
            std::string_view typeStr(void) const override;
            void deserialize(const uint8_t*, size_t) override;
         };

         ////
         class Payload_Ping : public Payload
         {
         private:
            size_t serializeInner(uint8_t*) const override;

         public:
            uint64_t nonce_ = UINT64_MAX;

         public:
            Payload_Ping(void);
            Payload_Ping(const uint8_t*, size_t);

            PayloadType type(void) const override;
            std::string_view typeStr(void) const override;
            void deserialize(const uint8_t*, size_t) override;
         };

         ////
         class Payload_Pong : public Payload
         {
         private:
            size_t serializeInner(uint8_t*) const override;

         public:
            uint64_t nonce_ = UINT64_MAX;

         public:
            Payload_Pong(void);
            Payload_Pong(const uint8_t*, size_t);

            PayloadType type(void) const override;
            std::string_view typeStr(void) const override;
            void deserialize(const uint8_t*, size_t) override;
         };

         ////
         struct InvEntry
         {
            InvType invtype = Inv_Error;
            uint8_t hash[32];
         };
         using InvVector = std::vector<InvEntry>;

         class Payload_Inv : public Payload
         {
         private:
            size_t serializeInner(uint8_t*) const override;

         public:
            InvVector invVector_;

         public:
            Payload_Inv(void);
            Payload_Inv(const uint8_t*, size_t);

            PayloadType type(void) const override;
            std::string_view typeStr(void) const override;
            void deserialize(const uint8_t*, size_t) override;

            void setInvVector(InvVector);
         };

         ////
         class Payload_GetData : public Payload
         {
         private:
            InvVector invVector_;

         private:
            size_t serializeInner(uint8_t*) const override;

         public:
            Payload_GetData(void);
            Payload_GetData(const uint8_t*, size_t);
            Payload_GetData(const InvEntry&);
            Payload_GetData(InvVector&&);

            PayloadType type(void) const override;
            std::string_view typeStr(void) const override;
            void deserialize(const uint8_t*, size_t) override;

            const InvVector& getInvVector(void) const;
         };

         ////
         struct Payload_Tx : public Payload
         {
         private:
            std::vector<uint8_t> rawTx_;
            mutable BinaryData txHash_;

         private:
            size_t serializeInner(uint8_t*) const override;

         public:
            Payload_Tx(void);
            Payload_Tx(const uint8_t*, size_t);

            PayloadType type(void) const override;
            std::string_view typeStr(void) const override;
            void deserialize(const uint8_t*, size_t) override;

            const BinaryData& getHash256(void) const;
            const std::vector<uint8_t>& getRawTx(void) const;
            void moveFrom(Payload_Tx&);
            void setRawTx(std::vector<uint8_t>);
            size_t getSize(void) const;
            bool empty(void) const;
         };

         ////reject
         class Payload_Reject : public Payload
         {
            friend class ::NodeUnitTest;

         private:
            PayloadType rejectType_;
            int8_t code_;
            std::string reasonStr_;
            std::vector<uint8_t> extra_;

         private:
            size_t serializeInner(uint8_t*) const override;

         public:
            Payload_Reject(void);
            Payload_Reject(const uint8_t*, size_t);

            PayloadType type(void) const override;
            std::string_view typeStr(void) const override;
            void deserialize(const uint8_t*, size_t) override;

            PayloadType rejectType(void) const;
            const std::vector<uint8_t>& getExtra(void) const;
            const std::string& getReasonStr(void) const;
            int8_t code (void) const;
         };

         ///////////////////////////////////////////////////////////////////////
         class GetDataStatus
         {
         private:
            bool received_ = true;
            std::string msg_;

            std::shared_ptr<std::promise<PayloadPtr>> prom_;
            std::shared_future<PayloadPtr> fut_;

         public:
            GetDataStatus(void);
            GetDataStatus(const GetDataStatus&) = delete;

            std::shared_future<PayloadPtr> getFuture(void) const;
            std::shared_ptr<std::promise<PayloadPtr>> getPromise(void) const;

            void setMessage(const std::string&);
            const std::string& getMessage(void) const;
            bool status(void) const;
            void setStatus(bool);
         };

         ///////////////////////////////////////////////////////////////////////
         class Socket : public Armory::Network::PersistentSocket
         {
         private:
            std::shared_ptr<Armory::Threading::BlockingQueue<
               std::vector<uint8_t>>> readDataStack_;

         public:
            Socket(const std::string&, const std::string&,
               std::shared_ptr<Armory::Threading::BlockingQueue<std::vector<uint8_t>>>);

            SocketType type(void) const override;
            void pushPayload(
               std::unique_ptr<Armory::Network::Socket_WritePayload>,
               std::shared_ptr<Armory::Network::Socket_ReadPayload>) override;
            void respond(std::vector<uint8_t>&) override;
         };

         //////////////////////////////////////////////////////////////////////////
         class Iface
         {
         protected:
            const MagicWordType magicWord_;
            bool isSegWit_ = false;
            std::atomic<bool> run_;

            //new block notification queue
            std::shared_ptr<Armory::Threading::BlockingQueue<InvVector>> invBlockStack_;

            //callback lambdas
            std::function<void(InvVector&)> invTxLambda_;
            std::function<void(std::unique_ptr<Payload>)> getTxDataLambda_;
            std::function<void(void)> nodeStatusLambda_;

         protected:
            void processGetTx(std::unique_ptr<Payload>);

         public:
            struct GetDataPayload
            {
               std::unique_ptr<Payload> payload_;
            };
            Armory::Threading::TransactionalMap<
               BinaryData, std::shared_ptr<GetDataPayload>> getDataPayloadMap_;

         public:
            Iface(MagicWordType, bool);
            virtual ~Iface(void) = 0;

            //virtuals
            virtual void connectToNode(bool) = 0;
            virtual void shutdown(void);
            virtual bool connected(void) const = 0;
            virtual void sendMessage(std::unique_ptr<Payload>) = 0;

            //locals
            bool isSegWit(void) const;
            MagicWordType getMagicWord(void) const;
            std::shared_ptr<Armory::Threading::BlockingQueue<InvVector>>
            getInvBlockStack(void) const;
            void requestTx(InvVector);

            //inv processing
            void processInvTx(InvVector);
            void processInvBlock(InvVector);

            //callback registration
            void registerInvTxCallback(
               const std::function<void(InvVector)>&);
            void registerNodeStatusCallback(
               const std::function<void(void)>&);
            void registerGetTxCallback(
               const std::function<void(std::unique_ptr<Payload>)>&);
         };

         ///////////////////////////////////////////////////////////////////////
         class Peer : public Iface
         {
         private:
            const std::string addr_;
            const std::string port_;
            struct sockaddr node_addr_;
            std::unique_ptr<Socket> socket_;

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

            int64_t getTimeStamp(void) const;
            void callback(void) const;
            void sendMessage(std::vector<std::unique_ptr<Payload>>);

         public:
            Peer(const std::string&, const std::string&,
               MagicWordType, bool);
            ~Peer(void);

            //virtuals
            void connectToNode(bool) override;
            void shutdown(void) override;
            void sendMessage(std::unique_ptr<Payload>) override;
            bool connected(void) const override;

            //locals
            void updateNodeStatus(bool);
         };
      } //namespace P2P
   } //namespace Core
} //namespace Node
