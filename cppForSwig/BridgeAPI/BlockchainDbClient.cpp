////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cstdlib>
#include <errno.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <random>
#include <cstring>

#ifndef _WIN32
   #include <sys/wait.h>
   #include "spawn.h"
#endif

#include "BlockchainDbClient.h"
#include <Utils/ArmoryConfig.h>
#include <Utils/FileUtils.h>
#include <Utils/Cryptography.h>

#include <Wallets/IOHeader.h>
#include <Wallets/AuthorizedPeers.h>
#include <AsyncClient.h>

using namespace Armory;
using namespace Armory::Bridge;

using namespace std::string_view_literals;
using namespace std::chrono_literals;

namespace {
#ifdef _WIN32
   size_t getFileSize(HANDLE fHandle)
   {
      auto size = GetFileSize(fHandle, NULL);
      if (size == INVALID_FILE_SIZE) {
         throw std::runtime_error("failed to grab file size");
      }
      return size_t(size);
   }

#else
   size_t getFileSize(int fd)
   {
      struct stat buf;
      if (fstat(fd, &buf) != 0) {
         throw std::runtime_error(
            "fstat failed with error: " + std::string{strerror(errno)});
      }
      return buf.st_size;
   }
#endif

   std::pair<std::string, std::string> getIpAndPortFromPeerName(
      const std::string& peerName)
   {
      //TODO: flesh this out
      std::stringstream ss(peerName);
      std::pair<std::string, std::string> output;

      //ip
      std::getline(ss, output.first, ':');

      //port
      if (ss.good()) {
         std::getline(ss, output.second);
      } else {
         output.second = Config::NetworkSettings::dbPort();
      }
      return output;
   }
}

////////////////////////////////////////////////////////////////////////////////
#ifdef _WIN32
void* Armory::Bridge::autoDbHandle = INVALID_HANDLE_VALUE;

