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

///////////////////////////////////////////////////////////////////////////////
//
// BinarySocket
//
///////////////////////////////////////////////////////////////////////////////
BinarySocket::BinarySocket(const string& addr, const string& port) :
   addr_(addr), port_(port)
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
   string addrstr = addr;
   if(addr == "localhost")
      addrstr = "127.0.0.1"; 
#else
   auto& addrstr = addr;
#endif

   getaddrinfo(addrstr.c_str(), port.c_str(), &hints, &result);
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
SOCKET BinarySocket::openSocket(void)
{
   SOCKET sockfd = socket(serv_addr_.sa_family, SOCK_STREAM, 0);
   if (sockfd < 0)
      throw runtime_error("failed to create socket");

   if (connect(sockfd, &serv_addr_, sizeof(serv_addr_)) < 0)
      throw runtime_error("failed to connect to server");

   return sockfd;
}

///////////////////////////////////////////////////////////////////////////////
void BinarySocket::closeSocket(SOCKET sockfd)
{
   if (sockfd == SOCK_MAX)
      return;

#ifdef WIN32
   if (closesocket(sockfd) != 0)
      throw runtime_error("failed to close socket");
#else
   if(close(sockfd) != 0)
      throw runtime_error("failed to close socket");
#endif
}

///////////////////////////////////////////////////////////////////////////////
void BinarySocket::writeToSocket(
   SOCKET sockfd, const char* data, uint32_t size)
{
   if(WRITETOSOCKET(sockfd, data, size) != size)
      throw runtime_error("failed to write to socket");
}

///////////////////////////////////////////////////////////////////////////////
void BinarySocket::readFromSocket(
   SOCKET sockfd, vector<char>& buffer)
{
   size_t increment = 8192;
   auto currentSize = buffer.size();
   buffer.resize(currentSize + increment);
   size_t totalread = currentSize;

   //TODO: break down reads to 8192 byte packets, set max read to 1MB

   try
   {
      while (1)
      {
         auto bytesread = READFROMSOCKET(sockfd, &buffer[totalread], increment);
         totalread += bytesread;

         if (bytesread == 0)
            break;
         if (bytesread < 0)
            throw runtime_error("error while reading socket");

         if (bytesread == increment)
            buffer.resize(totalread + increment);
         else
            break;

         if (totalread > maxread_)
            throw runtime_error("too much data to read from socket");
      }
   }
   catch (runtime_error &e)
   {
      throw e;
   }

   buffer.resize(totalread);
}

///////////////////////////////////////////////////////////////////////////////
string BinarySocket::writeAndRead(const string& msg, SOCKET sockfd)
{
   if (sockfd == SOCK_MAX)
      sockfd = this->openSocket();

   writeToSocket(sockfd, msg.c_str(), msg.size());
   vector<char> retval;
   readFromSocket(sockfd, retval);

   closeSocket(sockfd);
   
   string retmsg;
   typedef vector<char>::iterator IterType;
   retmsg.insert(retmsg.begin(),
      move_iterator<IterType>(retval.begin()),
      move_iterator<IterType>(retval.end()));
   return retmsg;
}

///////////////////////////////////////////////////////////////////////////////
bool BinarySocket::testConnection(void)
{
   try
   {
      auto sockfd = openSocket();
      closeSocket(sockfd);
      return true;
   }
   catch (runtime_error&)
   {
   }

   return false;
}

///////////////////////////////////////////////////////////////////////////////
//
// HttpSocket
//
///////////////////////////////////////////////////////////////////////////////
HttpSocket::HttpSocket(const BinarySocket& obj) :
   BinarySocket(obj)
{
   stringstream ss;
   ss << "POST /index.php HTTP/1.1" << "\r\n";
   ss << "Host: " << addr_ << "\r\n";
   ss << "Content-type: text/html; charset=UTF-8" << "\r\n";
   ss << "Content-Length: ";

   http_header_ = move(ss.str());
}

