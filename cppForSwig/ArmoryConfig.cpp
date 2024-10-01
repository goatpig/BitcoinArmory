////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ArmoryConfig.h"
#include "BtcUtils.h"
#include "DBUtils.h"
#include "DbHeader.h"
#include "EncryptionUtils.h"
#include "JSON_codec.h"
#include "SocketObject.h"
#include "BIP150_151.h"
#include "BitcoinP2p.h"
#include "BitcoinSettings.h"
#include "nodeRPC.h"

#include "gtest/NodeUnitTest.h"

#ifndef _WIN32
#include "sys/stat.h"
#endif

using namespace std;
using namespace Armory;
using namespace Armory::Config;

////////////////////////////////////////////////////////////////////////////////
#define DEFAULT_DBDIR_SUFFIX "/databases"

#if defined(_WIN32)
#define MAINNET_DEFAULT_DATADIR "~/Armory"
#define TESTNET_DEFAULT_DATADIR "~/Armory/testnet3"
#define REGTEST_DEFAULT_DATADIR "~/Armory/regtest"

#define MAINNET_DEFAULT_BLOCKPATH "~/Bitcoin/blocks"
#define TESTNET_DEFAULT_BLOCKPATH "~/Bitcoin/testnet3/blocks"
#define REGTEST_DEFAULT_BLOCKPATH "~/Bitcoin/regtest/blocks"

#elif defined(__APPLE__)
#define MAINNET_DEFAULT_DATADIR "~/Library/Application Support/Armory"
#define TESTNET_DEFAULT_DATADIR "~/Library/Application Support/Armory/testnet3"
#define REGTEST_DEFAULT_DATADIR "~/Library/Application Support/Armory/regtest"

#define MAINNET_DEFAULT_BLOCKPATH "~/Library/Application Support/Bitcoin/blocks"
#define TESTNET_DEFAULT_BLOCKPATH "~/Library/Application Support/Bitcoin/testnet3/blocks"
#define REGTEST_DEFAULT_BLOCKPATH "~/Library/Application Support/Bitcoin/regtest/blocks"

#else
#define MAINNET_DEFAULT_DATADIR "~/.armory"
#define TESTNET_DEFAULT_DATADIR "~/.armory/testnet3"
#define REGTEST_DEFAULT_DATADIR "~/.armory/regtest"

#define MAINNET_DEFAULT_BLOCKPATH "~/.bitcoin/blocks"
#define TESTNET_DEFAULT_BLOCKPATH "~/.bitcoin/testnet3/blocks"
#define REGTEST_DEFAULT_BLOCKPATH "~/.bitcoin/regtest/blocks"

#endif

////////////////////////////////////////////////////////////////////////////////
void Armory::Config::printHelp(void)
{
  static std::string helpMsg = R"(
--help                     print help message and exit
--testnet                  run db against testnet bitcoin network
--regtest                  run db against regression test network
--rescan                   delete all processed history data and rescan
                           blockchain from the first block
--rebuild                  delete all DB data and build and scan from scratch
--rescanSSH                delete balance and txcount data and rescan it.
                           Much faster than rescan or rebuild.
--checkchain               builds db (no scanning) with full txhints, then
                           verifies all tx (consensus and sigs).
--datadir                  path to the operation folder
--dbdir                    path to folder containing the database files.
                           If empty, a new db will be created there
--satoshi-datadir          path to blockchain data folder (blkXXXXX.dat files)
--ram-usage                defines the ram use during scan operations.
                           1 level averages 128MB of ram (without accounting the
                           base amount, ~400MB). Defaults at 50.
                           Can't be lower than 1.
                           Can be changed in between processes
--thread-count             defines how many processing threads can be used during
                           db builds and scans. Defaults to maximum available CPU
                           threads. Can't be lower than 1. Can be changed in
                           between processes
--zcthread-count           defines the maximum number on threads the zc parser
                           can create for processing incoming transcations from
                           the network node
--db-type                  sets the db type:
                           DB_BARE:  tracks wallet history only. Smallest DB.
                           DB_FULL:  tracks wallet history and resolves all
                              relevant tx hashes. ~2.4GB DB at the time
                              of 0.97 release. Default DB type.
                           DB_SUPER: tracks all blockchain history.
                              XXL DB (100GB+).
                           db type cannot be changed in between processes.
                           Once a db has been built with a certain type, it will
                           always function according to that type.
                           Specifying another type will do nothing. Build a new
                           db to change type.
--cookie                   create a cookie file holding a random authentication
                           key to allow local clients to make use of elevated
                           commands, like shutdown. Client and server will make
                           use of ephemeral peer keys, ignoring the on disk peer
                           wallet
--armorydb-port            DB port to connect to.
--armorydb-ip              DB IP to connect to.
--clear-mempool            delete all zero confirmation transactions from the DB.
--satoshirpc-port          set node rpc port
--satoshi-port             set Bitcoin node port
--public                   BIP150 auth will allow for anonymous requesters.
                           While only clients can be anon (servers/responders are
                           always auth'ed), both sides need to enable public
                           channels for the handshake to succeed)   
--offline                  Do not seek to connect with the ArmoryDB blockchain
                           service)";

   cerr << helpMsg << endl;
}

