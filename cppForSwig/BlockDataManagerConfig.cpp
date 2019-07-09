////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-19, goatpig                                            //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BlockDataManagerConfig.h"
#include "BtcUtils.h"
#include "DBUtils.h"
#include "DbHeader.h"
#include "EncryptionUtils.h"
#include "JSON_codec.h"
#include "SocketObject.h"
#include "BIP150_151.h"
#include "btc/ecc.h"
#ifndef _WIN32
#include "sys/stat.h"
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////
//
// NodeStatusStruct
//
////////////////////////////////////////////////////////////////////////////////
ARMORY_DB_TYPE BlockDataManagerConfig::armoryDbType_ = ARMORY_DB_FULL;
SOCKET_SERVICE BlockDataManagerConfig::service_ = SERVICE_WEBSOCKET;
ARMORY_OPERATION_MODE BlockDataManagerConfig::operationMode_ = OPERATION_REGULAR;

////////////////////////////////////////////////////////////////////////////////
const string BlockDataManagerConfig::dbDirExtention_ = "/databases";
#if defined(_WIN32)
const string BlockDataManagerConfig::defaultDataDir_ = 
   "~/Armory";
const string BlockDataManagerConfig::defaultBlkFileLocation_ = 
   "~/Bitcoin/blocks";

const string BlockDataManagerConfig::defaultTestnetDataDir_ = 
   "~/Armory/testnet3";
const string BlockDataManagerConfig::defaultTestnetBlkFileLocation_ = 
   "~/Bitcoin/testnet3/blocks";

const string BlockDataManagerConfig::defaultRegtestDataDir_ = 
   "~/Armory/regtest";
const string BlockDataManagerConfig::defaultRegtestBlkFileLocation_ = 
   "~/Bitcoin/regtest/blocks";
#elif defined(__APPLE__)
const string BlockDataManagerConfig::defaultDataDir_ = 
   "~/Library/Application Support/Armory";
const string BlockDataManagerConfig::defaultBlkFileLocation_ = 
   "~/Library/Application Support/Bitcoin/blocks";

const string BlockDataManagerConfig::defaultTestnetDataDir_ = 
   "~/Library/Application Support/Armory/testnet3";
const string BlockDataManagerConfig::defaultTestnetBlkFileLocation_ =   
   "~/Library/Application Support/Bitcoin/testnet3/blocks";

const string BlockDataManagerConfig::defaultRegtestDataDir_ = 
   "~/Library/Application Support/Armory/regtest";
const string BlockDataManagerConfig::defaultRegtestBlkFileLocation_ = 
   "~/Library/Application Support/Bitcoin/regtest/blocks";
#else
const string BlockDataManagerConfig::defaultDataDir_ = 
   "~/.armory";
const string BlockDataManagerConfig::defaultBlkFileLocation_ = 
   "~/.bitcoin/blocks";

const string BlockDataManagerConfig::defaultTestnetDataDir_ = 
   "~/.armory/testnet3";
const string BlockDataManagerConfig::defaultTestnetBlkFileLocation_ = 
   "~/.bitcoin/testnet3/blocks";

const string BlockDataManagerConfig::defaultRegtestDataDir_ = 
   "~/.armory/regtest";
const string BlockDataManagerConfig::defaultRegtestBlkFileLocation_ = 
   "~/.bitcoin/regtest/blocks";
#endif

string BlockDataManagerConfig::dataDir_ = "";
bool BlockDataManagerConfig::ephemeralPeers_ = false;

////////////////////////////////////////////////////////////////////////////////
BlockDataManagerConfig::BlockDataManagerConfig() :
   cookie_(CryptoPRNG::generateRandom(32).toHexStr())
{
   selectNetwork(NETWORK_MODE_MAINNET);
}

////////////////////////////////////////////////////////////////////////////////
string BlockDataManagerConfig::portToString(unsigned port)
{
   stringstream ss;
   ss << port;
   return ss.str();
}