std::pair<std::shared_ptr<Wallets::AuthorizedPeers>, uint32_t>
Bridge::spawnDb(const std::filesystem::path& satoshiPath,
   const std::filesystem::path& dbDir)
{
   //sanity check
   if (autoDbHandle != INVALID_HANDLE_VALUE) {
      throw std::runtime_error("already have an instance of ArmoryDB");
   }

   const std::filesystem::path armoryDbPath{
      Config::Pathing::runningDir() / L"ArmoryDB.exe" };
   if (!FileUtils::pathExists(armoryDbPath, 0)) {
      throw std::runtime_error("invalid db binary path: " + armoryDbPath.string());
   }

   //1. setup ephemeral authPeers
   auto peers = std::make_shared<Wallets::AuthorizedPeers>();

   //generate random db port & set it
   uint32_t port = (rand() % 10000) + 50000;
   std::wstring dbPortStr{ L"--armorydb-port=" + std::to_wstring(port) };

   //db paths
   std::wstring dbDirWStr{ L"--dbdir=" + dbDir.wstring() };
   std::wstring dataDir{ L"--datadir=" + Config::getDataDir().wstring() };

   //btc network
   std::wstring network;
   switch (Config::BitcoinSettings::getMode())
   {
      case Config::NETWORK_MODE_TESTNET:
         network = std::wstring{L"--testnet"};
         break;

      case Config::NETWORK_MODE_REGTEST:
         network = std::wstring{L"--regtest"};
         break;

      default:
         network = std::wstring{L"--mainnet"};
   }

   //core settings
   std::wstring satoshiDir{
      L"--satoshi-datadir=" + satoshiPath.wstring() };
   std::wstring satoshiPort{
      L"--satoshi-port=" + Config::NetworkSettings::btcPortW() };

   std::wstring rpcPort{
      L"--satoshirpc-port=" + Config::NetworkSettings::rpcPortW() };

   //2. randomize a file name
   std::filesystem::path keyFilePath{ Config::getDataDir() /
      std::string{ "keyFile_" +
         Cryptography::PRNG::fortuna.generateRandom(7).toHexStr() }};

   //1. use CreateFile to generate a inheritable file handle
   SECURITY_DESCRIPTOR secDep;
   if (!InitializeSecurityDescriptor(&secDep, SECURITY_DESCRIPTOR_REVISION)) {
      throw std::runtime_error("failed to init keyFile security descriptor");
   }
   SECURITY_ATTRIBUTES secAtt;
   secAtt.nLength = sizeof(SECURITY_ATTRIBUTES);
   secAtt.lpSecurityDescriptor = &secDep;
   secAtt.bInheritHandle = true;

   auto fileHandle = CreateFileW(keyFilePath.c_str(),
      //child process (ArmoryDB) will inherit the handle for writing
      GENERIC_READ | GENERIC_WRITE,

      //no other process should be allowed to open the file while own it
      0,

      //handle should be inheritable
      &secAtt,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      NULL
   );
   if (fileHandle == INVALID_HANDLE_VALUE) {
      auto lastError = GetLastError();
      throw std::runtime_error("failed to create key file with error: " + lastError);
   }

   //2. use CreateProcess to spawn ArmoryDB, and have it inherit the file handle
   std::wstring commandLine{ armoryDbPath.wstring() + L" " +
      L"--ephemeral " + dbPortStr + L" " + dataDir + L" " + dbDirWStr + L" " +
      network + L" " + satoshiDir + L" " + satoshiPort + L" " + rpcPort
   };

   //mandatory, process handle is writting in pi after start
   STARTUPINFOW si;
   PROCESS_INFORMATION pi;
   ZeroMemory( &si, sizeof(si) );
   si.cb = sizeof(si);
   ZeroMemory( &pi, sizeof(pi) );

   /*
   On Windows we add envvars to the parent instead of creating custom ones.
   The child will inherit them.

   It seems that there is a set of undocumented envvars to provide for a
   binary to even run on Windows.
   */
   const auto& pubkey = peers->getOwnPublicKey();
   BinaryDataRef keyRef{pubkey.pubkey, 33};
   SetEnvironmentVariable("CALLER_PUBKEY", keyRef.toHexStr().c_str());
   SetEnvironmentVariable("KEYFILE_HANDLE", std::to_string((uint64_t)fileHandle).c_str());

   if (!CreateProcessW(NULL,
      commandLine.data(),
      NULL,
      NULL,
      true, //inherit parent handles where possible
      NORMAL_PRIORITY_CLASS,
      NULL, //no explicit envvars on windows, let child inherit parent's
      NULL,
      &si, &pi
   )) {
      auto lastError = GetLastError();
      throw std::runtime_error("failed to spawn ArmorDB with error: " + std::to_string(lastError));
   }
   autoDbHandle = pi.hProcess;
   CloseHandle(pi.hThread);

   //5. wait for db to set pubkey in shared file
   unsigned count = 0;
   while (true) {
      if (count >= 100) {
         throw std::runtime_error("autodb handshake timeout");
      }

      if (getFileSize(fileHandle) != 33) {
         //key file hasnt changed, keep polling
         std::this_thread::sleep_for(100ms);
         ++count;
         continue;
      }

      //grab db pubkey from shared file
      SecureBinaryData serverPubkey(33);
      DWORD sizeRead = 0;
      SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN);
      if (!ReadFile(fileHandle, serverPubkey.getPtr(), 33, &sizeRead, NULL) || sizeRead != 33) {
         throw std::runtime_error("failed to read pubkey from key file");
      }

      //add db key to custom store
      std::string addr{"127.0.0.1:" + std::to_string(port)};
      peers->addPeer(serverPubkey, {addr}, {}, false);
      break;
   }

   //close & delete the file
   if (!CloseHandle(fileHandle)) {
      throw std::runtime_error("failed to close key file handle");
   }

   if (!std::filesystem::remove(keyFilePath)) {
      throw std::runtime_error("key file did not exists!");
      //fs::remove returns false if there was nothing to remove.
      //it will throw on failure.
   }

   //return ephemeral key store
   return { peers, port };
}
#else
int Armory::Bridge::autoDbPid = -1;