////////////////////////////////////////////////////////////////////////////////
const string& Armory::Config::getDataDir()
{
   return BaseSettings::dataDir_;
}

////////////////////////////////////////////////////////////////////////////////
void Armory::Config::parseArgs(int argc, char* argv[], ProcessType procType)
{
   vector<string> lines;
   lines.reserve(argc);
   for (int i=1; i<argc; i++)
      lines.emplace_back(argv[i], strlen(argv[i]));

   Armory::Config::parseArgs(lines, procType);
}

////////////////////////////////////////////////////////////////////////////////
void Armory::Config::parseArgs(
   const vector<string>& lines, ProcessType procType)
{
   unique_lock<mutex> lock(BaseSettings::configMutex_);
   if (BaseSettings::initCount_++ > 0)
   {
      LOGERR << "Trying to override config";
      throw runtime_error("Trying to override config");
   }

   /*
   1. figure out the network (mainnet, testnet, unit tests)
   2. figure out the datadir
   3. grab the config file if any, parse and add to arg map
   4. finally, parse arg map for everything else
   */

   try
   {
      //parse command line args
      map<string, string> args;
      for (const auto& line : lines)
      {
         if (line == ("--help")) {
            Armory::Config::printHelp();
            exit(0);
         }

         //string prefix and tokenize
         auto strings = SettingsUtils::tokenizeLine(line, "--");
         for (auto& line : strings)
         {
            auto keyVal = SettingsUtils::getKeyValFromLine(line, '=');

            args.insert(make_pair(
               keyVal.first, SettingsUtils::stripQuotes(keyVal.second)));
         }
      }

      //figure out the network
      BitcoinSettings::processArgs(args);

      //datadir
      BaseSettings::detectDataDir(args);

      //get config file
      auto configPath = Armory::Config::getDataDir();
      DBUtils::appendPath(configPath, "armorydb.conf");

      if (SettingsUtils::fileExists(configPath, 2))
      {
         Config::File cf(configPath);
         auto mapIter = cf.keyvalMap_.find("datadir");
         if (mapIter != cf.keyvalMap_.end())
            throw DbErrorMsg("datadir is illegal in .conf file");

         //parse config file for network arg
         BitcoinSettings::processArgs(cf.keyvalMap_);

         //merge with regular file
         args.insert(cf.keyvalMap_.begin(), cf.keyvalMap_.end());
      }

      //parse for networking
      NetworkSettings::processArgs(args, procType);

      //parse for paths
      Pathing::processArgs(args, procType);

      //db settings
      DBSettings::processArgs(args);
   }
   catch (const Config::Error& e)
   {
      cerr << e.what() << endl;
      throw e;
   }
}

////////////////////////////////////////////////////////////////////////////////
void Armory::Config::reset()
{
   unique_lock<mutex> lock(BaseSettings::configMutex_);

   NetworkSettings::reset();
   Pathing::reset();
   DBSettings::reset();
   BaseSettings::reset();
}

////////////////////////////////////////////////////////////////////////////////
//
// SettingsUtils
//
////////////////////////////////////////////////////////////////////////////////
vector<string> SettingsUtils::getLines(const string& path)
{
   vector<string> output;
   fstream fs(path, ios_base::in);

   while (fs.good())
   {
      string str;
      getline(fs, str);
      output.push_back(move(str));
   }

   return output;
}