///////////////////////////////////////////////////////////////////////////////
int32_t HttpSocket::makePacket(char** packet, const char* msg)
{
   if (packet == nullptr)
      return -1;

   stringstream ss;
   ss << strlen(msg);
   ss << "\r\n\r\n";

   *packet = new char[strlen(msg) +
      ss.str().size() +
      http_header_.size() +
      1];

   memcpy(*packet, http_header_.c_str(), http_header_.size());
   size_t pos = http_header_.size();

   memcpy(*packet + pos, ss.str().c_str(), ss.str().size());
   pos += ss.str().size();

   memcpy(*packet + pos, msg, strlen(msg));
   pos += strlen(msg);

   memset(*packet + pos, 0, 1);

   return pos;
}

///////////////////////////////////////////////////////////////////////////////
string HttpSocket::getMessage(vector<char>& msg)
{
   /***
   Always expect text data (null terminated)
   ***/

   //look for double crlf http header end, return everything after that
   typedef vector<char>::iterator vcIter;

   //let's use a move iterator, enough copies already as it is
   string htmlstr(
      move_iterator<vcIter>(msg.begin()), 
      move_iterator<vcIter>(msg.end()));

   size_t pos = htmlstr.find("\r\n\r\n");
   if (pos == string::npos)
   {
      //no html break, check for error marker
      pos = htmlstr.find("error:");
      if (pos != string::npos)
         throw runtime_error(htmlstr);

      throw runtime_error("unexpected return value");
   }

   return htmlstr.substr(pos +4);
}

///////////////////////////////////////////////////////////////////////////////
string HttpSocket::writeAndRead(const string& msg, SOCKET sockfd)
{
   if (sockfd == SOCK_MAX)
      sockfd = openSocket();

   char* packet = nullptr;
   auto packetSize = makePacket(&packet, msg.c_str());
   
   writeToSocket(sockfd, packet, packetSize);

   vector<char> retval;
   typedef vector<char>::iterator vecIterType;

   int content_length = -1;
   size_t header_len = 0;

   auto get_content_len = [&content_length](const string& header_str)
   {
      string err504("HTTP/1.1 504");
      if (header_str.compare(0, err504.size(), err504) == 0)
         throw runtime_error("connection timed out");
      
      string search_tok_caps("Content-Length: ");
      auto tokpos = header_str.find(search_tok_caps);
      if (tokpos != string::npos)
      {
         content_length = atoi(header_str.c_str() + 
                               tokpos + search_tok_caps.size());
         return;
      }

      string search_tok("content-length: ");
      tokpos = header_str.find(search_tok);
      if (tokpos != string::npos)
      {
         content_length = atoi(header_str.c_str() + 
                               tokpos + search_tok.size());
         return;
      }
   };

   while (1)
   {
      //get as much http data as available
      readFromSocket(sockfd, retval);

      if (content_length == -1)
      {
         //if content_length is -1, we have not read the content-lengh in the
         //http header yet, let's do that
         for (unsigned i = 0; i < retval.size(); i++)
         {
            if (retval[i] == '\r')
            {
               if (retval.size() - i < 3)
                  break;

               if (retval[i + 1] == '\n' &&
                  retval[i + 2] == '\r' &&
                  retval[i + 3] == '\n')
               {
                  header_len = i + 4;
                  break;
               }
            }
         }

         if (header_len == 0)
            throw runtime_error("couldn't find http header in response");

         string header_str(&retval[0], header_len);
         get_content_len(header_str);
      }
         
      if (content_length == -1)
         throw runtime_error("failed to find http header response packet");

      //check the total amount of data read matches the advertised
      //data in the http header
      if (retval.size() >= content_length + header_len)
      {
         retval.resize(content_length + header_len);
         break;
      }
   }
   
   auto&& retmsg = getMessage(retval);
   closeSocket(sockfd);
   
   return retmsg;
}

///////////////////////////////////////////////////////////////////////////////
//
// FcgiSocket
//
///////////////////////////////////////////////////////////////////////////////
FcgiSocket::FcgiSocket(const HttpSocket& obj) :
   HttpSocket(obj)
{}

