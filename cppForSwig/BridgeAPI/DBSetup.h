////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <functional>
#include <memory>
#include <filesystem>

namespace AsyncClient
{
   class BlockDataViewer;
}

class BinaryData;
class RemoteCallback;

namespace Armory
{
   namespace NetworkPeers
   {
      class ClientStore;
      class PeerKey;
   }

   namespace Bridge
   {
      enum class AutomationStep : int
      {
         SpawnNode,
         SpawnDb,
         ConnectToDb,
         ShutdownDb,
         ShutdownNode,
         Done,
         Cleanup
      };

   #ifdef _WIN32
      using instance_t = void*;
   #else
      using instance_t = int;
   #endif

      class ProcessInstance
      {
      private:
         instance_t instance_;

      public:
         ProcessInstance(void);
         ProcessInstance(instance_t);

         bool isValid(void) const;
         bool isRunning(void);
         void wait(void) const;
      };

      class AutomationContext
      {
         using CallbackFunc = std::function<void(AutomationStep)>;

      private:
         const std::filesystem::path satoshiDir_;
         const std::filesystem::path satoshiBin_;
         const std::filesystem::path dbDir_;
         const bool automateNode_;
         const bool automateDb_;

         std::shared_ptr<NetworkPeers::ClientStore> peers_;
         uint32_t dbPort_ = UINT32_MAX;
         bool hasRun_ = false;

         std::string rpcLogin_;
         std::string rpcPass_;

         ProcessInstance nodeInstance_;
         ProcessInstance dbInstance_;

      private:
         void automateDb(void);
         void automateSatoshi(void);
         void cleanupDb(void);
         void cleanupSatoshi(void);

      public:
         AutomationContext(
            const std::filesystem::path&,
            const std::filesystem::path&,
            const std::filesystem::path&,
            bool, bool
         );

         bool run(const CallbackFunc&);
         void cleanup(const CallbackFunc&);

         bool isDbRunning(void);
         bool isSatoshiRunning(void);
         uint32_t getDbPort(void) const;
         std::shared_ptr<NetworkPeers::ClientStore> getPeerStore(void) const;
      };

      /* db helpers */
      using BdvPtr = std::shared_ptr<AsyncClient::BlockDataViewer>;

      BdvPtr setupClientConnection(
         std::shared_ptr<NetworkPeers::ClientStore>,
         const std::string&, const std::string&, bool,
         const std::function<bool(const BinaryData&)>&,
         std::shared_ptr<RemoteCallback>
      );
      BdvPtr setupClientConnection(
         std::shared_ptr<NetworkPeers::ClientStore>,
         const NetworkPeers::PeerKey&,
         std::shared_ptr<RemoteCallback>
      );
   } //namespace Bridge
} //namespace Armory

namespace Node
{
   namespace Core
   {
      struct DatadirState
      {
         const std::filesystem::path datadir;
         const size_t fileSize;
         const bool isPruned;
      };

      struct BinaryState
      {
         const std::filesystem::path path;
         const std::string version;
      };

      std::filesystem::path findDatadir(void);
      std::filesystem::path findBinary(void);

      DatadirState validateDatadir(const std::filesystem::path&);
      BinaryState validateBinary(const std::filesystem::path&);
   }
}