std::pair<std::shared_ptr<Wallets::AuthorizedPeers>, uint32_t>
Bridge::spawnDb(const std::filesystem::path& satoshiPath,
   const std::filesystem::path& dbDir)
{
   /*
   Use posix_spawn, which wraps around fork() & execve() to spawn an instance
   of ArmoryDB, with tailored CLI args, environment and ephemeral AEAD 2-way
   handshake.

   Keys are prepared following these steps:
      1. CppBridge creates an ephemeral key store and adds its public key to
         the tailored envvar served to ArmoryDB via execve/posix_spawn.
      2. CppBridge creates a random key file in the datadir and locks it.
         The open file descriptor is sent to ArmoryDB via envvars as well.
      3. ArmoryDB detects automation via the --ephemeral CLI arg.
         It creates an ephemeral key store, reads the caller pubkey from
         envvars, adds it to the store and sets it as the store's master key.
      4. ArmoryDB grabs the key file fd from envvars and writes its pubkey
         there. This should work by virtue of opened file descriptors sharing
         rules for fork() evexve(), which posix_spawn() inherits.
      5. CppBridge detects changes to the key file, grabs the pubkey and
         injects it into its own store. The lock is released, the file closed
         and removed.

   Note:
      On Windows, we implement the same design but use WinAPI specific calls,
      namely CreateProcess to spawn ArmoryDB and CreateFile to acquire a file
      handle that is inheritable by the child, instead of a file descriptor.
   */

   //sanity check
   if (autoDbPid != -1) {
      throw std::runtime_error("already have an instance of ArmoryDB");
   }

   //get full path to armorydb
   const std::filesystem::path armoryDbPath{
      Config::Pathing::runningDir() / "ArmoryDB" };
   if (!FileUtils::pathExists(armoryDbPath, 0)) {
      throw std::runtime_error("invalid db binary path: " + armoryDbPath.string());
   }

   //1. setup ephemeral authPeers
   auto peers = std::make_shared<Wallets::AuthorizedPeers>();
   const auto& pubkey = peers->getOwnPublicKey();
   BinaryDataRef keyRef{pubkey.pubkey, 33};
   std::string keyStr{ "CALLER_PUBKEY=" + keyRef.toHexStr() };

   //generate random db port & set it
   uint32_t port = (rand() % 10000) + 50000;
   auto portStr = std::to_string(port);
   std::string dbPortStr{ "--armorydb-port=" + portStr };
   Armory::Config::NetworkSettings::setDbPort(portStr);

   //db paths
   std::string dbDirStr{ "--dbdir=" + dbDir.string() };
   std::string dataDir{ "--datadir=" + Config::getDataDir().string() };

   //btc network
   std::string network;
   switch (Config::BitcoinSettings::getMode())
   {
      case Config::NETWORK_MODE_TESTNET:
         network = std::string{"--testnet"};
         break;

      case Config::NETWORK_MODE_REGTEST:
         network = std::string{"--regtest"};
         break;

      default:
         network = std::string{"--mainnet"};
   }

   //core settings
   std::string satoshiDir{
      "--satoshi-datadir=" + satoshiPath.string() };
   std::string satoshiPort{
      "--satoshi-port=" + Config::NetworkSettings::btcPort() };
   std::string rpcPort{
      "--satoshirpc-port=" + Config::NetworkSettings::rpcPort() };

   //setup argv
   auto dbPathStr = armoryDbPath.string();
   char* argv[] = {
      //first arg has to be binary's path
      dbPathStr.data(),
      //ephemeral mode, custom port to listen to
      (char*)"--ephemeral"sv.data(), dbPortStr.data(),
      //datadir, dbdir
      dataDir.data(), dbDirStr.data(),
      //network & core settings
      network.data(), satoshiDir.data(), satoshiPort.data(), rpcPort.data(),
      (char*)nullptr
   };

   //2. randomize a file name
   std::filesystem::path keyFilePath{ Config::getDataDir() /
      std::string{ "keyFile_" +
         Cryptography::PRNG::fortuna.generateRandom(7).toHexStr()
      }};

   //open file and lock it
   auto fd = open(keyFilePath.c_str(), O_CREAT | O_EXCL | O_RSYNC | O_RDWR, 0600);
   if (fd == -1) {
      throw std::runtime_error("failed to create autodb key file");
   }
   if (flock(fd, LOCK_EX) != 0) {
      throw std::runtime_error(
         "failed to lock key file with error: " + std::string{strerror(errno)});
   }
   if (getFileSize(fd) != 0) {
      throw std::runtime_error("autodb key file isnt fresh");
   }
   std::string keyFileFd{ "KEYFILE_FD=" + std::to_string(fd) };

   //setup envp
   char* envp[] = {
      keyStr.data(), keyFileFd.data(),
      (char*)nullptr
   };

   //spawn the db
   int result = posix_spawn(&autoDbPid, argv[0], nullptr, nullptr, argv, envp);
   if (result != 0 || autoDbPid == -1) {
      throw std::runtime_error(
         "failed to spawn armorydb with error: " + std::string{strerror(errno)});
   }

   //5. wait for db to set pubkey in shared file
   unsigned count = 0;
   while (true) {
      if (count >= 100) {
         throw std::runtime_error("autodb handshake timeout");
      }

      if (getFileSize(fd) != 33) {
         //key file hasnt changed, keep polling
         std::this_thread::sleep_for(100ms);
         ++count;
         continue;
      }

      //grab db pubkey from shared file
      SecureBinaryData serverPubkey(33);
      lseek(fd, 0, SEEK_SET);
      if (read(fd, serverPubkey.getPtr(), 33) != 33) {
         throw std::runtime_error("failed to read pubkey from key file");
      }

      //add db key to custom store
      std::string addr{"127.0.0.1:" + portStr};
      peers->addPeer(serverPubkey, {addr}, {}, false);
      break;
   }

   //clean up the file
   if (flock(fd, LOCK_UN) != 0) {
      throw std::runtime_error("failed to unlock key file");
   }
   if (close(fd) != 0) {
      throw std::runtime_error("failed to close key file");
   }

   if (!std::filesystem::remove(keyFilePath)) {
      throw std::runtime_error("key file did not exists!");
      //fs::remove returns false if there was nothing to remove.
      //it will throw on failure.
   }

   //return ephemeral key store
   return { peers, port };
}
#endif