///////////////////////////////////////////////////////////////////////////////
FcgiMessage FcgiSocket::makePacket(const char* msg)
{
   FcgiMessage fcgiMsg;
   auto requestID = fcgiMsg.beginRequest();

   stringstream msglength;
   msglength << strlen(msg);

   //params
   auto& params = fcgiMsg.getNewPacket();
   params.addParam("CONTENT_TYPE", "text/html; charset=UTF-8");
   params.addParam("CONTENT_LENGTH", msglength.str());
   params.buildHeader(FCGI_PARAMS, requestID);

   //terminate fcgi_param
   auto& paramterminator = fcgiMsg.getNewPacket();
   paramterminator.buildHeader(FCGI_PARAMS, requestID);

   //data
   //TODO: break down data into several FCGI_STDIN packets if length > UINT16_MAX
   auto& data = fcgiMsg.getNewPacket();
   data.addData(msg, strlen(msg));
   data.buildHeader(FCGI_STDIN, requestID);

   //terminate fcgi_stdin
   auto& dataterminator = fcgiMsg.getNewPacket();
   dataterminator.buildHeader(FCGI_STDIN, requestID);

   return fcgiMsg;
}

///////////////////////////////////////////////////////////////////////////////
string FcgiSocket::getMessage(const vector<char>& msg, 
   const FcgiMessage& fcgimsg)
{
   //parse FCGI headers, rebuild HTTP packet
   if (msg.size() == 0)
      throw runtime_error("empty fcgi packet");

   vector<char> httpmsg;

   int endpacket = 0;
   size_t ptroffset = 0;
   auto processFcgiPacket = [&](void)->void
   {
      while (ptroffset + FCGI_HEADER_LEN <= msg.size())
      {
         //grab fcgi header
         const char* fcgiheader = &msg[ptroffset];

         ptroffset += FCGI_HEADER_LEN;

         //make sure fcgi version and request id match
         if (fcgiheader[0] != FCGI_VERSION_1)
            throw runtime_error("unexpected fcgi header version");

         uint16_t requestid;
         requestid = (uint8_t)fcgiheader[3] + (uint8_t)fcgiheader[2] * 256;
         if (requestid != fcgimsg.id())
            throw runtime_error("request id mismatch");

         //check packet type
         switch (fcgiheader[1])
         {
         case FCGI_END_REQUEST:
            endpacket++;
            return;

         case FCGI_STDOUT:
            //get packetsize and padding
            uint16_t packetsize, padding;

            packetsize = (uint8_t)fcgiheader[5] + (uint8_t)fcgiheader[4] * 256;
            padding    = (uint8_t)fcgiheader[6] + (uint8_t)fcgiheader[7] * 256;

            if (packetsize > 0)
            {
               //extract http data
               httpmsg.insert(httpmsg.end(),
                  msg.begin() + ptroffset,
                  msg.begin() + ptroffset + packetsize);

               //advance index to next header
               ptroffset += packetsize + padding;
            }
            else
            {
               endpacket++;
            }
            break;

         case FCGI_ABORT_REQUEST:
            throw runtime_error("received FCGI_ABORT_REQUEST packet");

         default:
            //we do not handle the other request types as a client
            throw runtime_error("unknown fcgi header request byte");
         }
      }
   };

   processFcgiPacket();
   auto&& httpbody = HttpSocket::getMessage(httpmsg);

   if (endpacket < 2)
      throw runtime_error("fcgi packet is missing termination");

   return httpbody;
}

///////////////////////////////////////////////////////////////////////////////
string FcgiSocket::writeAndRead(const string& msg, SOCKET sockfd)
{
   if (sockfd == SOCK_MAX)
      sockfd = openSocket();  

   auto&& fcgiMsg = makePacket(msg.c_str());
   auto serdata = fcgiMsg.serialize();
   auto serdatalength = fcgiMsg.getSerializedDataLength();

   writeToSocket(sockfd, (char*)serdata, serdatalength);

   vector<char> retval;
   readFromSocket(sockfd, retval);
   auto&& retmsg = getMessage(retval, fcgiMsg);

   closeSocket(sockfd);
   fcgiMsg.clear();

   return retmsg;
}
