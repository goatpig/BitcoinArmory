////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
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
#include <charconv>

#ifndef _WIN32
   #include <sys/wait.h>
   #include "spawn.h"
#endif

#include "DBSetup.h"
#include <Utils/ArmoryConfig.h>
#include <Utils/BtcUtils.h>
#include <Utils/FileUtils.h>
#include <Utils/Cryptography.h>
#include <Utils/JSON_codec.h>

#include <Wallets/IOHeader.h>
#include <Wallets/AuthorizedPeers.h>
#include <AsyncClient.h>
#include <Node/nodeRPC.h>

using namespace Armory;
using namespace Armory::Bridge;

using namespace std::string_view_literals;
using namespace std::chrono_literals;

namespace {
#ifdef _WIN32
   instance_t INVALID_INSTANCE = INVALID_HANDLE_VALUE;

   std::vector<std::string_view> satoshiDirCandidates{
      "Local/Bitcoin"sv,
      "Roaming/Bitcoin"sv
   };
   std::vector<std::string_view> satoshiBinCandidates{
      //figure out default windows install location
   };

   std::wstring toWString(const std::string& str)
   {
      auto wCharCount = MultiByteToWideChar(
         CP_UTF8, 0,
         str.c_str(), str.size(),
         nullptr, 0
      );
      if (wCharCount == 0) {
         throw std::runtime_error("could not project wchar size");
      }

      std::wstring wString;
      wString.resize(wCharCount);
      auto result = MultiByteToWideChar(
         CP_UTF8, 0,
         str.c_str(), str.size(),
         wString.data(), wCharCount
      );
      if (result != wCharCount) {
         throw std::runtime_error("failed to convert to wstring");
      }
      return wString;
   }

   std::string getLastErrorVerbose()
   {
      auto lastError = GetLastError();
      LPVOID lpMsgBuf;

      if (FormatMessage(
         FORMAT_MESSAGE_ALLOCATE_BUFFER | 
         FORMAT_MESSAGE_FROM_SYSTEM |
         FORMAT_MESSAGE_IGNORE_INSERTS,
         NULL,
         lastError,
         MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
         (LPTSTR) &lpMsgBuf,
         0, NULL) == 0) {
         return std::format("could not retrieve verbose for error code {}", lastError);
      }

      std::string result{(LPCSTR)lpMsgBuf};
      LocalFree(lpMsgBuf);
      return result;
   }

   ////////
   std::pair<ProcessInstance, std::string> spawnProcess(
      const std::filesystem::path& target,
      const std::map<std::string, std::filesystem::path>& pathArgs,
      const std::vector<std::string>& args,
      const std::map<std::string, std::string>& envvars,
      bool captureStdOut)
   {
      /* use CreateProcess to spawn ArmoryDB */

      //binary target
      std::wstring commandLine{ target.wstring() };

      //path arguments
      try {
         for (const auto& pathArg : pathArgs) {
            auto wArg = toWString(pathArg.first);
            commandLine.append(std::format(L" {}={}", wArg, pathArg.second.wstring()));
         }

         //other args
         for (const auto& arg : args) {
            auto wArg = toWString(arg);
            commandLine.append(std::format(L" {}", wArg));
         }
      } catch (const std::exception& e) {
         return { {}, std::format(
            "failed to build arg string with error: {}", e.what()) };
      }

      //mandatory, process handle is written inside pi after start
      STARTUPINFOW si;
      ZeroMemory( &si, sizeof(si) );
      si.cb = sizeof(si);

      PROCESS_INFORMATION pi;
      ZeroMemory( &pi, sizeof(pi) );

      /*
      On Windows we add envvars to the parent instead of creating custom ones.
      The child will inherit them.

      It seems that there is a set of undocumented envvars to provide for a
      binary to even run on Windows.
      */
      for (const auto envvar : envvars) {
         SetEnvironmentVariable(envvar.first.c_str(), envvar.second.c_str());
      }

      HANDLE pipeRead = nullptr;
      HANDLE pipeWrite = nullptr;
      if (captureStdOut) {
         //create pipes to take over stdout in child process
         SECURITY_ATTRIBUTES sa;
         ZeroMemory(&sa, sizeof(sa));
         sa.nLength = sizeof(SECURITY_ATTRIBUTES);
         sa.bInheritHandle = true;
         if (CreatePipe(&pipeRead, &pipeWrite, &sa, 0) == 0) {
            return { {}, "failed to create pipes" };
         }

         //do not let child inherit the read pipe
         SetHandleInformation(pipeRead, HANDLE_FLAG_INHERIT, 0);
         si.hStdOutput = pipeWrite;
         si.hStdError = pipeWrite;
         si.dwFlags |= STARTF_USESTDHANDLES;
      }
      if (CreateProcessW(NULL,
         commandLine.data(),
         NULL,
         NULL,
         true, //inherit parent handles where possible
         NORMAL_PRIORITY_CLASS,
         NULL, //no explicit envvars on windows, let child inherit parent's
         NULL,
         &si, &pi) == 0) {
         return { {}, getLastErrorVerbose() };
      }

      std::string stdOutStr;
      if (captureStdOut) {
         //parent side has no use for the write pipe
         CloseHandle(pipeWrite);

         //grab first output to stdout and cleanup
         stdOutStr.resize(2048);
         DWORD bytesRead;
         if (ReadFile(pipeRead, stdOutStr.data(), 2047, &bytesRead, nullptr) != 0) {
            stdOutStr.resize(bytesRead);
         } else {
            stdOutStr.resize(0);
         }
         CloseHandle(pipeRead);
      }

      auto handle = pi.hProcess;
      CloseHandle(pi.hThread);
      return { {handle}, stdOutStr };
   }

#else
   instance_t INVALID_INSTANCE = -1;

