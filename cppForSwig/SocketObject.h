////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _SOCKETOBJ_H
#define _SOCKETOBJ_H

#include <sys/types.h>
#include <string>
#include <sstream>
#include <stdint.h>
#include <functional>
#include <memory>

#ifndef _WIN32
#include <poll.h>
#define socketService socketService_nix
#else
#define socketService socketService_win
#endif

#include "ThreadSafeClasses.h"
#include "bdmenums.h"
#include "log.h"
#include "SocketIncludes.h"
#include "BinaryData.h"

#include <google/protobuf/message.h>

typedef function<bool(vector<uint8_t>, exception_ptr)>  ReadCallback;

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn
{
   virtual ~CallbackReturn(void) = 0;
   virtual void callback(BinaryDataRef bdr) = 0;
};

struct CallbackReturn_CloseBitcoinP2PSocket : public CallbackReturn
{
private:
   shared_ptr<BlockingQueue<vector<uint8_t>>> dataStack_;

public:
   CallbackReturn_CloseBitcoinP2PSocket(
      shared_ptr<BlockingQueue<vector<uint8_t>>> datastack) :
      dataStack_(datastack)
   {}

   void callback(const BinaryDataRef& bdr) 
   { dataStack_->terminate(nullptr); }
};

///////////////////////////////////////////////////////////////////////////////
struct Socket_ReadPayload
{
   uint16_t id_ = UINT16_MAX;
   unique_ptr<CallbackReturn> callbackReturn_ = nullptr;

   Socket_ReadPayload(void)
   {}

   Socket_ReadPayload(unsigned id) :
      id_(id)
   {}
};

///////////////////////////////////////////////////////////////////////////////
struct Socket_WritePayload
{
   virtual ~Socket_WritePayload(void) = 0;
   virtual void serialize(vector<uint8_t>&) = 0;
   virtual string serializeToText(void) = 0;
};

////
struct WritePayload_Protobuf : public Socket_WritePayload
{
   unique_ptr<::google::protobuf::Message> message_;

   void serialize(vector<uint8_t>&);
   string serializeToText(void);
};

////
struct WritePayload_Raw : public Socket_WritePayload
{
   vector<uint8_t> data_;

   void serialize(vector<uint8_t>&);
   string serializeToText(void) {
      throw SocketError("raw payload cannot serilaize to str"); }
};

////
struct WritePayload_String : public Socket_WritePayload
{
   string data_;

   void serialize(vector<uint8_t>&) {
      throw SocketError("string payload cannot serilaize to raw binary");
   }
   
   string serializeToText(void) {
      return move(data_);
   }
};

////
struct WritePayload_StringPassthrough : public Socket_WritePayload
{
   string data_;

   void serialize(vector<uint8_t>& payload) {
      payload.reserve(data_.size() +1);
      payload.insert(payload.end(), data_.begin(), data_.end());
      data_.push_back(0);
   }

   string serializeToText(void) {
      return move(data_);
   }
};


///////////////////////////////////////////////////////////////////////////////
struct AcceptStruct
{
   SOCKET sockfd_;
   sockaddr saddr_;
   socklen_t addrlen_;
   ReadCallback readCallback_;

   AcceptStruct(void) :
      addrlen_(sizeof(saddr_))
   {}
};

///////////////////////////////////////////////////////////////////////////////
class SocketPrototype
{
   friend class FCGI_Server;
   friend class ListenServer;

private: 
   bool blocking_ = true;

protected:

public:
   typedef function<bool(const vector<uint8_t>&)>  SequentialReadCallback;
   typedef function<void(AcceptStruct)> AcceptCallback;

protected:
   const size_t maxread_ = 4*1024*1024;
   
   struct sockaddr serv_addr_;
   const string addr_;
   const string port_;

   bool verbose_ = true;

private:
   void init(void);

protected:   
   void setBlocking(SOCKET, bool);
   void listen(AcceptCallback, SOCKET& sockfd);

   SocketPrototype(void) :
      addr_(""), port_("")
   {}
   
public:
   SocketPrototype(const string& addr, const string& port, bool init = true);
   virtual ~SocketPrototype(void) = 0;