////////////////////////////////////////////////////////////////////////////////
map<string, string> SettingsUtils::getKeyValsFromLines(
   const vector<string>& lines, char delim)
{
   map<string, string> output;
   for (auto& line : lines)
      output.insert(move(getKeyValFromLine(line, delim)));

   return output;
}

////////////////////////////////////////////////////////////////////////////////
pair<string, string> SettingsUtils::getKeyValFromLine(
   const string& line, char delim)
{
   stringstream ss(line);
   pair<string, string> output;

   //key
   getline(ss, output.first, delim);

   //val
   if (ss.good())
      getline(ss, output.second);

   return output;
}

////////////////////////////////////////////////////////////////////////////////
vector<string> SettingsUtils::tokenizeLine(
   const string& line, const string& token)
{
   if (token.empty() || line.empty())
      return {};

   vector<string> result;

   unsigned i=0;
   unsigned tkId = 0;
   while (i < line.size())
   {
      if (line.c_str()[i] == token.c_str()[tkId])
      {
         ++tkId;
         if (tkId == token.size())
         {
            ++i;
            auto y = i;
            while (i < line.size() -1)
            {
               if (line.c_str()[i] == ' ')
                  break;
               ++i;
            }

            if (i >= y)
            {
               //keep last char in the line
               if (i==line.size() -1)
                  ++i;
               
               string str(line.c_str() + y, i-y);
               result.emplace_back(move(str));
            }

            tkId = 0;
         }
      }
      else
      {
         tkId = 0;
      }

      ++i;
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
vector<string> SettingsUtils::keyValToArgv(
   const map<string, string>& keyValMap)
{
   vector<string> argv;

   for (auto& keyval : keyValMap)
   {
      stringstream ss;
      if (keyval.first.compare(0, 2, "--") != 0)
         ss << "--";
      ss << keyval.first;

      if (keyval.second.size() != 0)
         ss << "=" << keyval.second;

      argv.push_back(ss.str());
   }

   return argv;
}

////////////////////////////////////////////////////////////////////////////////
bool SettingsUtils::fileExists(const string& path, int mode)
{
#ifdef _WIN32
   return _access(path.c_str(), mode) == 0;
#else
   auto nixmode = F_OK;
   if (mode & 2)
      nixmode |= R_OK;
   if (mode & 4)
      nixmode |= W_OK;
   auto result = access(path.c_str(), nixmode);
   return result == 0;
#endif
}

////////////////////////////////////////////////////////////////////////////////
string SettingsUtils::portToString(unsigned port)
{
   stringstream ss;
   ss << port;
   return ss.str();
}

////////////////////////////////////////////////////////////////////////////////
string SettingsUtils::stripQuotes(const string& input)
{
   size_t start = 0;
   size_t len = input.size();

   auto& first_char = input.c_str()[0];
   auto& last_char = input.c_str()[len - 1];

   if (first_char == '\"' || first_char == '\'')
   {
      start = 1;
      --len;
   }

   if (last_char == '\"' || last_char == '\'')
      --len;

   return input.substr(start, len);
}

////////////////////////////////////////////////////////////////////////////////
bool SettingsUtils::testConnection(const string& ip, const string& port)
{
   SimpleSocket testSock(ip, port);
   return testSock.testConnection();
}

////////////////////////////////////////////////////////////////////////////////
string SettingsUtils::getPortFromCookie(const string& datadir)
{
   //check for cookie file
   string cookie_path = datadir;
   DBUtils::appendPath(cookie_path, ".cookie_");
   auto&& lines = SettingsUtils::getLines(cookie_path);
   if (lines.size() != 2)
      return string();

   return lines[1];
}

////////////////////////////////////////////////////////////////////////////////
string SettingsUtils::hasLocalDB(const string& datadir, const string& port)
{
   //check db on provided port
   if (SettingsUtils::testConnection("127.0.0.1", port))
      return port;

   //check db on default port
   if (SettingsUtils::testConnection(
      "127.0.0.1", SettingsUtils::portToString(LISTEN_PORT_MAINNET)))
   {
      return SettingsUtils::portToString(LISTEN_PORT_MAINNET);
   }

   //check for cookie file
   auto&& cookie_port = getPortFromCookie(datadir);
   if (cookie_port.size() == 0)
      return string();

   if (SettingsUtils::testConnection("127.0.0.1", cookie_port))
      return cookie_port;

   return string();
}

////////////////////////////////////////////////////////////////////////////////
//
// BaseSettings
//
////////////////////////////////////////////////////////////////////////////////
mutex BaseSettings::configMutex_;
string BaseSettings::dataDir_;
unsigned BaseSettings::initCount_ = 0;

////////////////////////////////////////////////////////////////////////////////
void BaseSettings::detectDataDir(map<string, string>& args)
{
   //figure out the datadir
   auto argIter = args.find("datadir");
   if (argIter != args.end())
   {
      dataDir_ = argIter->second;
      args.erase(argIter);
   }
   else
   {
      switch (BitcoinSettings::getMode())
      {
      case NETWORK_MODE_MAINNET:
      {
         dataDir_ = MAINNET_DEFAULT_DATADIR;
         break;
      }

      case NETWORK_MODE_TESTNET:
      {
         dataDir_ = TESTNET_DEFAULT_DATADIR;
         break;
      }

      case NETWORK_MODE_REGTEST:
      {
         dataDir_ = REGTEST_DEFAULT_DATADIR;
         break;
      }

      default:
         LOGERR << "unexpected network mode";
         throw runtime_error("unexpected network mode");
      }
   }

   DBUtils::expandPath(dataDir_);
}

////////////////////////////////////////////////////////////////////////////////
void BaseSettings::reset()
{
   dataDir_.clear();
   initCount_ = 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// DBSettings
//
////////////////////////////////////////////////////////////////////////////////
BDM_INIT_MODE DBSettings::initMode_ = INIT_RESUME;
ARMORY_DB_TYPE DBSettings::armoryDbType_ = ARMORY_DB_FULL;
SOCKET_SERVICE DBSettings::service_ = SERVICE_WEBSOCKET;

unsigned DBSettings::ramUsage_ = 4;
unsigned DBSettings::threadCount_ = thread::hardware_concurrency();
unsigned DBSettings::zcThreadCount_ = DEFAULT_ZCTHREAD_COUNT;

bool DBSettings::reportProgress_ = true;
bool DBSettings::checkChain_ = false;
bool DBSettings::clearMempool_ = false;
bool DBSettings::checkTxHints_ = false;

////////////////////////////////////////////////////////////////////////////////
void DBSettings::processArgs(const map<string, string>& args)
{
   //db init options
   auto iter = args.find("rescanSSH");
   if (iter != args.end())
      initMode_ = INIT_SSH;

   iter = args.find("rescan");
   if (iter != args.end())
      initMode_ = INIT_RESCAN;

   iter = args.find("rebuild");
   if (iter != args.end())
      initMode_ = INIT_REBUILD;

   iter = args.find("checkchain");
   if (iter != args.end())
      checkChain_ = true;

   iter = args.find("clear-mempool");
   if (iter != args.end())
      clearMempool_ = true;

   iter = args.find("check-txhints");
   if (iter != args.end())
      checkTxHints_ = true;

   //db type
   iter = args.find("db-type");
   if (iter != args.end())
   {
      if (iter->second == "DB_BARE")
      {
         throw runtime_error("deprecated");
         armoryDbType_ = ARMORY_DB_BARE;
      }
      else if (iter->second == "DB_FULL")
      {
         armoryDbType_ = ARMORY_DB_FULL;
      }
      else if (iter->second == "DB_SUPER")
      {
         armoryDbType_ = ARMORY_DB_SUPER;
      }
      else
      {
         cout << "Error: unexpected DB type: " << iter->second << endl;
         printHelp();
         exit(0);
      }
   }

   //resource control
   iter = args.find("thread-count");
   if (iter != args.end())
   {
      int val = 0;
      try
      {
         val = stoi(iter->second);
      }
      catch (...)
      {
      }

      if (val > 0)
         threadCount_ = val;
   }

   iter = args.find("ram-usage");
   if (iter != args.end())
   {
      int val = 0;
      try
      {
         val = stoi(iter->second);
      }
      catch (...)
      {
      }

      if (val > 0)
         ramUsage_ = val;
   }

   iter = args.find("zcthread-count");
   if (iter != args.end())
   {
      int val = 0;
      try
      {
         val = stoi(iter->second);
      }
      catch (...)
      {
      }

      if (val > 0)
         zcThreadCount_ = val;
   }
}

////////////////////////////////////////////////////////////////////////////////
string DBSettings::getCookie(const string& datadir)
{
   string cookie_path = datadir;
   DBUtils::appendPath(cookie_path, ".cookie_");
   auto&& lines = SettingsUtils::getLines(cookie_path);
   if (lines.size() != 2)
      return string();

   return lines[0];
}

////////////////////////////////////////////////////////////////////////////////
string DBSettings::getDbModeStr()
{
   switch(getDbType())
   {
   case ARMORY_DB_BARE: 
      return "DB_BARE";

   case ARMORY_DB_FULL:
      return "DB_FULL";
  
   case ARMORY_DB_SUPER:
      return "DB_SUPER";

   default:
      throw runtime_error("invalid db type!");
   }
}

////////////////////////////////////////////////////////////////////////////////
void DBSettings::reset()
{
   initMode_ = INIT_RESUME;
   armoryDbType_ = ARMORY_DB_FULL;
   service_ = SERVICE_WEBSOCKET;

   ramUsage_ = 4;
   threadCount_ = thread::hardware_concurrency();
   zcThreadCount_ = DEFAULT_ZCTHREAD_COUNT;

   reportProgress_ = true;  
   checkChain_ = false;
   clearMempool_ = false;
}

////////////////////////////////////////////////////////////////////////////////
//
// NetworkSettings
//
////////////////////////////////////////////////////////////////////////////////
bool NetworkSettings::customDbPort_ = false;
bool NetworkSettings::customBtcPort_ = false;

NetworkSettings::NodePair NetworkSettings::bitcoinNodes_;
NetworkSettings::RpcPtr NetworkSettings::rpcNode_;

string NetworkSettings::btcPort_;
string NetworkSettings::dbPort_;
string NetworkSettings::dbIP_;
string NetworkSettings::rpcPort_;

bool NetworkSettings::useCookie_ = false;
bool NetworkSettings::ephemeralPeers_;
bool NetworkSettings::oneWayAuth_ = false;
bool NetworkSettings::offline_ = false;

string NetworkSettings::cookie_;
BinaryData NetworkSettings::uiPublicKey_;

////////////////////////////////////////////////////////////////////////////////
void NetworkSettings::processArgs(const map<string, string>& args,
   ProcessType procType)
{
   auto iter = args.find("armorydb-port");
   if (iter != args.end()) {
      dbPort_ = SettingsUtils::stripQuotes(iter->second);
      int portInt = 0;
      stringstream portSS(dbPort_);
      portSS >> portInt;

      if (portInt < 1 || portInt > 65535) {
         cout << "Invalid listen port, falling back to default" << endl;
         dbPort_ = "";
      } else {
         customDbPort_ = true;
      }
   }

   iter = args.find("armorydb-ip");
   if (iter != args.end()) {
      dbIP_ = SettingsUtils::stripQuotes(iter->second);
   }

   iter = args.find("satoshi-port");
   if (iter != args.end()) {
      btcPort_ = SettingsUtils::stripQuotes(iter->second);
      customBtcPort_ = true;
   }

   //network type
   iter = args.find("testnet");
   if (iter != args.end()) {
      selectNetwork(NETWORK_MODE_TESTNET);
   } else {
      iter = args.find("regtest");
      if (iter != args.end()) {
         selectNetwork(NETWORK_MODE_REGTEST);
      } else {
         selectNetwork(NETWORK_MODE_MAINNET);
      }
   }

   //rpc port
   iter = args.find("satoshirpc-port");
   if (iter != args.end()) {
      auto value = SettingsUtils::stripQuotes(iter->second);
      int portInt = 0;
      stringstream portSS(value);
      portSS >> portInt;

      if (portInt < 1 || portInt > 65535) {
         cout << "Invalid satoshi rpc port, falling back to default" << endl;
      } else {
         rpcPort_ = value;
      }
   }

   //public
   iter = args.find("public");
   if (iter != args.end()) {
      oneWayAuth_ = true;
   }

   //offline
   iter = args.find("offline");
   if (iter != args.end()) {
      offline_ = true;
   }

   //ui pubkey
   iter = args.find("uiPubKey");
   if (iter != args.end()) {
      uiPublicKey_ = READHEX(iter->second);
   }

   //cookie
   iter = args.find("cookie");
   if (iter != args.end()) {
      useCookie_ = true;
      ephemeralPeers_ = true;
   }

   //generate cookie
   cookie_ = BtcUtils::fortuna_.generateRandom(32).toHexStr();

   if (offline_) {
      return;
   }

   if (useCookie_) {
      randomizeDbPort();
      createCookie();
   } else if (DBSettings::getServiceType() == SERVICE_UNITTEST ||
      DBSettings::getServiceType() == SERVICE_UNITTEST_WITHWS) {
      randomizeDbPort();
   }

   if (procType == ProcessType::DB) {
      createNodes();
   }
}

////////////////////////////////////////////////////////////////////////////////
void NetworkSettings::selectNetwork(NETWORK_MODE mode)
{
   switch (mode)
   {
   case NETWORK_MODE_MAINNET:
   {
      rpcPort_ = SettingsUtils::portToString(RPC_PORT_MAINNET);

      if (!customDbPort_) {
         dbPort_ = SettingsUtils::portToString(LISTEN_PORT_MAINNET);
      }

      if (!customBtcPort_) {
         btcPort_ = SettingsUtils::portToString(NODE_PORT_MAINNET);
      }
      break;
   }

   case NETWORK_MODE_TESTNET:
   {
      rpcPort_ = SettingsUtils::portToString(RPC_PORT_TESTNET);

      if (!customDbPort_) {
         dbPort_ = SettingsUtils::portToString(LISTEN_PORT_TESTNET);
      }

      if (!customBtcPort_) {
         btcPort_ = SettingsUtils::portToString(NODE_PORT_TESTNET);
      }
      break;
   }

   case NETWORK_MODE_REGTEST:
   {
      rpcPort_ = SettingsUtils::portToString(RPC_PORT_REGTEST);

      if (!customDbPort_) {
         dbPort_ = SettingsUtils::portToString(LISTEN_PORT_REGTEST);
      }

      if (!customBtcPort_) {
         btcPort_ = SettingsUtils::portToString(NODE_PORT_REGTEST);
      }
      break;
   }

   default:
      LOGERR << "unexpected network mode!";
      throw runtime_error("unxecpted network mode");
   }
}

////////////////////////////////////////////////////////////////////////////////
const string& NetworkSettings::btcPort()
{
   return btcPort_;
}

////////////////////////////////////////////////////////////////////////////////
const string& NetworkSettings::dbPort()
{
   return dbPort_;
}

////////////////////////////////////////////////////////////////////////////////
const string& NetworkSettings::dbIP()
{
   return dbIP_;
}

////////////////////////////////////////////////////////////////////////////////
const string& NetworkSettings::rpcPort()
{
   return rpcPort_;
}

////////////////////////////////////////////////////////////////////////////////
void NetworkSettings::randomizeDbPort()
{
   if (customDbPort_) {
      return;
   }

   //no custom listen port was provided and the db was spawned with a 
   //cookie file, listen port will be randomized
   srand(time(0));
   while (true) {
      auto port = rand() % 15000 + 50000;
      stringstream portss;
      portss << port;

      if (!SettingsUtils::testConnection("127.0.0.1", portss.str())) {
         dbPort_ = portss.str();
         break;
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void NetworkSettings::createNodes()
{
   auto magicBytes = BitcoinSettings::getMagicBytes();

   if (DBSettings::getServiceType() == SERVICE_WEBSOCKET)
   {
      bitcoinNodes_.first =
         make_shared<BitcoinP2P>("127.0.0.1", btcPort_,
         *(uint32_t*)magicBytes.getPtr(), false);

      bitcoinNodes_.second =
         make_shared<BitcoinP2P>("127.0.0.1", btcPort_,
         *(uint32_t*)magicBytes.getPtr(), true);

      rpcNode_ = make_shared<CoreRPC::NodeRPC>();
   }
   else
   {
      auto primary =
         make_shared<NodeUnitTest>(*(uint32_t*)magicBytes.getPtr(), false);

      auto watcher =
         make_shared<NodeUnitTest>(*(uint32_t*)magicBytes.getPtr(), true);

      bitcoinNodes_.first = primary;
      bitcoinNodes_.second = watcher;
      rpcNode_ = make_shared<NodeRPC_UnitTest>(primary, watcher);
   }
}

////////////////////////////////////////////////////////////////////////////////
const NetworkSettings::NodePair& NetworkSettings::bitcoinNodes()
{
   return bitcoinNodes_;
}

////////////////////////////////////////////////////////////////////////////////
NetworkSettings::RpcPtr NetworkSettings::rpcNode()
{
   return rpcNode_;
}

////////////////////////////////////////////////////////////////////////////////
void NetworkSettings::createCookie()
{
   //cookie file
   if (!useCookie_)
      return;

   if (DBSettings::getServiceType() == SERVICE_UNITTEST ||
      DBSettings::getServiceType() == SERVICE_UNITTEST_WITHWS)
      return;

   auto cookiePath = Armory::Config::getDataDir();
   DBUtils::appendPath(cookiePath, ".cookie_");
   fstream fs(cookiePath, ios_base::out | ios_base::trunc);
   fs << cookie_ << endl;
   fs << dbPort_;
}

////////////////////////////////////////////////////////////////////////////////
void NetworkSettings::injectUiPubkey(BinaryData& pubkey)
{
   uiPublicKey_ = move(pubkey);
}

////////////////////////////////////////////////////////////////////////////////
void NetworkSettings::reset()
{
   customDbPort_ = false;
   customBtcPort_ = false;

   bitcoinNodes_.first.reset();
   bitcoinNodes_.second.reset();
   rpcNode_.reset();

   btcPort_.clear();
   dbPort_.clear();
   dbIP_.clear();
   rpcPort_.clear();

   cookie_.clear();

   useCookie_ = false;
   ephemeralPeers_ = false;
   oneWayAuth_ = false;
   offline_ = false;
}

////////////////////////////////////////////////////////////////////////////////
//
// Pathing
//
////////////////////////////////////////////////////////////////////////////////
string Pathing::blkFilePath_;
string Pathing::dbDir_;

////////////////////////////////////////////////////////////////////////////////
void Pathing::processArgs(const map<string, string>& args, ProcessType procType)
{
   //paths
   auto iter = args.find("dbdir");
   if (iter != args.end())
      dbDir_ = SettingsUtils::stripQuotes(iter->second);

   iter = args.find("satoshi-datadir");
   if (iter != args.end())
      blkFilePath_ = SettingsUtils::stripQuotes(iter->second);

   bool autoDbDir = false;
   if (dbDir_.empty())
   {
      dbDir_ = Armory::Config::getDataDir();
      DBUtils::appendPath(dbDir_, DEFAULT_DBDIR_SUFFIX);
      autoDbDir = true;
   }

   if (blkFilePath_.empty())
   {
      switch (BitcoinSettings::getMode())
      {
      case NETWORK_MODE_MAINNET:
      {
         blkFilePath_ = MAINNET_DEFAULT_BLOCKPATH;
         break;
      }

      default:
         blkFilePath_ = TESTNET_DEFAULT_BLOCKPATH;
      }
   }

   //expand paths if necessary
   DBUtils::expandPath(dbDir_);
   DBUtils::expandPath(blkFilePath_);

   if (blkFilePath_.size() < 6 ||
      blkFilePath_.substr(blkFilePath_.length() - 6, 6) != "blocks")
   {
      DBUtils::appendPath(blkFilePath_, "blocks");
   }

   //test all paths
   auto testPath = [](const string& path, int mode)->bool
   {
      return SettingsUtils::fileExists(path, mode);
   };

   if (!testPath(Armory::Config::getDataDir(), 6))
   {
      string errMsg = Armory::Config::getDataDir() +
         " is not a valid datadir path";
      throw DbErrorMsg(errMsg); 
   }

   if (procType != ProcessType::DB)
   {
      //path checks past this point only apply to ArmoryDB
      return;
   }

   if (NetworkSettings::isOffline())
   {
      //skip checks on block and db folders in offline mode
      return;
   }

   //create dbdir if set automatically
   if (autoDbDir)
   {
      if (!testPath(dbDir_, 0))
      {
#ifdef _WIN32
         CreateDirectory(dbDir_.c_str(), NULL);
#else
         mkdir(dbDir_.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
      }
   }

   //now for the regular test, let it throw if it fails
   if (!testPath(dbDir_, 6))
   {
      string errMsg = dbDir_ + " is not a valid db path";
      throw DbErrorMsg(errMsg); 
   }

   /*
   TODO: differentiate path defaults and checks between local automated
      bitcoind, local manual bitcoind and remote armorydb
   */

   if (!NetworkSettings::isOffline())
   {
      if (!testPath(blkFilePath_, 2))
      {
         string errMsg = blkFilePath_ + " is not a valid blockchain data path";
         throw DbErrorMsg(errMsg); 
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void Pathing::reset()
{
   blkFilePath_.clear();
   dbDir_.clear();
}

////////////////////////////////////////////////////////////////////////////////
string Pathing::logFilePath(const string& logName)
{
   return getDataDir() + "/" + logName + ".txt";
}

////////////////////////////////////////////////////////////////////////////////
//
// ConfigFile
//
////////////////////////////////////////////////////////////////////////////////
Config::File::File(const string& path)
{
   auto&& lines = SettingsUtils::getLines(path);

   for (auto& line : lines)
   {
      auto&& keyval = SettingsUtils::getKeyValFromLine(line, '=');

      if (keyval.first.size() == 0)
         continue;

      if (keyval.first.compare(0, 1, "#") == 0)
         continue;

      keyvalMap_.insert(make_pair(
         keyval.first, SettingsUtils::stripQuotes(keyval.second)));
   }
}

////////////////////////////////////////////////////////////////////////////////
vector<BinaryData> Config::File::fleshOutArgs(
   const string& path, const vector<BinaryData>& argv)
{
   //sanity check
   if (path.size() == 0)
      throw runtime_error("invalid config file path");

   //remove first arg
   auto binaryPath = argv.front();
   vector<string> arg_minus_1;

   auto argvIter = argv.begin() + 1;
   while (argvIter != argv.end())
   {
      string argStr((*argvIter).getCharPtr(), (*argvIter).getSize());
      arg_minus_1.push_back(move(argStr));
      ++argvIter;
   }

   //break down string vector
   auto&& keyValMap = SettingsUtils::getKeyValsFromLines(arg_minus_1, '=');

   //complete config file path
   string configFile_path = MAINNET_DEFAULT_DATADIR;
   if (keyValMap.find("--testnet") != keyValMap.end())
      configFile_path = TESTNET_DEFAULT_DATADIR;
   else if (keyValMap.find("--regtest") != keyValMap.end())
      configFile_path = REGTEST_DEFAULT_DATADIR;

   auto datadir_iter = keyValMap.find("--datadir");
   if (datadir_iter != keyValMap.end() && datadir_iter->second.size() > 0)
      configFile_path = datadir_iter->second;

   DBUtils::appendPath(configFile_path, path);
   DBUtils::expandPath(configFile_path);

   //process config file
   Config::File cfile(configFile_path);
   if (cfile.keyvalMap_.size() == 0)
      return argv;

   //merge with argv
   for (auto& keyval : cfile.keyvalMap_)
   {
      //skip if argv already has this key
      stringstream argss;
      if (keyval.first.compare(0, 2, "--") != 0)
         argss << "--";
      argss << keyval.first;

      auto keyiter = keyValMap.find(argss.str());
      if (keyiter != keyValMap.end())
         continue;

      keyValMap.insert(keyval);
   }

   //convert back to string list format
   auto&& newArgs = SettingsUtils::keyValToArgv(keyValMap);

   //prepend the binary path and return
   vector<BinaryData> fleshedOutArgs;
   fleshedOutArgs.push_back(binaryPath);
   auto newArgsIter = newArgs.begin();
   while (newArgsIter != newArgs.end())
   {
      auto&& bdStr = BinaryData::fromString(*newArgsIter);
      fleshedOutArgs.push_back(move(bdStr));
      ++newArgsIter;
   }

   return fleshedOutArgs;
}

////////////////////////////////////////////////////////////////////////////////
//
// BDV_Error_Struct
//
////////////////////////////////////////////////////////////////////////////////
BinaryData BDV_Error_Struct::serialize(void) const
{
   BinaryWriter bw;
   bw.put_int32_t(errCode_);

   bw.put_var_int(errData_.getSize());
   bw.put_BinaryData(errData_);

   bw.put_var_int(errorStr_.size());
   bw.put_String(errorStr_);

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
void BDV_Error_Struct::deserialize(const BinaryData& data)
{
   BinaryRefReader brr(data);

   errCode_ = brr.get_int32_t();
   
   auto len = brr.get_var_int();
   errData_ = brr.get_BinaryData(len);

   len = brr.get_var_int();
   errorStr_ = brr.get_String(len);
}