   std::vector<std::string_view> satoshiDirCandidates{
      ".bitcoin"sv
   };
   std::vector<std::string_view> satoshiBinCandidates{
      "/usr/bin"sv,
      "/usr/local/bin"sv,
      "/opt/bin"sv
   };

   std::pair<ProcessInstance, std::string> spawnProcess(
      const std::filesystem::path& target,
      const std::map<std::string, std::filesystem::path>& pathArgs,
      const std::vector<std::string>& args,
      const std::map<std::string, std::string>& envvars,
      bool captureStdOut)
   {
      std::vector<char*> argv;
      auto targetStr = target.string();
      argv.emplace_back(targetStr.data());

      //path args
      std::vector<std::string> pathArgsStr;
      for (const auto& pathArg : pathArgs) {
         pathArgsStr.emplace_back(std::format("{}={}",
            pathArg.first, pathArg.second.string()));
         argv.emplace_back((char*)pathArgsStr.back().data());
      }

      //other args
      for (const auto& arg : args) {
         argv.emplace_back((char*)arg.data());
      }

      //argv null terminator
      argv.emplace_back(nullptr);

      std::vector<std::string> envStrings;
      for (const auto& envvar : envvars) {
         envStrings.emplace_back(
            std::format("{}={}", envvar.first, envvar.second));
      }
      std::vector<char*> envp;
      for (const auto& envstr : envStrings) {
         envp.emplace_back((char*)envstr.data());
      }
      envp.emplace_back(nullptr);

      if (captureStdOut) {
         //create pipe that will replace stdout fd
         int stdout_pipe[2];
         if (pipe(stdout_pipe) != 0) {
            throw std::runtime_error("failed to setup pipes");
         }

         //tell posix_spawn to substitute child's fd 1 (stdout) with our pipe
         posix_spawn_file_actions_t ps_actions;
         posix_spawn_file_actions_init(&ps_actions);
         posix_spawn_file_actions_adddup2(&ps_actions, stdout_pipe[1], 1);

         //spawn the process
         int pid = -1;
         int result = posix_spawn(&pid, argv[0], &ps_actions, nullptr, &argv[0], &envp[0]);
         std::string stdout_str;


         //read stdout via the pipe
         if (result == 0 && pid != -1) {
            struct pollfd pfd{ stdout_pipe[0], POLLIN };
            while (poll(&pfd, 1, 2000) > 0) {
               stdout_str.resize(1024);
               auto bytesread = read(stdout_pipe[0], stdout_str.data(), 1023);
               if (bytesread > 0) {
                  stdout_str.resize(bytesread);
                  break;
               } else {
                  stdout_str.clear();
               }
            }
         }

         //cleanup pipes and posix_spawn data
         close(stdout_pipe[0]);
         close(stdout_pipe[1]);
         posix_spawn_file_actions_destroy(&ps_actions);

         //return pid and stdout output
         return { {pid}, stdout_str };
      } else {
         int pid = -1;
         int result = posix_spawn(&pid, argv[0], nullptr, nullptr, &argv[0], &envp[0]);
         if (result != 0 || pid == -1) {
            return { -1, {} };
         }
         return { {pid}, {} };
      }
   }

#endif