////////////////////////////////////////////////////////////////////////////////
bool BlockDataManagerConfig::fileExists(const string& path, int mode)
{
#ifdef _WIN32
   return _access(path.c_str(), mode) == 0;
#else
   auto nixmode = F_OK;
   if (mode & 2)
      nixmode |= R_OK;
   if (mode & 4)
      nixmode |= W_OK;
   return access(path.c_str(), nixmode) == 0;
#endif
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataManagerConfig::selectNetwork(NETWORK_MODE mode)
{
   NetworkConfig::selectNetwork(mode);

   switch (mode)
   {
   case NETWORK_MODE_MAINNET:
   {
      rpcPort_ = portToString(RPC_PORT_MAINNET);
      
      if (!customListenPort_)
         listenPort_ = portToString(LISTEN_PORT_MAINNET);
      
      if(!customBtcPort_)
         btcPort_ = portToString(NODE_PORT_MAINNET);

      break;
   }

   case NETWORK_MODE_TESTNET:
   {
      rpcPort_ = portToString(RPC_PORT_TESTNET);

      if (!customListenPort_)
         listenPort_ = portToString(LISTEN_PORT_TESTNET);

      if (!customBtcPort_)
         btcPort_ = portToString(NODE_PORT_TESTNET);

      break;
   }

   case NETWORK_MODE_REGTEST:
   {
      rpcPort_ = portToString(RPC_PORT_REGTEST);

      if (!customListenPort_)
         listenPort_ = portToString(LISTEN_PORT_REGTEST);

      if (!customBtcPort_)
         btcPort_ = portToString(NODE_PORT_REGTEST);

      break;
   }

   default:
      LOGERR << "unexpected network mode!";
      throw runtime_error("unxecpted network mode");
   }
}

////////////////////////////////////////////////////////////////////////////////
string BlockDataManagerConfig::stripQuotes(const string& input)
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
void BlockDataManagerConfig::printHelp(void)
{
   //TODO: spit out arg list with description
   exit(0);
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataManagerConfig::parseArgs(int argc, char* argv[])
{
   /***
   --testnet: run db against testnet bitcoin network

   --regtest: run db against regression test network

   --rescan: delete all processed history data and rescan blockchain from the
   first block

   --rebuild: delete all DB data and build and scan from scratch

   --rescanSSH: delete balance and txcount data and rescan it. Much faster than
   rescan or rebuild.

   --datadir: path to the operation folder

   --dbdir: path to folder containing the database files. If empty, a new db
   will be created there

   --satoshi-datadir: path to blockchain data folder (blkXXXXX.dat files)

   --ram-usage: defines the ram use during scan operations. 1 level averages
   128MB of ram (without accounting the base amount, ~400MB). Defaults at 50.
   Can't be lower than 1. Can be changed in between processes

   --thread-count: defines how many processing threads can be used during db
   builds and scans. Defaults to maximum available CPU threads. Can't be
   lower than 1. Can be changed in between processes

   --zcthread-count: defines the maximum number on threads the zc parser can
   create for processing incoming transcations from the network node

   --db-type: sets the db type:
   DB_BARE: tracks wallet history only. Smallest DB.
   DB_FULL: tracks wallet history and resolves all relevant tx hashes.
   ~750MB DB at the time of 0.95 release. Default DB type.
   DB_SUPER: tracks all blockchain history. XXL DB (100GB+).
   Not implemented yet

   db type cannot be changed in between processes. Once a db has been built
   with a certain type, it will always function according to that type.
   Specifying another type will do nothing. Build a new db to change type.

   --cookie: create a cookie file holding a random authentication key to allow
   local clients to make use of elevated commands, like shutdown. Client and 
   server will make use of ephemeral peer keys, ignoring the on disk peer wallet

   --listen-port: sets the DB listening port.

   --clear-mempool: delete all zero confirmation transactions from the DB.

   --satoshirpc-port: set node rpc port

   --satoshi-port: set Bitcoin node port

   --public: BIP150 auth will allow for anonymous requesters. While only clients
   can be anon (servers/responders are always auth'ed), both sides need to enable
   public channels for the handshake to succeed

   --cli-bip150-key: BIP150 identity key for the client. Used for 2-way
   authentication.

   ***/

   try
   {
      //parse cli args
      map<string, string> args;
      for (int i = 1; i < argc; i++)
      {
         //check prefix
         if (strlen(argv[i]) < 2)
            throw DbErrorMsg("invalid CLI arg");

         string prefix(argv[i], 2);
         if (prefix != "--")
            throw DbErrorMsg("invalid CLI arg");

         //string prefix and tokenize
         string line(argv[i] + 2);
         auto&& argkeyval = getKeyValFromLine(line, '=');
         args.insert(make_pair(
            argkeyval.first, stripQuotes(argkeyval.second)));
      }

      processArgs(args, true);

      //figure out datadir
      auto argIter = args.find("datadir");
      if (argIter != args.end())
      {
         dataDir_ = argIter->second;
         args.erase(argIter);
      }
      else
      {
         switch (NetworkConfig::getMode())
         {
         case NETWORK_MODE_MAINNET:
         {
            dataDir_ = defaultDataDir_;
            break;
         }

         case NETWORK_MODE_TESTNET:
         {
            dataDir_ = defaultTestnetDataDir_;
            break;
         }

         case NETWORK_MODE_REGTEST:
         {
            dataDir_ = defaultRegtestDataDir_;
            break;
         }

         default:
            LOGERR << "unexpected network mode";
            throw runtime_error("unexpected network mode");
         }
      }

      DBUtils::expandPath(dataDir_);

      //get config file
      auto configPath = dataDir_;
      DBUtils::appendPath(configPath, "armorydb.conf");

      if (fileExists(configPath, 2))
      {
         ConfigFile cf(configPath);
         auto mapIter = cf.keyvalMap_.find("datadir");
         if (mapIter != cf.keyvalMap_.end())
            throw DbErrorMsg("datadir is illegal in .conf file");

         processArgs(cf.keyvalMap_, false);
      }

      processArgs(args, false);

      //figure out defaults
      bool autoDbDir = false;
      if (dbDir_.size() == 0)
      {
         dbDir_ = dataDir_;
         DBUtils::appendPath(dbDir_, dbDirExtention_);
         autoDbDir = true;
      }

      if (blkFileLocation_.size() == 0)
      {
         switch (NetworkConfig::getMode())
         {
         case NETWORK_MODE_MAINNET:
         {
            blkFileLocation_ = defaultBlkFileLocation_;
            break;
         }
         
         default:
            blkFileLocation_ = defaultTestnetBlkFileLocation_;
         }
      }

      //expand paths if necessary
      DBUtils::expandPath(dbDir_);
      DBUtils::expandPath(blkFileLocation_);

      if (blkFileLocation_.size() < 6 ||
         blkFileLocation_.substr(blkFileLocation_.length() - 6, 6) != "blocks")
      {
         DBUtils::appendPath(blkFileLocation_, "blocks");
      }

      logFilePath_ = dataDir_;
      DBUtils::appendPath(logFilePath_, "dbLog.txt");

      //test all paths
      auto testPath = [](const string& path, int mode)
      {
         if (!fileExists(path, mode))
         {
            stringstream ss;
            ss << path << " is not a valid path";

            cout << ss.str() << endl;
            throw DbErrorMsg(ss.str());
         }
      };

      testPath(dataDir_, 6);

      //create dbdir if was set automatically
      if (autoDbDir)
      {
         try
         {
            testPath(dbDir_, 0);
         }
         catch (DbErrorMsg&)
         {
#ifdef _WIN32
            CreateDirectory(dbDir_.c_str(), NULL);
#else
            mkdir(dbDir_.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
         }
      }

      //now for the regular test, let it throw if it fails
      testPath(dbDir_, 6);

      testPath(blkFileLocation_, 2);

      //listen port
      if (useCookie_ && !customListenPort_)
      {
         //no custom listen port was provided and the db was spawned with a 
         //cookie file, listen port will be randomized
         srand(time(0));
         while (1)
         {
            auto port = rand() % 15000 + 49150;
            stringstream portss;
            portss << port;

            if (!testConnection("127.0.0.1", portss.str()))
            {
               listenPort_ = portss.str();
               break;
            }
         }
      }
   }
   catch (...)
   {
      exceptionPtr_ = current_exception();
   }
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataManagerConfig::processArgs(const map<string, string>& args, 
   bool onlyDetectNetwork)
{
   // Client key - Used to enable 2-way BIP 150/151 handshakes.
   auto iter = args.find("cli-bip150-key");
   if (iter != args.end())
   {
      auto bip150PubKeyData = stripQuotes(iter->second);
      string cliBIP150PubKeyStr;

      // Verify that the incoming key is valid and compressed. Format is
      // IP:Port_CompressedPublicKey
      stringstream keyData(bip150PubKeyData);
      getline(keyData, cliIP_, '_');
      getline(keyData, cliBIP150PubKeyStr, '_');
      cliBIP150PubKey_ = READHEX(cliBIP150PubKeyStr);

      if(!btc_ecc_verify_pubkey(cliBIP150PubKey_.getPtr(), true))
      {
         LOGERR << "Incoming client BIP 150 key is invalid";
         cliBIP150PubKey_ = BinaryData();
      }
   }

   //server networking
   iter = args.find("listen-port");
   if (iter != args.end())
   {
      listenPort_ = stripQuotes(iter->second);
      int portInt = 0;
      stringstream portSS(listenPort_);
      portSS >> portInt;

      if (portInt < 1 || portInt > 65535)
      {
         cout << "Invalid listen port, falling back to default" << endl;
         listenPort_ = "";
      }
      else
      {
         customListenPort_ = true;
      }
   }

   iter = args.find("satoshi-port");
   if (iter != args.end())
   {
      btcPort_ = stripQuotes(iter->second);
      customBtcPort_ = true;
   }

   //network type
   iter = args.find("testnet");
   if (iter != args.end())
   {
      selectNetwork(NETWORK_MODE_TESTNET);
   }
   else
   {
      iter = args.find("regtest");
      if (iter != args.end())
      {
         selectNetwork(NETWORK_MODE_REGTEST);
      }
      else
      {
         selectNetwork(NETWORK_MODE_MAINNET);
      }
   }

   //rpc port
   iter = args.find("satoshirpc-port");
   if (iter != args.end())
   {
      auto value = stripQuotes(iter->second);
      int portInt = 0;
      stringstream portSS(value);
      portSS >> portInt;

      if (portInt < 1 || portInt > 65535)
      {
         cout << "Invalid satoshi rpc port, falling back to default" << endl;
      }
      else
      {
         rpcPort_ = value;
      }
   }

   if (onlyDetectNetwork)
      return;

   //db init options
   iter = args.find("rescanSSH");
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

   iter = args.find("clear_mempool");
   if (iter != args.end())
      clearMempool_ = true;

   //db type
   iter = args.find("db-type");
   if (iter != args.end())
   {
      if (iter->second == "DB_BARE")
         armoryDbType_ = ARMORY_DB_BARE;
      else if (iter->second == "DB_FULL")
         armoryDbType_ = ARMORY_DB_FULL;
      else if (iter->second == "DB_SUPER")
         armoryDbType_ = ARMORY_DB_SUPER;
      else
      {
         cout << "Error: unexpected DB type: " << iter->second << endl;
         printHelp();
      }
   }

   //paths
   iter = args.find("datadir");
   if (iter != args.end())
      dataDir_ = stripQuotes(iter->second);

   iter = args.find("dbdir");
   if (iter != args.end())
      dbDir_ = stripQuotes(iter->second);

   iter = args.find("satoshi-datadir");
   if (iter != args.end())
      blkFileLocation_ = stripQuotes(iter->second);

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

   //cookie
   iter = args.find("cookie");
   if (iter != args.end())
   {
      useCookie_ = true;
      ephemeralPeers_ = true;
   }

   //public
   iter = args.find("public");
   if (iter != args.end())
   {
      startupBIP150CTX(4, true);
   }
}

////////////////////////////////////////////////////////////////////////////////
vector<string> BlockDataManagerConfig::getLines(const string& path)
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
map<string, string> BlockDataManagerConfig::getKeyValsFromLines(
   const vector<string>& lines, char delim)
{
   map<string, string> output;
   for (auto& line : lines)
      output.insert(move(getKeyValFromLine(line, delim)));

   return output;
}

////////////////////////////////////////////////////////////////////////////////
pair<string, string> BlockDataManagerConfig::getKeyValFromLine(
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
vector<string> BlockDataManagerConfig::keyValToArgv(
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
// Write an authentication cookie that allows local clients to make use of
// elevated commands. Cookie will also include the ArmoryDB listening port.
void BlockDataManagerConfig::createAuthCookie() const
{
   //cookie file
   if (!useCookie_)
      return;

   auto cookiePath = dataDir_;
   DBUtils::appendPath(cookiePath, ".cookie_");
   fstream fs(cookiePath, ios_base::out | ios_base::trunc);
   fs << cookie_ << endl;
   fs << listenPort_;
}

////////////////////////////////////////////////////////////////////////////////
bool BlockDataManagerConfig::testConnection(
   const string& ip, const string& port)
{
   SimpleSocket testSock(ip, port);
   return testSock.testConnection();
}

////////////////////////////////////////////////////////////////////////////////
string BlockDataManagerConfig::hasLocalDB(
   const string& datadir, const string& port)
{
   //check db on provided port
   if (testConnection("127.0.0.1", port))
      return port;

   //check db on default port
   if (testConnection("127.0.0.1", portToString(LISTEN_PORT_MAINNET)))
      return portToString(LISTEN_PORT_MAINNET);

   //check for cookie file
   auto&& cookie_port = getPortFromCookie(datadir);
   if (cookie_port.size() == 0)
      return string();

   if (testConnection("127.0.0.1", cookie_port))
      return cookie_port;

   return string();
}

////////////////////////////////////////////////////////////////////////////////
string BlockDataManagerConfig::getPortFromCookie(const string& datadir)
{
   //check for cookie file
   string cookie_path = datadir;
   DBUtils::appendPath(cookie_path, ".cookie_");
   auto&& lines = getLines(cookie_path);
   if (lines.size() != 2)
      return string();

   return lines[1];
}

////////////////////////////////////////////////////////////////////////////////
string BlockDataManagerConfig::getCookie(const string& datadir)
{
   string cookie_path = datadir;
   DBUtils::appendPath(cookie_path, ".cookie_");
   auto&& lines = getLines(cookie_path);
   if (lines.size() != 2)
      return string();

   return lines[0];
}

////////////////////////////////////////////////////////////////////////////////
string BlockDataManagerConfig::getDbModeStr()
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
//
// ConfigFile
//
////////////////////////////////////////////////////////////////////////////////
ConfigFile::ConfigFile(const string& path)
{
   auto&& lines = BlockDataManagerConfig::getLines(path);

   for (auto& line : lines)
   {
      auto&& keyval = BlockDataManagerConfig::getKeyValFromLine(line, '=');

      if (keyval.first.size() == 0)
         continue;

      if (keyval.first.compare(0, 1, "#") == 0)
         continue;

      keyvalMap_.insert(make_pair(
         keyval.first, BlockDataManagerConfig::stripQuotes(keyval.second)));
   }
}

////////////////////////////////////////////////////////////////////////////////
vector<string> ConfigFile::fleshOutArgs(
   const string& path, const vector<string>& argv)
{
   //sanity check
   if (path.size() == 0)
      throw runtime_error("invalid config file path");

   //remove first arg
   auto stringPath = argv.front();
   vector<string> arg_minus_1;
   arg_minus_1.insert(arg_minus_1.end(), argv.begin() + 1, argv.end());

   //break down string vector
   auto&& keyValMap = BlockDataManagerConfig::getKeyValsFromLines(arg_minus_1, '=');

   //complete config file path
   string configFile_path = BlockDataManagerConfig::defaultDataDir_;
   if (keyValMap.find("--testnet") != keyValMap.end())
      configFile_path = BlockDataManagerConfig::defaultTestnetDataDir_;
   else if (keyValMap.find("--regtest") != keyValMap.end())
      configFile_path = BlockDataManagerConfig::defaultRegtestDataDir_;

   auto datadir_iter = keyValMap.find("--datadir");
   if (datadir_iter != keyValMap.end() && datadir_iter->second.size() > 0)
      configFile_path = datadir_iter->second;

   DBUtils::appendPath(configFile_path, path);
   DBUtils::expandPath(configFile_path);

   //process config file
   ConfigFile cfile(configFile_path);
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
   auto&& newArgs = BlockDataManagerConfig::keyValToArgv(keyValMap);

   //prepend the binary path and return
   vector<string> fleshedOutArgs;
   fleshedOutArgs.push_back(stringPath);
   fleshedOutArgs.insert(fleshedOutArgs.end(),
      arg_minus_1.begin(), arg_minus_1.end());

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
   bw.put_uint8_t(errType_);

   BinaryDataRef errbdr((const uint8_t*)errorStr_.c_str(), errorStr_.size());
   bw.put_var_int(errorStr_.size());
   bw.put_BinaryData(errbdr);

   BinaryDataRef extbdr((const uint8_t*)extraMsg_.c_str(), extraMsg_.size());
   bw.put_var_int(extraMsg_.size());
   bw.put_BinaryData(extbdr);

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
void BDV_Error_Struct::deserialize(const BinaryData& data)
{
   BinaryRefReader brr(data);

   errType_ = BDV_ErrorType(brr.get_uint8_t());
   
   auto len = brr.get_var_int();
   errorStr_ = move(string((char*)brr.get_BinaryDataRef(len).getPtr(), len));

   len = brr.get_var_int();
   extraMsg_ = move(string((char*)brr.get_BinaryDataRef(len).getPtr(), len));
}

////////////////////////////////////////////////////////////////////////////////
BDV_Error_Struct BDV_Error_Struct::cast_to_BDVErrorStruct(void* ptr)
{
   auto obj = (BDV_Error_Struct*)ptr;
   return *obj;
}