   virtual bool testConnection(void);
   bool isBlocking(void) const { return blocking_; }
   SOCKET openSocket(bool blocking);
   
   static void closeSocket(SOCKET&);
   virtual void pushPayload(
      unique_ptr<Socket_WritePayload>,
      shared_ptr<Socket_ReadPayload>) = 0;
   virtual bool connectToRemote(void) = 0;

   virtual SocketType type(void) const = 0;
   const string& getAddrStr(void) const { return addr_; }
   const string& getPortStr(void) const { return port_; }
};

///////////////////////////////////////////////////////////////////////////////
class SimpleSocket : public SocketPrototype
{
protected:
   SOCKET sockfd_ = SOCK_MAX;

private:
   int writeToSocket(vector<uint8_t>&);

public:
   SimpleSocket(const string& addr, const string& port) :
      SocketPrototype(addr, port)
   {}
   
   SimpleSocket(SOCKET sockfd) :
      SocketPrototype(), sockfd_(sockfd)
   {}

   ~SimpleSocket(void)
   {
      closeSocket(sockfd_);
   }

   SocketType type(void) const { return SocketSimple; }

   void pushPayload(
      unique_ptr<Socket_WritePayload>,
      shared_ptr<Socket_ReadPayload>);
   vector<uint8_t> readFromSocket(void);
   void shutdown(void);
   void listen(AcceptCallback);
   bool connectToRemote(void);
   SOCKET getSockFD(void) const { return sockfd_; }

   //
   static bool checkSocket(const string& ip, const string& port);
};

///////////////////////////////////////////////////////////////////////////////
class PersistentSocket : public SocketPrototype
{
   friend class ListenServer;

private:
   SOCKET sockfd_ = SOCK_MAX;
   vector<thread> threads_;
   
   vector<uint8_t> writeLeftOver_;
   size_t writeOffset_ = 0;

   atomic<bool> run_;

#ifdef _WIN32
   WSAEVENT events_[2];
#else
   SOCKET pipes_[2];
#endif

   BlockingQueue<vector<uint8_t>> readQueue_;
   Queue<vector<uint8_t>> writeQueue_;

private:
   void signalService(uint8_t);
#ifdef _WIN32
   void socketService_win(void);
#else
   void socketService_nix(void);
#endif
   void readService(void);
   void initPipes(void);
   void cleanUpPipes(void);
   void init(void);

protected:
   virtual bool processPacket(vector<uint8_t>&, vector<uint8_t>&);
   virtual void respond(vector<uint8_t>&) = 0;
   void queuePayloadForWrite(vector<uint8_t>&);

public:
   PersistentSocket(const string& addr, const string& port) :
      SocketPrototype(addr, port)
   {
      init();
   }

   PersistentSocket(SOCKET sockfd) :
      SocketPrototype(), sockfd_(sockfd)
   {
      init();
   }

   ~PersistentSocket(void)
   {
      shutdown();
   }

   void shutdown();
   bool openSocket(bool blocking);
   int getSocketName(struct sockaddr& sa);
   int getPeerName(struct sockaddr& sa);
   bool connectToRemote(void);
   bool isValid(void) const { return sockfd_ != SOCK_MAX; }
   bool testConnection(void) { return isValid(); }
};

///////////////////////////////////////////////////////////////////////////////
class ListenServer
{
private:
   struct SocketStruct
   {
   private:
      SocketStruct(const SocketStruct&) = delete;

   public:
      SocketStruct(void)
      {}

      shared_ptr<SimpleSocket> sock_;
      thread thr_;
   };

private:
   unique_ptr<SimpleSocket> listenSocket_;
   map<SOCKET, unique_ptr<SocketStruct>> acceptMap_;
   Queue<SOCKET> cleanUpStack_;

   thread listenThread_;
   mutex mu_;

private:
   void listenThread(ReadCallback);
   void acceptProcess(AcceptStruct);
   ListenServer(const ListenServer&) = delete;

public:
   ListenServer(const string& addr, const string& port);
   ~ListenServer(void)
   {
      stop();
   }

   void start(ReadCallback);
   void stop(void);
   void join(void);
};

#endif
