////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2024, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <string>
#include <filesystem>
#include <iostream>
#include <sstream>

#include "btc/ecc.h"
#include "EncryptionUtils.h"
#include "AuthorizedPeers.h"
#include "TerminalPassphrasePrompt.h"

#define SERVER_FILE "server.peers"
#define CLIENT_FILE "client.peers"

using namespace Armory::Wallets;

std::vector<std::string> names;

////////////////////////////////////////////////////////////////////////////////
std::pair<std::string, std::string> getKeyValFromLine(
   const std::string& line, char delim)
{
   std::stringstream ss(line);
   std::pair<std::string, std::string> output;

   //key
   std::getline(ss, output.first, delim);

   //val
   if (ss.good()) {
      std::getline(ss, output.second);
   }
   return output;
}

////////////////////////////////////////////////////////////////////////////////
std::string stripQuotes(const std::string& input)
{
   size_t start = 0;
   size_t len = input.size();

   auto& first_char = input.c_str()[0];
   auto& last_char = input.c_str()[len - 1];

   if (first_char == '\"' || first_char == '\'') {
      start = 1;
      --len;
   }

   if (last_char == '\"' || last_char == '\'') {
      --len;
   }

   return input.substr(start, len);
}

////////////////////////////////////////////////////////////////////////////////
std::map<std::string, std::string> parseArgs(int argc, char* argv[])
{
   std::map<std::string, std::string> args;
   for (int i = 1; i < argc; i++) {
      //check prefix
      if (strlen(argv[i]) < 2) {
         std::stringstream ss;
         ss << "argument #" << i << " is too short";
         throw std::runtime_error(ss.str());
      }

      std::string prefix(argv[i], 2);
      if (prefix != "--") {
         names.push_back(argv[i]);
      }

      //string prefix and tokenize
      std::string line(argv[i] + 2);
      auto argkeyval = getKeyValFromLine(line, '=');
      args.emplace(argkeyval.first, stripQuotes(argkeyval.second));
   }

   return args;
}

////////////////////////////////////////////////////////////////////////////////
int processArgs(std::map<std::string, std::string> args)
{
   //look for datadir
   std::string datadir("./");
   auto iter = args.find("datadir");
   if (iter != args.end()) {
      datadir = iter->second;
   }

   //server or client?
   std::string filename;
   iter = args.find("server");
   if (iter != args.end()) {
      filename = SERVER_FILE;
   }

   iter = args.find("client");
   if (iter != args.end()) {
      if (filename.empty()) {
         throw std::runtime_error("client/server setting conflict");
      }
      filename = CLIENT_FILE;
   }

   if (filename.size() == 0) {
      throw std::runtime_error("missing client or server argument!");
   }

   //construct full path
   auto fullpath = std::filesystem::path(datadir) / filename;

   //is this a passphrase change operation?
   iter = args.find("change-pass");
   if (iter != args.end()) {
      AuthorizedPeers::changeControlPassphrase(fullpath.string());
      exit(0);
   }

   bool noPass = false;
   iter = args.find("no-pass");
   if (iter != args.end()) {
      noPass = true;
   }

   if (FileUtils::fileExists(fullpath, 6)) {
      std::cout << "Loading peers db from " << fullpath << std::endl;
   } else {
      std::cout << "Missing peers db, creating a fresh one now." << std::endl;
   }

   //passphrase lbd
   PassphraseLambda passLbd;
   if (!noPass) {
      passLbd = TerminalPassphrasePrompt::getLambda("peers db");
   } else {
      passLbd = [](const std::set<EncryptionKeyId>&)->SecureBinaryData
      { return SecureBinaryData(); };
   }

   AuthorizedPeers authPeers(datadir, filename, passLbd);

   /*mutually exclusive args from here on*/

   //show my own public key
   iter = args.find("show-my-key");
   if (iter != args.end()) {
      auto& ownkey = authPeers.getOwnPublicKey();
      BinaryDataRef bdr(ownkey.pubkey, 33);
      std::cout << "  displaying own public key (hex): " << bdr.toHexStr() << std::endl;
      return 0;
   }

   //show all keys
   iter = args.find("show-keys");
   if (iter != args.end()) {
      std::map<BinaryDataRef, std::set<std::string>> keyToNames;
      auto& nameMap = authPeers.getPeerNameMap();
      for (auto& namePair : nameMap) {
         if (namePair.first == "own") {
            continue;
         }

         BinaryDataRef keyBdr(namePair.second.pubkey, 33);
         auto keyIter = keyToNames.find(keyBdr);
         if (keyIter == keyToNames.end()) {
            keyIter = keyToNames.emplace(keyBdr, std::set<std::string>{}).first;
         }

         auto& nameSet = keyIter->second;
         nameSet.insert(namePair.first);
      }

      //intro
      std::cout << " displaying all keys in " << filename << ":" << std::endl;

      //output keys
      unsigned i = 1;
      for (auto& nameSet : keyToNames) {
         std::stringstream ss;
         ss << "  " << i << ". " << nameSet.first.toHexStr() << std::endl;
         ss << "   ";
         auto nameIter = nameSet.second.begin();
         while (true) {
            ss << "\"" << *nameIter++ << "\"";
            if (nameIter == nameSet.second.end())
               break;
            ss << ", ";
         }

         ss << std::endl;
         std::cout << ss.str();
         ++i;
      }

      return 0;
   }

   //add key
   iter = args.find("add-key");
   if(iter != args.end()) {
      if (names.empty()) {
         throw std::runtime_error("malformed add-key argument");
      }

      BinaryData bd_key = READHEX(names[0]);
      if (bd_key.getSize() != 33 && bd_key.getSize() != 65) {
         throw std::runtime_error("invalid public key size");
      }

      if (!CryptoECDSA().VerifyPublicKeyValid(bd_key)) {
         throw std::runtime_error("invalid public key");
      }

      SecureBinaryData key_compressed = bd_key;
      if (bd_key.getSize() == 65) {
         key_compressed = CryptoECDSA().CompressPoint(bd_key);
      }

      std::vector<std::string> keyNames;
      keyNames.insert(keyNames.end(), names.begin() + 1, names.end());
      authPeers.addPeer(key_compressed, keyNames);

      return 0;
   }

   std::cout << "no known command, aborting" << std::endl;
   return -1;
}

int main(int argc, char* argv[])
{
   CryptoECDSA::setupContext();
   Armory::Config::parseArgs({}, Armory::Config::ProcessType::KeyManager);

   std::map<std::string, std::string> args;
   try {
      args = parseArgs(argc, argv);
      return processArgs(args);
   } catch (const std::exception& e) {
      std::cout << "failed to parse arguments with error: " << std::endl;
      std::cout << "   " << e.what() << std::endl;
   }

   std::cout << "no valid argument to process, exiting" << std::endl;
   return -1;
}