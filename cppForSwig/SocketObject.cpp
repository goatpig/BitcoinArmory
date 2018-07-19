////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "SocketObject.h"
#include <cstring>
#include <stdexcept>

#include "google/protobuf/text_format.h"

///////////////////////////////////////////////////////////////////////////////
//
// SocketPrototype
//
///////////////////////////////////////////////////////////////////////////////
SocketPrototype::~SocketPrototype()
{}

///////////////////////////////////////////////////////////////////////////////
SocketPrototype::SocketPrototype(const string& addr, const string& port,
   bool doInit) :
   addr_(addr), port_(port)
{
   if (doInit)
      init();
}

///////////////////////////////////////////////////////////////////////////////
void SocketPrototype::init()
{
   //resolve address
   struct addrinfo hints;
   struct addrinfo *result;
   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_protocol = IPPROTO_TCP;

#ifdef _WIN32
   //somehow getaddrinfo doesnt handle localhost on Windows
   string addrstr = addr_;
   if(addr_ == "localhost")
      addrstr = "127.0.0.1"; 
#else
   auto& addrstr = addr_;
#endif

   getaddrinfo(addrstr.c_str(), port_.c_str(), &hints, &result);
   for (auto ptr = result; ptr != nullptr; ptr = ptr->ai_next)
   {
      if (ptr->ai_family == AF_INET)
      {
         memcpy(&serv_addr_, ptr->ai_addr, sizeof(sockaddr_in));
         memcpy(&serv_addr_.sa_data, &ptr->ai_addr->sa_data, 14);
         break;
      }

      throw runtime_error("unsupported remote address format");
   }
   freeaddrinfo(result);
}

///////////////////////////////////////////////////////////////////////////////
SOCKET SocketPrototype::openSocket(bool blocking)
{
   SOCKET sockfd = SOCK_MAX;
   try
   {
      sockfd = socket(serv_addr_.sa_family, SOCK_STREAM, 0);
      if (sockfd < 0)
         throw SocketError("failed to create socket");

      auto result = connect(sockfd, &serv_addr_, sizeof(serv_addr_));
      if (result < 0)
      {
         closeSocket(sockfd);
         throw SocketError("failed to connect to server");
      }
   
      setBlocking(sockfd, blocking);
   }
   catch (SocketError &)
   {
      closeSocket(sockfd);
      sockfd = SOCK_MAX;
   }

   return sockfd;
}

///////////////////////////////////////////////////////////////////////////////
void SocketPrototype::closeSocket(SOCKET& sockfd)
{
   if (sockfd == SOCK_MAX)
      return;

#ifdef WIN32
   closesocket(sockfd);
#else
   close(sockfd);
#endif

   sockfd = SOCK_MAX;
}

///////////////////////////////////////////////////////////////////////////////
bool SocketPrototype::testConnection(void)
{
   try
   {
      auto sockfd = openSocket(true);
      if (sockfd == SOCK_MAX)
         return false;

      closeSocket(sockfd);
      return true;
   }
   catch (runtime_error&)
   {
   }

   return false;
}

////////////////////////////////////////////////////////////////////////////////
void SocketPrototype::setBlocking(SOCKET sock, bool setblocking)
{
   if (sock < 0)
      throw SocketError("invalid socket");

#ifdef WIN32
   unsigned long mode = (unsigned long)!setblocking;
   if (ioctlsocket(sock, FIONBIO, &mode) != 0)
      throw SocketError("failed to set blocking mode on socket");
#else
   int flags = fcntl(sock, F_GETFL, 0);
   if (flags < 0) return;
   flags = setblocking ? (flags&~O_NONBLOCK) : (flags | O_NONBLOCK);
   int rt = fcntl(sock, F_SETFL, flags);
   if (rt != 0)
   {
      auto thiserrno = errno;
      cout << "fcntl returned " << rt << endl;
      cout << "error: " << strerror(errno);
      throw SocketError("failed to set blocking mode on socket");
   }
#endif

   blocking_ = setblocking;
}