   std::pair<std::string, Network::port_t> getIpAndPortFromPeerName(
      const std::string& peerName)
   {
      //TODO: flesh this out
      std::stringstream ss(peerName);
      std::pair<std::string, Network::port_t> output;

      //ip
      std::getline(ss, output.first, ':');

      //port
      if (ss.good()) {
         std::string portStr;
         std::getline(ss, portStr);
         output.second = std::stoi(portStr);
      } else {
         output.second = Config::NetworkSettings::dbPort();
      }
      return output;
   }

   ////////
   constexpr std::string_view blocksDir = "blocks"sv;
   constexpr std::string_view bitcoind = "bitcoind"sv;
   constexpr std::string_view xorFile = "xor.dat"sv;
   constexpr std::string_view bitcoinConfFile = "bitcoin.conf"sv;
   constexpr std::string_view cookieFile = ".cookie"sv;
   constexpr std::string_view prunedKey = "prune"sv;
   constexpr std::string_view rpcLogKey = "rpcuser"sv;
   constexpr std::string_view rpcPassKey = "rpcpassword"sv;
   constexpr std::string_view coreVersionHeader = "Bitcoin Core daemon version "sv;

   std::filesystem::path getSatoshiDatadir(const std::filesystem::path& path)
   {
      if (path.stem() != blocksDir) {
         return path;
      }
      return path.parent_path();
   }

   std::filesystem::path getBlocksDir(const std::filesystem::path& path)
   {
      if (path.stem() == blocksDir) {
         return path;
      }
      return path / blocksDir;
   }

   std::pair<bool, size_t> validateBlocksDir(const std::filesystem::path& dataDir)
   {
      auto blocksDir = getBlocksDir(dataDir);
      if (!FileUtils::isDir(blocksDir, 2)) {
         return { false, SIZE_MAX };
      }

      //look for first blkXXXXX.dat file
      std::filesystem::path firstBlkFile{};
      size_t totalChainSize = 0;
      for (const auto& entry : std::filesystem::directory_iterator{blocksDir}) {
         if (!entry.is_regular_file()) {
            continue;
         }

         auto blkFilePath = entry.path();
         auto filesize = FileUtils::getFileSize(blkFilePath);
         if (filesize == SIZE_MAX) {
            continue;
         }
         if (filesize >= 8 && firstBlkFile.empty()) {
            firstBlkFile = blkFilePath;
         }
         totalChainSize += filesize;
      }

      if (firstBlkFile.empty()) {
         return { false, SIZE_MAX };
      }

      //look for xor key
      auto xorPath = blocksDir / xorFile;
      uint64_t xorKey = 0;
      if (FileUtils::pathExists(xorPath, 2)) {
         auto fileCopy = FileUtils::FileCopy(xorPath);
         if (fileCopy.size() == 8) {
            std::memcpy(&xorKey, fileCopy.ptr(), 8);
         }
      }

      //check magic byte in first file
      uint64_t firstChunk;
      std::ifstream blkFileStream{firstBlkFile, std::ios::binary | std::ios::in};
      blkFileStream.read(reinterpret_cast<char*>(&firstChunk), sizeof(uint64_t));
      if (xorKey != 0) {
         firstChunk ^= xorKey;
      }

      auto magicBytes = Config::BitcoinSettings::getMagicBytes();
      auto isMatch = std::memcmp(
         reinterpret_cast<char*>(&firstChunk),
         magicBytes.getPtr(),
         magicBytes.getSize()
      ) == 0;
      return { isMatch, isMatch ? totalChainSize : SIZE_MAX };
   }

   std::string getVersionStringFromOutput(const std::string& output)
   {
      if (output.size() < coreVersionHeader.size()) {
         throw std::runtime_error("invalid node executable output size");
      }
      std::string_view header{output.c_str(), coreVersionHeader.size()};
      if (header != coreVersionHeader) {
         LOGWARN << "node output: " << output;
         throw std::runtime_error("node executable output header mismatch");
      }

      //assume next token is version string
      std::string_view body{
         output.c_str() + coreVersionHeader.size(),
         output.size() - coreVersionHeader.size()
      };
      auto end = body.find(' ');
      if (end == std::string_view::npos) {
         throw std::runtime_error("failed to tokenize node output");
      }
      return std::string{ body.substr(0, end) };
   }

