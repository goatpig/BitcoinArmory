////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig.                                         //
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

#include "Utils/ThreadSafeClasses.h"
#include "Utils/BinaryData.h"
#ifndef NO_SOCKET_INCLUDES
#include "SocketIncludes.h"
#endif

typedef std::function<bool(std::vector<uint8_t>, std::exception_ptr)> ReadCallback;
enum class SocketType : int;

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn
{
   virtual ~CallbackReturn(void) = 0;
   virtual void callback(BinaryDataRef) = 0;
};

////
struct CallbackReturn_CloseBitcoinP2PSocket : public CallbackReturn
{
private:
   std::shared_ptr<
      Armory::Threading::BlockingQueue<std::vector<uint8_t>>> dataStack_;

public:
   CallbackReturn_CloseBitcoinP2PSocket(
      std::shared_ptr<Armory::Threading::BlockingQueue<std::vector<uint8_t>>>);

   void callback(const BinaryDataRef&);
};

///////////////////////////////////////////////////////////////////////////////
struct Socket_ReadPayload
{
public:
   uint16_t id_ = UINT16_MAX;
   std::unique_ptr<CallbackReturn> callbackReturn_ = nullptr;

public:
   Socket_ReadPayload(void);
   Socket_ReadPayload(unsigned);
};

///////////////////////////////////////////////////////////////////////////////
struct Socket_WritePayload
{
public:
   unsigned id_;

public:
   virtual ~Socket_WritePayload(void) = 0;
   virtual void serialize(std::vector<uint8_t>&) = 0;
   virtual std::string serializeToText(void) = 0;
   virtual size_t getSerializedSize(void) const = 0;
   virtual bool isSingleSegment(void) const;
};

///////////////////////////////////////////////////////////////////////////////
#ifndef NO_SOCKET_INCLUDES
struct AcceptStruct
{
public:
   SOCKET sockfd_;
   sockaddr saddr_;
   socklen_t addrlen_;
   ReadCallback readCallback_;

public:
   AcceptStruct(void);
};
#endif

///////////////////////////////////////////////////////////////////////////////
class SocketPrototype
{
   friend class ListenServer;

private: 
   bool blocking_ = true;

protected:

public:
   typedef std::function<bool(const std::vector<uint8_t>&)>  SequentialReadCallback;
#ifndef NO_SOCKET_INCLUDES
   typedef std::function<void(AcceptStruct)> AcceptCallback;
#endif

protected:
   const size_t maxread_ = 4*1024*1024;
   
#ifndef NO_SOCKET_INCLUDES
   struct sockaddr serv_addr_;
#endif
   const std::string addr_;
   const std::string port_;

   bool verbose_ = true;

private:
   void init(void);

protected:
   SocketPrototype(void);

#ifndef NO_SOCKET_INCLUDES
   void setBlocking(SOCKET, bool);
   void listen(AcceptCallback, SOCKET& sockfd);
#endif
public:
   SocketPrototype(const std::string& addr, const std::string& port, bool init = true);
   virtual ~SocketPrototype(void) = 0;

   virtual bool testConnection(void);
   bool isBlocking(void) const;
#ifndef NO_SOCKET_INCLUDES
   SOCKET openSocket(bool blocking);
   static void closeSocket(SOCKET&);
#endif
   virtual void pushPayload(
      std::unique_ptr<Socket_WritePayload>,
      std::shared_ptr<Socket_ReadPayload>) = 0;
   virtual bool connectToRemote(void) = 0;

   virtual SocketType type(void) const = 0;
   const std::string& getAddrStr(void) const;
   const std::string& getPortStr(void) const;

   //override me
   virtual bool running(void) const;
};

///////////////////////////////////////////////////////////////////////////////
class SimpleSocket : public SocketPrototype
{
#ifndef NO_SOCKET_INCLUDES
protected:
   SOCKET sockfd_ = SOCK_MAX;
#endif
private:
   int writeToSocket(std::vector<uint8_t>&);

public:
   SimpleSocket(const std::string& addr, const std::string& port);
#ifndef NO_SOCKET_INCLUDES
   SimpleSocket(SOCKET);
#endif
   ~SimpleSocket(void);

   SocketType type(void) const override;
#ifndef NO_SOCKET_INCLUDES
   SOCKET getSockFD(void) const;
#endif
   void pushPayload(
      std::unique_ptr<Socket_WritePayload>,
      std::shared_ptr<Socket_ReadPayload>) override;
   std::vector<uint8_t> readFromSocket(void);
   void shutdown(void);
#ifndef NO_SOCKET_INCLUDES
   void listen(AcceptCallback);
#endif
   bool connectToRemote(void) override;

   //
   static bool checkSocket(const std::string& ip, const std::string& port);
};

///////////////////////////////////////////////////////////////////////////////
class PersistentSocket : public SocketPrototype
{
   friend class ListenServer;

private:
#ifndef NO_SOCKET_INCLUDES
   SOCKET sockfd_ = SOCK_MAX;
#endif
   std::vector<std::thread> threads_;

   std::vector<uint8_t> writeLeftOver_;
   size_t writeOffset_ = 0;

   std::atomic<bool> run_;
   std::shared_future<bool> shutdownFut_;
   std::unique_ptr<std::promise<bool>> shutdownProm_;
   std::mutex shutdownMutex_;

#ifndef NO_SOCKET_INCLUDES
#ifdef _WIN32
   WSAEVENT events_[2];
#else
   SOCKET pipes_[2];
#endif
#endif
   Armory::Threading::BlockingQueue<std::vector<uint8_t>> readQueue_;
   Armory::Threading::Queue<std::vector<uint8_t>> writeQueue_;

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
   virtual bool processPacket(std::vector<uint8_t>&, std::vector<uint8_t>&);
   virtual void respond(std::vector<uint8_t>&) = 0;
   void queuePayloadForWrite(std::vector<uint8_t>&);

public:
   PersistentSocket(const std::string& addr, const std::string& port);
#ifndef NO_SOCKET_INCLUDES
   PersistentSocket(SOCKET);
#endif
   ~PersistentSocket(void);

   void shutdown();
   bool openSocket(bool);
   int getSocketName(struct sockaddr& );
   int getPeerName(struct sockaddr&);
   bool connectToRemote(void);
   bool isValid(void) const;
   bool testConnection(void);
   void blockUntilClosed(void) const;
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
      SocketStruct(void);

   public:
      std::shared_ptr<SimpleSocket> sock_;
      std::thread thr_;
   };

private:
   std::unique_ptr<SimpleSocket> listenSocket_;
#ifndef NO_SOCKET_INCLUDES
   std::map<SOCKET, std::unique_ptr<SocketStruct>> acceptMap_;
   Armory::Threading::Queue<SOCKET> cleanUpStack_;
#endif
   std::thread listenThread_;
   std::mutex mu_;

private:
   void listenThread(ReadCallback);
#ifndef NO_SOCKET_INCLUDES
   void acceptProcess(AcceptStruct);
#endif
   ListenServer(const ListenServer&) = delete;

public:
   ListenServer(const std::string& addr, const std::string& port);
   ~ListenServer(void);

   void start(ReadCallback);
   void stop(void);
   void join(void);
};

#endif