///////////////////////////////////////////////////////////////////////////////
void SocketPrototype::listen(AcceptCallback callback, SOCKET& sockfd)
{
   try
   {
      sockfd = socket(serv_addr_.sa_family, SOCK_STREAM, 0);
      if (sockfd < 0)
         throw SocketError("failed to create socket");

      if (::bind(sockfd, &serv_addr_, sizeof(serv_addr_)) < 0)
      {
         closeSocket(sockfd);
         throw SocketError("failed to bind socket");
      }

      if (::listen(sockfd, 10) < 0)
      {
         closeSocket(sockfd);
         throw SocketError("failed to listen to socket");
      }
   }
   catch (SocketError &)
   {
      closeSocket(sockfd);
      return;
   }

   stringstream errorss;
   exception_ptr exceptptr = nullptr;

   struct pollfd pfd;
   pfd.fd = sockfd;
   pfd.events = POLLIN;

   try
   {
      while (1)
      {
#ifdef _WIN32
         auto status = WSAPoll(&pfd, 1, 60000);
#else
         auto status = poll(&pfd, 1, 60000);
#endif

         if (status == 0)
            continue;

         if (status == -1)
         {
            //select error, process and exit loop
#ifdef _WIN32
            auto errornum = WSAGetLastError();
#else
            auto errornum = errno;
#endif
            errorss << "poll() error in readFromSocketThread: " << errornum;
            LOGERR << errorss.str();
            throw SocketError(errorss.str());
         }

         if (pfd.revents & POLLNVAL)
         {
            throw SocketError("POLLNVAL in readFromSocketThread");
         }

         //exceptions
         if (pfd.revents & POLLERR)
         {
            //TODO: grab socket error code, pass error to callback

            //break out of poll loop
            errorss << "POLLERR error in readFromSocketThread";
            LOGERR << errorss.str();
            throw SocketError(errorss.str());
         }

         if (pfd.revents & POLLIN)
         {
            //accept socket and trigger callback
            AcceptStruct astruct;
            astruct.sockfd_ = accept(sockfd, &astruct.saddr_, &astruct.addrlen_);
            callback(move(astruct));
         }
      }
   }
   catch (...)
   {
      exceptptr = current_exception();
   }

   //cleanup
   closeSocket(sockfd);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//// PersistentSocket
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
#ifndef _WIN32
void PersistentSocket::socketService_nix()
{
   int readIncrement = 8192;
   stringstream errorss;

   exception_ptr exceptptr = nullptr;

   struct pollfd pfd[2];

   //pipe to poll
   pfd[0].fd = pipes_[0];
   pfd[0].events = POLLIN;

   //socket to poll
   pfd[1].fd = sockfd_;
   pfd[1].events = POLLIN;

   int timeout = 100;

   auto serviceWrite = [&](void)->void
   {
      vector<uint8_t> payload;

      if (writeLeftOver_.size() != 0)
      {
         payload = move(writeLeftOver_);
         writeLeftOver_.clear();
      }
      else
      {
         try
         {
            payload = move(writeQueue_.pop_front());
         }
         catch (IsEmpty&)
         {
            pfd[1].events = POLLIN;
            return;
         }
      }

      auto bytessent = send(sockfd_, 
         (char*)&payload[0] + writeOffset_, 
         payload.size() - writeOffset_, 0);

      if (bytessent == 0)
         LOGERR << "failed to send data: " << payload.size() << ", offset: " << writeOffset_;

      writeOffset_ += bytessent;
      if (writeOffset_ < payload.size())
         writeLeftOver_ = move(payload);
      else
         writeOffset_ = 0;
   };

   while (1)
   {
      auto status = poll(pfd, 2, timeout);

      if (status == 0)
         continue;

      if (status == -1)
      {
         //poll error, process and exit loop
         auto errornum = errno;
         LOGERR << "poll() error in readFromSocketThread: " << errornum;
         break;
      }

      if (pfd[0].revents & POLLIN)
      {
         uint8_t b;
         auto readAmt = read(pipes_[0], (char*)&b, 1);

         if (readAmt == 1)
         {
            if (b == 0)
               pfd[1].events = POLLIN | POLLOUT;
            else if (b == 1)
               break; //exit poll loop signal
         }
      }

      if (pfd[1].revents & POLLNVAL)
      {
         LOGERR << "POLLNVAL in readFromSocketThread";
      }

      //exceptions
      if (pfd[1].revents & POLLERR)
      {
         //break out of poll loop
         LOGERR << "POLLERR error in readFromSocketThread";
         break;
      }

      if (pfd[1].revents & POLLIN)
      {
         //read socket
         vector<uint8_t> readdata;
         readdata.resize(readIncrement);

         size_t totalread = 0;
         int readAmt;

         while ((readAmt =
            recv(sockfd_, (char*)&readdata[0] + totalread, readIncrement, 0))
            != 0)
         {
            if (readAmt < 0)
            {
               auto errornum = errno;
               if (errornum == EAGAIN || errornum == EWOULDBLOCK)
                  break;

               LOGERR << "recv error: " << errornum;
               break;
            }

            totalread += readAmt;
            if (readAmt < readIncrement)
               break;

            readdata.resize(totalread + readIncrement);
         }

         if (totalread > 0)
         {
            readdata.resize(totalread);
            readQueue_.push_back(move(readdata));
         }
      }

      if (pfd[1].revents & POLLOUT)
      {
         serviceWrite();
      }

      //socket was closed
      if (pfd[1].revents & POLLHUP)
         break;
   }

   run_.store(false, memory_order_relaxed);
}
#endif

///////////////////////////////////////////////////////////////////////////////
#ifdef _WIN32
void PersistentSocket::socketService_win()
{
   size_t readIncrement = 8192;
   DWORD timeout = 100000;

   WSAEventSelect(sockfd_, events_[0], FD_READ | FD_WRITE | FD_CLOSE);
   bool writeReady = false;

   auto serviceSocketWrite = [&writeReady, this](void)->void
   {
      if (!writeReady)
         return;

      vector<uint8_t> payload;
      if (writeLeftOver_.size() != 0)
      {
         payload = move(writeLeftOver_);
         writeLeftOver_.clear();
      }
      else
      {
         try
         {
            payload = move(writeQueue_.pop_front());
         }
         catch (IsEmpty&)
         {
            return;
         }
      }

      WSABUF wsaBuffer;
      wsaBuffer.buf = (char*)&payload[0] + writeOffset_;
      wsaBuffer.len = payload.size() - writeOffset_;

      DWORD bytessent;
      if (WSASend(sockfd_, &wsaBuffer, 1, &bytessent, 0, nullptr, nullptr) ==
         SOCKET_ERROR)
      {
         auto wsaError = WSAGetLastError();
         if (wsaError == WSAEWOULDBLOCK)
         {
            writeReady = false;
         }
         else
         {
            LOGERR << "WSASend error with code: " << wsaError;
            writeOffset_ = 0;
            writeLeftOver_.clear();
            return;
         }
      }
      else
      {
         if (bytessent == 0)
            LOGWARN << "failed to write to socket, aborting";
      }

      writeOffset_ += bytessent;
      if (writeOffset_ < payload.size())
         writeLeftOver_ = move(payload);
      else
         writeOffset_ = 0;
   };

   while (run_.load(memory_order_relaxed))
   {
      serviceSocketWrite();
      auto ev = WSAWaitForMultipleEvents(1, events_, false, timeout, false);
      if (ev == WSA_WAIT_TIMEOUT)
         continue;

      if (ev == WSA_WAIT_FAILED)
      {
         LOGERR << "WSAWaitForMultipleEvents failed";
         break;
      }


      if (ev == WSA_WAIT_EVENT_0)
      {
         //reset user event
         WSAResetEvent(events_[0]);
      }

      WSANETWORKEVENTS networkevents;
      if (WSAEnumNetworkEvents(sockfd_, 0,
         &networkevents) == SOCKET_ERROR)
      {
         LOGERR << "error getting network events for socket";
         break;
      }
       
      //service socket
      if (networkevents.lNetworkEvents & FD_READ)
      {
         //read socket
         vector<uint8_t> readdata;
         readdata.resize(readIncrement);

         size_t totalread = 0;
         int readAmt;

         while ((readAmt =
            recv(sockfd_, (char*)&readdata[0] + totalread, readIncrement, 0))
            != 0)
         {
            if (readAmt < 0)
            {
               auto errornum = errno;
               if (errornum == EAGAIN || errornum == EWOULDBLOCK)
                  break;
   
               LOGERR << "error reading socket, aborting";
               return;
            }

            totalread += readAmt;
            if (readAmt < readIncrement)
               break;

            readdata.resize(totalread + readIncrement);
         }

         if (totalread > 0)
         {
            readdata.resize(totalread);
            readQueue_.push_back(move(readdata));
         }
      }

      if (networkevents.lNetworkEvents & FD_WRITE)
      {
         writeReady = true;
      }

      if (networkevents.lNetworkEvents & FD_CLOSE)
      {
         LOGERR << "socket was closed";
         break;
      }
   }

   run_.store(false, memory_order_relaxed);
}
#endif

///////////////////////////////////////////////////////////////////////////////
void PersistentSocket::queuePayloadForWrite(vector<uint8_t>& payload)
{
   if (payload.size() == 0)
      return;

   //push to write queue
   writeQueue_.push_back(move(payload));

   //signal poll service with 0 to trigger POLLOUT event
   signalService(0);
}

///////////////////////////////////////////////////////////////////////////////
void PersistentSocket::readService()
{
   while (1)
   {
      vector<uint8_t> packet;
      try
      {
         packet = move(readQueue_.pop_front());
      }
      catch(StopBlockingLoop&)
      {
         //exit condition
         break;
      }

      vector<uint8_t> payload;
      if (!processPacket(packet, payload))
         continue;

      respond(payload);
   }
}

///////////////////////////////////////////////////////////////////////////////
bool PersistentSocket::processPacket(
   vector<uint8_t>& packet, vector<uint8_t>& payload)
{
   payload = move(packet);
   return true;
}

///////////////////////////////////////////////////////////////////////////////
void PersistentSocket::signalService(uint8_t signal)
{

   //0 to trigger a pollout, 1 to exit poll loop
#ifdef _WIN32
   if (signal == 1)
      run_.store(false, memory_order_relaxed);

   WSASetEvent(events_[0]);
#else
   if (pipes_[1] == SOCK_MAX)
      return;
   write(pipes_[1], &signal, 1);
#endif
}

///////////////////////////////////////////////////////////////////////////////
void PersistentSocket::init()
{
#ifndef _WIN32
   pipes_[0] = pipes_[1] = SOCK_MAX;
#else
   events_[0] = events_[1] = nullptr;
#endif
}

///////////////////////////////////////////////////////////////////////////////
void PersistentSocket::initPipes()
{
   cleanUpPipes();

#ifdef _WIN32
   for (unsigned i = 0; i < 1; i++)
      events_[i] = WSACreateEvent();
#else
   pipe(pipes_);
#endif
}

///////////////////////////////////////////////////////////////////////////////
void PersistentSocket::cleanUpPipes()
{
   for (unsigned i = 0; i < 2; i++)
   {
#ifdef _WIN32
      if (events_[i] != nullptr)
      {
         WSACloseEvent(events_[i]);
         events_[i] = nullptr;
      }
#else
      if (pipes_[i] != SOCK_MAX)
         close(pipes_[i]);
      pipes_[i] = SOCK_MAX;
#endif
   }
}

///////////////////////////////////////////////////////////////////////////////
bool PersistentSocket::openSocket(bool blocking)
{
   if (addr_.size() != 0 && port_.size() != 0)
      sockfd_ = SocketPrototype::openSocket(blocking);

   return isValid();
}

///////////////////////////////////////////////////////////////////////////////
int PersistentSocket::getSocketName(struct sockaddr& sa)
{
#ifdef _WIN32
   int namelen = sizeof(sa);
#else
   unsigned int namelen = sizeof(sa);
#endif

   return getsockname(sockfd_, &sa, &namelen);
}

///////////////////////////////////////////////////////////////////////////////
int PersistentSocket::getPeerName(struct sockaddr& sa)
{
#ifdef _WIN32
   int namelen = sizeof(sa);
#else
   unsigned int namelen = sizeof(sa);
#endif

   return getpeername(sockfd_, &sa, &namelen);
}

///////////////////////////////////////////////////////////////////////////////
bool PersistentSocket::connectToRemote()
{
   if (run_.load(memory_order_relaxed))
      return true;

   if (sockfd_ == SOCK_MAX)
   {
      if (!openSocket(false))
         return false;
   }

   run_.store(true, memory_order_relaxed);
   initPipes();

   auto readLBD = [this](void)->void
   {
      this->readService();
   };

   auto socketLBD = [this](void)->void
   {
      try
      {
         this->socketService();
      }
      catch (SocketError&)
      {
         LOGERR << "error in socket service, shutting down connection";
         shutdown();
      }
   };

   threads_.push_back(thread(readLBD));
   threads_.push_back(thread(socketLBD));

   return true;
}

///////////////////////////////////////////////////////////////////////////////
void PersistentSocket::shutdown()
{
   readQueue_.terminate();
   signalService(1);

   for (auto& thr : threads_)
      if (thr.joinable())
         thr.join();

   cleanUpPipes();
   closeSocket(sockfd_);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//// SimpleSocket
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void SimpleSocket::pushPayload(
   unique_ptr<Socket_WritePayload> write_payload,
   shared_ptr<Socket_ReadPayload> read_payload)
{
   if (write_payload == nullptr)
      return;

   vector<uint8_t> data;
   write_payload->serialize(data);
   writeToSocket(data);

   if (read_payload == nullptr)
      return;

   auto&& result = readFromSocket();
   BinaryDataRef bdr(&result[0], result.size());
   read_payload->callbackReturn_->callback(bdr);
}

///////////////////////////////////////////////////////////////////////////////
void SimpleSocket::listen(AcceptCallback acb)
{
   SocketPrototype::listen(move(acb), sockfd_);
}

///////////////////////////////////////////////////////////////////////////////
void SimpleSocket::shutdown()
{
   closeSocket(sockfd_);
}

///////////////////////////////////////////////////////////////////////////////
vector<uint8_t> SimpleSocket::readFromSocket(void)
{
   //exit after one read
   size_t readIncrement = 8192;

   struct pollfd pfd;
   pfd.fd = sockfd_;
   pfd.events = POLLIN;

   int timeout = 100;

   while (1)
   {
#ifdef _WIN32
      auto status = WSAPoll(&pfd, 1, timeout);
#else
      auto status = poll(&pfd, 1, timeout);
#endif

      if (status == 0)
         continue;

      if (status == -1)
      {
         //poll error, process and exit loop
#ifdef _WIN32
         auto errornum = WSAGetLastError();
#else
         auto errornum = errno;
#endif
         LOGERR << "poll() error in readFromSocketThread: " << errornum;
         break;
      }

      if (pfd.revents & POLLNVAL)
      {
         LOGERR << "POLLNVAL in readFromSocketThread";
         break;
      }

      //exceptions
      if (pfd.revents & POLLERR)
      {
         //break out of poll loop
         LOGERR << "POLLERR error in readFromSocketThread";
         break;
      }

      if (pfd.revents & POLLIN)
      {
         //read socket
         vector<uint8_t> readdata;
         readdata.resize(readIncrement);

         size_t totalread = 0;
         int readAmt;

         while ((readAmt =
            recv(sockfd_, (char*)&readdata[0] + totalread, readIncrement, 0))
            != 0)
         {
            if (readAmt < 0)
            {
#ifdef _WIN32
               auto errornum = WSAGetLastError();
               if (errornum == WSAEWOULDBLOCK)
                  break;
#else
               auto errornum = errno;
               if (errornum == EAGAIN || errornum == EWOULDBLOCK)
                  break;
#endif

               LOGERR << "recv error: " << errornum;
               break;
            }

            totalread += readAmt;
            if (readAmt < readIncrement)
               break;

            readdata.resize(totalread + readIncrement);
         }

         if (readAmt == 0)
         {
            LOGINFO << "POLLIN recv return 0";
            break;
         }

         if (totalread > 0)
         {
            readdata.resize(totalread);
            return readdata;
         }
      }

      //socket was closed
      if (pfd.revents & POLLHUP)
         break;
   }

   return vector<uint8_t>();
}

///////////////////////////////////////////////////////////////////////////////
int SimpleSocket::writeToSocket(vector<uint8_t>& payload)
{
   return send(sockfd_, (char*)&payload[0], payload.size(), 0);
}

///////////////////////////////////////////////////////////////////////////////
bool SimpleSocket::connectToRemote()
{
   if (sockfd_ == SOCK_MAX)
      sockfd_ = openSocket(false);
   return sockfd_ != SOCK_MAX;
}

///////////////////////////////////////////////////////////////////////////////
bool SimpleSocket::checkSocket(const string& ip, const string& port)
{
   SimpleSocket testSock(ip, port);
   return testSock.testConnection();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//// ListenServer
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
ListenServer::ListenServer(const string& addr, const string& port)
{
   listenSocket_ = make_unique<SimpleSocket>(addr, port);
   listenSocket_->verbose_ = false;
}

///////////////////////////////////////////////////////////////////////////////
void ListenServer::start(ReadCallback callback)
{
   auto listenlbd = [this](ReadCallback clbk)->void
   {
      this->listenThread(clbk);
   };

   listenThread_ = thread(listenlbd, callback);
}

///////////////////////////////////////////////////////////////////////////////
void ListenServer::join()
{
   if (listenThread_.joinable())
      listenThread_.join();
}

///////////////////////////////////////////////////////////////////////////////
void ListenServer::listenThread(ReadCallback callback)
{
   auto acceptlbd = [callback, this](AcceptStruct astruct)->void
   {
      astruct.readCallback_ = callback;
      this->acceptProcess(move(astruct));
   };

   listenSocket_->listen(acceptlbd);
}

///////////////////////////////////////////////////////////////////////////////
void ListenServer::acceptProcess(AcceptStruct aStruct)
{
   unique_lock<mutex> lock(mu_);

   auto readldb = [this](
      shared_ptr<SimpleSocket> sock, ReadCallback callback)->void
   {
      sock->connectToRemote();

      vector<uint8_t> result;
      exception_ptr eptr;
      try
      {
         result = move(sock->readFromSocket());
      }
      catch (exception&)
      {
         eptr = current_exception();
      }

      callback(result, eptr);

      auto sockfd = sock->getSockFD();
      this->cleanUpStack_.push_back(move(sockfd));
   };

   auto ss = make_unique<SocketStruct>();

   //create BinarySocket object from sockfd
   ss->sock_ = make_shared<SimpleSocket>(aStruct.sockfd_);
   ss->sock_->verbose_ = false;

   //start read lambda thread
   ss->thr_ = thread(readldb, ss->sock_, aStruct.readCallback_);

   //record thread id and socket ptr in socketStruct, add to acceptMap_
   acceptMap_.insert(make_pair(aStruct.sockfd_, move(ss)));

   //run through clean up stack, removing flagged entries from acceptMap_
   try
   {
      while (1)
      {
         auto&& sock = cleanUpStack_.pop_front();
         auto iter = acceptMap_.find(sock);
         if (iter != acceptMap_.end())
         {
            auto ssptr = move(iter->second);
            acceptMap_.erase(iter);

            if (ssptr->thr_.joinable())
               ssptr->thr_.join();
         }
      }
   }
   catch (IsEmpty&)
   { 
   }
}

///////////////////////////////////////////////////////////////////////////////
void ListenServer::stop()
{
   listenSocket_->shutdown();
   if (listenThread_.joinable())
      listenThread_.join();

   for (auto& sockPair : acceptMap_)
   {
      auto& sockstruct = sockPair.second;
      
      sockstruct->sock_->shutdown();
      if (sockstruct->thr_.joinable())
         sockstruct->thr_.join();
   }
}

///////////////////////////////////////////////////////////////////////////////
//
// CallbackReturn
//
///////////////////////////////////////////////////////////////////////////////
CallbackReturn::~CallbackReturn()
{}

///////////////////////////////////////////////////////////////////////////////
//
// Socket_WritePayload
//
///////////////////////////////////////////////////////////////////////////////
Socket_WritePayload::~Socket_WritePayload(void)
{}

///////////////////////////////////////////////////////////////////////////////
void WritePayload_Raw::serialize(vector<uint8_t>& data)
{
   data = move(data_);
}

///////////////////////////////////////////////////////////////////////////////
string WritePayload_Protobuf::serializeToText(void)
{
   if (message_ == nullptr)
      return string();

   string str;
   ::google::protobuf::TextFormat::PrintToString(*message_.get(), &str);
   return str;
}

///////////////////////////////////////////////////////////////////////////////
void WritePayload_Protobuf::serialize(vector<uint8_t>& data)
{
   if (message_ == nullptr)
      return;

   data.resize(message_->ByteSize());
   message_->SerializeToArray(&data[0], data.size());
}