   std::pair<bool, std::string> validateSatoshiBinary(
      const std::filesystem::path& binPath,
      const std::filesystem::path& dataDir)
   {
      /*
      Run `bitcoin --version`, grab stdout.
      Parse it for the version string.
      */
      if (!FileUtils::pathExists(binPath, 8)) {
         return { false, std::format(
            "{} is not an executable", binPath.string()) };
      }

      /***
      bitcoind will fail to detect the datadir when ran from fork/execv.
      We need to feed it the fully qualified datadir path.
      bitcoind wants to create the wallets folder regardless of start
      condition so pass it the correct datadir if we know it, or a
      temp folder.
      ***/
      std::filesystem::path targetDir = dataDir;
      if (dataDir.empty()) {
         auto randomFragemnt = Cryptography::PRNG::fortuna.generateRandom(4);
         targetDir = std::filesystem::temp_directory_path() / randomFragemnt.toHexStr();
         std::filesystem::create_directory(targetDir);
      }

      if (!targetDir.is_absolute()) {
         //is this a fully qualified path?
         targetDir = std::filesystem::absolute(dataDir);
      }

      //run bitcoind --version
      auto result = spawnProcess(binPath,
         {{"--datadir", targetDir}},
         {"--version"},
         {}, true
      );
      result.first.wait();

      if (dataDir.empty()) {
         std::filesystem::remove_all(targetDir);
      }

      try {
         return { true, getVersionStringFromOutput(result.second) };
      } catch (const std::exception& e) {
         return { false, e.what() };
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   class ShutdownCallback : public RemoteCallback
   {
   private:
      std::unique_ptr<std::promise<bool>> promPtr_;
      std::future<bool> fut_;

   public:
      ShutdownCallback()
      {
         promPtr_ = std::make_unique<std::promise<bool>>();
         fut_ = promPtr_->get_future();
      }

      //virtuals
      void run(BdmNotification) override
      {}

      void progress(BDMPhase,
         const std::vector<std::string>&,
         float, unsigned, unsigned
      ) override
      {}

      void disconnected() override
      {
         promPtr_->set_value(true);
      }

      void waitOnDisconnect()
      {
         fut_.get();
      }
   };
}

////////////////////////////////////////////////////////////////////////////////
BdvPtr Armory::Bridge::setupClientConnection(
   std::shared_ptr<NetworkPeers::ClientStore> peers,
   const std::string& ip, Network::port_t port, bool oneWayAuth,
   const std::function<bool(const BinaryData&)>& presentPubKeyFunc,
   std::shared_ptr<RemoteCallback> cbPtr)
{
   LOGINFO << "connecting to ArmoryDB by IP";

   //sanity check
   if (peers == nullptr) {
      throw std::runtime_error("null peers db");
   }

   //setup bdv obj
   BdvPtr bdvPtr;
   unsigned count = 0;
   while (count++ < 10) {
      bdvPtr = AsyncClient::BlockDataViewer::getNewBDV(
         ip, port,
         peers, oneWayAuth,
         cbPtr
      );

      if (presentPubKeyFunc) {
         bdvPtr->setCheckServerKeyPromptLambda(presentPubKeyFunc);
      }

      //connect to db
      if (!bdvPtr->connectToRemote()) {
         //could not connect, sleep for 250ms and try again
         std::this_thread::sleep_for(250ms);
      }
      bdvPtr->registerWithDB(
         Config::BitcoinSettings::getMagicBytes().toHexStr());

      //notify setup is done
      return bdvPtr;
   }

   LOGERR << "failed to connect to armorydb";
   return nullptr;
}

BdvPtr Armory::Bridge::setupClientConnection(
   std::shared_ptr<NetworkPeers::ClientStore> peers,
   const NetworkPeers::PeerKey& peerObj,
   std::shared_ptr<RemoteCallback> cbPtr)
{
   LOGINFO << "connecting to ArmoryDB by peer";

   auto view = peers->getView(peerObj.isOneWay() ?
      NetworkPeers::PeerType::ServerOneWay :
      NetworkPeers::PeerType::ServerTwoWay);
   auto peerNames = view->getPeerNameMap();
   for (const auto& peerName : peerNames) {
      if (peerObj.getKey() != peerName.second) {
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

////////////////////////////////////////////////////////////////////////////////
std::filesystem::path Node::Core::findDatadir()
{
   auto home = FileUtils::getUserHomePath();
   for (const auto& candidate : satoshiDirCandidates) {
      std::filesystem::path dataDir = home / candidate;

      //look for /blocks folder
      auto blocksDir = getBlocksDir(dataDir);
      if (validateBlocksDir(blocksDir).first == true) {
         return dataDir;
      }
   }
   throw std::runtime_error("could not find a valid satoshi datadir");
}

std::filesystem::path Node::Core::findBinary()
{
   //look for bitcoind
   for (const auto& candidate : satoshiDirCandidates) {
      auto binPath = std::filesystem::path{candidate} / bitcoind;
      if (validateSatoshiBinary(binPath, {}).first == true) {
         return binPath;
      }
   }
   throw std::runtime_error("could not find a valid satoshi binary");}

////////
Node::Core::DatadirState Node::Core::validateDatadir(
   const std::filesystem::path& datadir)
{
   auto satoshiDir = getSatoshiDatadir(datadir);
   auto blocksDirValidation = validateBlocksDir(satoshiDir);
   if (blocksDirValidation.first == false) {
      throw std::runtime_error(
         std::format("{} is not a valid satoshi datadir", satoshiDir.string()));
   }

   //inspect files in the folder
   bool hasCookie = false;
   std::vector<std::string> confLines;
   for (const auto& entry : std::filesystem::directory_iterator{satoshiDir}) {
      if (!entry.is_regular_file()) {
         continue;
      }

      auto fpath = entry.path();
      const auto& fName = fpath.filename();
      if (fName == bitcoinConfFile) {
         confLines = Config::SettingsUtils::getLines(fpath);
      } else if (fName == cookieFile) {
         hasCookie = true;
      }
   }

   bool isPruned = false;
   bool hasRpcLog = false;
   bool hasRpcPass = false;
   for (const auto& confLine : confLines) {
      auto keyval = Config::SettingsUtils::getKeyValFromLine(confLine, '=');
      if (keyval.first == prunedKey) {
         const auto& val = keyval.second;
         int result = INT32_MAX;
         auto charConvResult = std::from_chars(
            val.data(), val.data() + val.size(), result);
         if (charConvResult.ec == std::errc{} && result != 0) {
            isPruned = true;
         }
      } else if (keyval.first == rpcLogKey) {
         if (!keyval.second.empty()) {
            hasRpcLog = true;
         }
      } else if (keyval.second == rpcPassKey) {
         if (!keyval.second.empty()) {
            hasRpcPass = true;
         }
      }
   }

   //TODO: cookie detection has to be more subtle

   return DatadirState{
      satoshiDir,
      blocksDirValidation.second,
      isPruned
   };
}

Node::Core::BinaryState Node::Core::validateBinary(
   const std::filesystem::path& binPath)
{
   auto result = validateSatoshiBinary(binPath, {});
   if (result.first == false) {
      throw std::runtime_error(result.second);
   }
   return { binPath, result.second };
}

////////////////////////////////////////////////////////////////////////////////
// ProcessInstance
ProcessInstance::ProcessInstance() :
   instance_{INVALID_INSTANCE}
{}

ProcessInstance::ProcessInstance(instance_t instance) :
   instance_{instance}
{}

bool ProcessInstance::isValid() const
{
   return instance_ != INVALID_INSTANCE;
}

bool ProcessInstance::isRunning()
{
   if (!isValid()) {
      return false;
   }

#ifdef _WIN32
   if (WaitForSingleObject(instance_, 0) != WAIT_TIMEOUT) {
      //we need to close this handle after use
      CloseHandle(instance_);
      instance_ = INVALID_INSTANCE;
      return false;
   }
#else
   siginfo_t processInfo;
   memset(&processInfo, 0, sizeof(processInfo));
   if (waitid(P_PID, (pid_t)instance_, &processInfo, WEXITED | WNOHANG) != 0) {
      return false;
   }

   if (processInfo.si_pid == 0) {
      return true;
   }
   if (processInfo.si_code == CLD_EXITED || processInfo.si_code == CLD_KILLED) {
      instance_ = INVALID_INSTANCE;
      return false;
   }
#endif
   return true;
}

void ProcessInstance::wait() const
{
   //wait on process to close
   if (!isValid()) {
      return;
   }

#ifdef _WIN32
   throw std::runtime_error("implement me");
#else
   int waitpid_status;
   waitpid(instance_, &waitpid_status, 0);
#endif
}

////////////////////////////////////////////////////////////////////////////////
// AutomationContext
AutomationContext::AutomationContext(
   const std::filesystem::path& satoshiDir,
   const std::filesystem::path& satoshiBin,
   const std::filesystem::path& dbDir,
   bool automateNode, bool automateDb) :
   satoshiDir_{satoshiDir}, satoshiBin_{satoshiBin}, dbDir_{dbDir},
   automateNode_{automateNode}, automateDb_{automateDb}
{}

Network::port_t AutomationContext::getDbPort() const
{
   return dbPort_;
}

std::shared_ptr<NetworkPeers::ClientStore> AutomationContext::getPeerStore() const
{
   return peers_;
}

////////
void AutomationContext::automateSatoshi()
{
   //sanity check
   if (nodeInstance_.isValid()) {
      throw std::runtime_error("already have an instance of Core");
   }

   LOGINFO << "spawning Bitcoin Core";

   //randomize rpc log and pass
   rpcLogin_ = Cryptography::PRNG::fortuna.generateRandom(16).toHexStr();
   rpcPass_ = Cryptography::PRNG::fortuna.generateRandom(16).toHexStr();
   auto salt = Cryptography::PRNG::fortuna.generateRandom(16).toHexStr();
   auto saltedPass = BtcUtils::getSaltedRpcPass(salt, rpcPass_);

   //setup argv
   auto datadir = satoshiDir_;
   if (Config::BitcoinSettings::getMode() == Config::NETWORK_MODE_TESTNET) {
      if (satoshiDir_.stem() == "testnet3") {
         datadir = satoshiDir_.parent_path();
      }
   }

   std::vector<std::string> args{
      std::format("--rpcauth={}:{}${}", rpcLogin_, salt, saltedPass.toHexStr()),
      {"--disablewallet"}
   };
   if (Config::BitcoinSettings::getMode() == Config::NETWORK_MODE_TESTNET) {
      args.emplace_back("--testnet");
   }
   auto result = spawnProcess(satoshiBin_,
      {{"--datadir", datadir}},
      args, {}, false
   );
   nodeInstance_ = result.first;
   if (!nodeInstance_.isValid()) {
      throw std::runtime_error(std::format(
         "failed to spawn bitcoind with error: {}", strerror(errno)));
   }
}

bool AutomationContext::isSatoshiRunning()
{
   return nodeInstance_.isRunning();
}

void AutomationContext::cleanupSatoshi()
{
   /*
   If we started the core node, connect to it via RPC and ask it to shutdown
   */

   //TODO: use callback to notify of shutdown progression

   if (!nodeInstance_.isValid()) {
      //not our node
      return;
   }

   try {
      //connect to RPC
      auto rpc = Node::Core::RPC::Client(false, rpcLogin_, rpcPass_);

      //request shutdown
      if (!rpc.shutdown()) {
         throw std::runtime_error("request was rejected by node");
      }

      //wait on process
      nodeInstance_.wait();
   } catch (const JSON::Exception& e) {
      LOGERR << "rpc shutdown request failed with error: " << e.what();
   }
}

////////////////////////////////////////////////////////////////////////////////
void AutomationContext::automateDb()
{
   LOGINFO << "spawning ArmoryDB";

   /*
   Spawn ArmoryDB with tailored CLI args and environment variables, to setup
   an adhoc AEAD 2-way handshake.

   2-way Keys are exchange via the following these steps:
      1. CppBridge creates an ephemeral key store and adds its public key to
         to ArmoryDB via envvars.
      2. CppBridge spawns ArmoryDB, replacing stdout by a pipe.
      3. ArmoryDB detects automation via the --ephemeral CLI arg.
         It creates an ephemeral key store, reads the caller pubkey from
         envvars, adds it to its store and sets it as the store's master key.
      4. ArmoryDB writes its public key to stdout.
      5. CppBridge reads ArmoryDB's stdout, grabs the pubkey and
         injects it into its own store.
   */

   //sanity check
   if (dbInstance_.isValid()) {
      throw std::runtime_error("already have an instance of ArmoryDB");
   }

   //get full path to armorydb
#ifdef _WIN32
   const std::filesystem::path armoryDbPath{
      Config::Pathing::runningDir() / "ArmoryDB.exe" };
#else
   const std::filesystem::path armoryDbPath{
      Config::Pathing::runningDir() / "ArmoryDB" };
#endif
   if (!FileUtils::pathExists(armoryDbPath, 8)) {
      throw std::runtime_error("invalid db binary path: " + armoryDbPath.string());
   }

   //setup ephemeral authPeers
   peers_ = std::make_shared<NetworkPeers::ClientStore>();
   NetworkPeers::PeerKey myKey{
      peers_->getOwnPublicKey(),
      NetworkPeers::PeerType::Client
   };

   //generate random db port & set it
   dbPort_ = (rand() % 10000) + 50000;
   Armory::Config::NetworkSettings::setDbPort(dbPort_);

   //args
   std::vector<std::string> args{
      { "--ephemeral" },
      std::format("--armorydb-port={}", dbPort_),
      std::format("--satoshi-port={}", Config::NetworkSettings::btcPort()),
      std::format("--satoshirpc-port={}", Config::NetworkSettings::rpcPort())
   };
   switch (Config::BitcoinSettings::getMode())
   {
      case Config::NETWORK_MODE_TESTNET:
         args.emplace_back("--testnet");
         break;

      case Config::NETWORK_MODE_REGTEST:
         args.emplace_back("--regtest");
         break;

      default:
         break;
   }

   //envvars
   std::map<std::string, std::string> envvars{
      {"MASTER_PUBKEY", myKey.toHumanReadable()}
   };

   //optionals
   if (automateNode_) {
      args.emplace_back("--automated-node");
      envvars.emplace("CORERPCLOG", rpcLogin_);
      envvars.emplace("CORERPCPASS", rpcPass_);
   }

   auto result = spawnProcess(armoryDbPath, {
         {"--dbdir", dbDir_},
         {"--datadir", Config::getDataDir()},
         {"--satoshi-datadir", satoshiDir_}
      }, args, envvars, true
   );
   dbInstance_ = result.first;
   if (!dbInstance_.isValid()) {
      throw std::runtime_error(std::format(
         "failed to spawn ArmoryDB with error: {}",
         strerror(errno))
      );
   }

   //set db pubkey
   auto serverKey = NetworkPeers::PeerKey::fromHumanReadable(result.second);
   peers_->addPeer(serverKey, {std::format("127.0.0.1:{}", dbPort_)}, {});
}

////
bool AutomationContext::isDbRunning()
{
   return dbInstance_.isRunning();
}

////////
void AutomationContext::cleanupDb()
{
   if (!dbInstance_.isValid()) {
      //not our db
      return;
   }

   //create bdv object
   auto callback = std::make_shared<ShutdownCallback>();
   auto bdvPtr = setupClientConnection(peers_,
      "127.0.0.1", dbPort_,
      false, nullptr, callback);
   if (bdvPtr == nullptr) {
      throw std::runtime_error("automatedDb connection failed");
   }

   //request db shutdown, wait on d/c notif
   bdvPtr->shutdown();
   callback->waitOnDisconnect();
   bdvPtr.reset();

   //wait on db shutdown
   while (isDbRunning()) {
      std::this_thread::sleep_for(100ms);
   }
}

////////////////////////////////////////////////////////////////////////////////
bool AutomationContext::run(const CallbackFunc& notifyStep)
{
   if (hasRun_) {
      throw std::runtime_error("context already in use");
   }
   hasRun_ = true;

   if (automateDb_) {
      //we always automate the db if we automate the node
      if (automateNode_) {
         notifyStep(AutomationStep::SpawnNode);
         automateSatoshi();
      }
      notifyStep(AutomationStep::SpawnDb);
      automateDb();
      return true;
   } else {
      return false;
   }
}

void AutomationContext::cleanup(const CallbackFunc& notifyStep)
{
   if (!hasRun_) {
      notifyStep(AutomationStep::Done);
      return;
   }
   hasRun_ = false;

   if (automateDb_) {
      notifyStep(AutomationStep::ShutdownDb);
      cleanupDb();
   }
   if (automateNode_) {
      notifyStep(AutomationStep::ShutdownNode);
      cleanupSatoshi();
   }
   notifyStep(AutomationStep::Done);
}