////
bool Armory::Bridge::isDbRunning()
{
#ifdef _WIN32
   if (autoDbHandle == INVALID_HANDLE_VALUE) {
      return false;
   }

   if (WaitForSingleObject(autoDbHandle, 0) != WAIT_TIMEOUT) {
      //we need to close this handle after use
      CloseHandle(autoDbHandle);
      autoDbHandle = INVALID_HANDLE_VALUE;
      return false;
   }
#else
   if (autoDbPid == -1) {
      return false;
   }

   siginfo_t processInfo;
   memset(&processInfo, 0, sizeof(processInfo));
   if (waitid(P_PID, (pid_t)autoDbPid, &processInfo, WEXITED | WNOHANG) != 0) {
      return false;
   }

   if (processInfo.si_pid == 0) {
      return true;
   }
   if (processInfo.si_code == CLD_EXITED || processInfo.si_code == CLD_KILLED) {
      autoDbPid = -1;
      return false;
   }
#endif

   return true;
}

////////////////////////////////////////////////////////////////////////////////
BdvPtr Armory::Bridge::setupClientConnection(
   std::shared_ptr<Wallets::AuthorizedPeers> peers,
   const std::string& ip, const std::string& port, bool oneWayAuth,
   const std::function<bool(const BinaryData&)>& presentPubKeyFunc,
   std::shared_ptr<RemoteCallback> cbPtr)
{
   //sanity check
   if (peers == nullptr) {
      throw std::runtime_error("null peers db");
   }

   //setup bdv obj
   BdvPtr bdvPtr = AsyncClient::BlockDataViewer::getNewBDV(
      ip, port,
      peers, oneWayAuth,
      cbPtr
   );

   if (presentPubKeyFunc) {
      bdvPtr->setCheckServerKeyPromptLambda(presentPubKeyFunc);
   }

   //connect to db
   if (!bdvPtr->connectToRemote()) {
      return nullptr;
   }
   bdvPtr->registerWithDB(
      Config::BitcoinSettings::getMagicBytes().toHexStr());

   //notify setup is done
   return bdvPtr;
}

////////////////////////////////////////////////////////////////////////////////
BdvPtr Armory::Bridge::setupClientConnection(
   std::shared_ptr<Wallets::AuthorizedPeers> peers,
   const Wallets::PeerKey& peerObj,
   std::shared_ptr<RemoteCallback> cbPtr)
{
   auto peerNames = peers->getPeerNameMap(peerObj.isOneWay());
   for (const auto& peerName : peerNames) {
      if (std::memcmp(peerObj.getKey().getPtr(),
         peerName.second.pubkey,
         BIP151PUBKEYSIZE) != 0) {
         continue;
      }

      auto ipAndPort = getIpAndPortFromPeerName(peerName.first);
      auto bdvPtr = setupClientConnection(peers,
         ipAndPort.first, ipAndPort.second,
         peerObj.isOneWay(), {},
         cbPtr
      );
      if (bdvPtr != nullptr) {
         return bdvPtr;
      }
   }
   return nullptr;
}
