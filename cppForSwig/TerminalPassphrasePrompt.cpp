////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "TerminalPassphrasePrompt.h"

#if defined(__MINGW32__) || defined(_MSC_VER)
   #include <windows.h>
#else
   #include <termios.h>
   #include <unistd.h>
#endif

using namespace Armory::Wallets;

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData TerminalPassphrasePrompt::prompt(
   const std::set<EncryptionKeyId>& idSet)
{
   std::unique_lock<std::mutex> lock(mu_);

   //check ids
   if (idSet.empty()) {
      //empty ids means we need to prompt for a new passphrase
      std::cout << std::endl;
      std::cout << "Set password for " << verbose_ << std::endl;

      return promptNewPass();
   } else if (idSet.size() == 1) {
      auto iter = idSet.find(CHANGE_PASS_FLAG);
      if (iter != idSet.end()) {
         std::cout << "Changing password for " << verbose_ << std::endl;
         return promptNewPass();
      }
   }

   //we have ids, prompt the user for it
   return promptForPassphrase(idSet);
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData TerminalPassphrasePrompt::promptNewPass()
{
   while (true) {
      std::string pass1, pass2;
      std::cout << "Enter new password: ";

      setEcho(false);
      std::getline(std::cin, pass1);
      setEcho(true);
      std::cout << std::endl;

      if (std::cin.fail()) {
         std::cerr << "Can't read password...";
         std::exit(1);
      }

      std::cout << "Repeat new password: ";

      setEcho(false);
      std::getline(std::cin, pass2);
      setEcho(true);
      std::cout << std::endl;

      if (std::cin.fail()) {
         std::cerr << "Can't read password...";
         std::exit(1);
      }

      if (pass1 != pass2) {
         std::cout << "Password mismatch, try again!" << std::endl << std::endl;
         continue;
      } else if (pass1.empty()) {
         std::cout << "You have provided an empty passphrase." << std::endl;
         std::cout << "If you continue, this " << verbose_ << " will be unencrypted!" << std::endl;

         while (true) {
            std::string yn;
            std::cout << "Do you wish to continue (Y/n)? ";
            std::cin >> yn;
            if (std::cin.fail()) {
               std::cerr << "Can't read answer...";
               std::exit(1);
            }

            if (yn == "n") {
               std::cout << std::endl;
               break;
            } else if (yn == "Y") {
               std::cout << "The " << verbose_ << " will be unencrypted!" << std::endl;
               return {};
            }
         }

         continue;
      }
      return SecureBinaryData::fromString(pass1);
   }
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData TerminalPassphrasePrompt::promptForPassphrase(
   const std::set<EncryptionKeyId>& idSet)
{
   if (idSet.empty()) {
      throw std::runtime_error("invalid id count");
   }

   bool suppress = false;
   for (const auto& id : idSet) {
      auto iter = countMap_.find(id);
      if (iter == countMap_.end()) {
         iter = countMap_.emplace(id, 0).first;
      }

      if (iter->second > 0) {
         suppress = true;
      }

      if (++(iter->second) > 3) {
         std::cout << "3 failed attempts, aborting" << std::endl << std::endl;
         std::exit(2);
         return SecureBinaryData();
      }
   }

   if (!suppress) {
      std::cout << std::endl << "Encrypted " << verbose_ <<
         ", please input the password for either of these key(s): " << std::endl;
    
      unsigned idCount = 1;
      for (const auto& id : idSet) {
         std::cout << " ." << idCount++ << ": " << id.toHexStr() << std::endl;
      }
   }

   std::cout << " passhrase: ";

   std::string pass1;
   setEcho(false);
   std::cin >> pass1;
   setEcho(true);
   std::cout << std::endl;

   return SecureBinaryData::fromString(pass1);
}

////////////////////////////////////////////////////////////////////////////////
void TerminalPassphrasePrompt::setEcho(bool enable)
{
#ifdef _WIN32
   HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
   DWORD mode;
   GetConsoleMode(hStdin, &mode);

   if (!enable) {
      mode &= ~ENABLE_ECHO_INPUT;
   } else {
      mode |= ENABLE_ECHO_INPUT;
   }
   SetConsoleMode(hStdin, mode );
#else
   struct termios tty;
   tcgetattr(STDIN_FILENO, &tty);

   if (!enable) {
      tty.c_lflag &= ~ECHO;
   } else {
      tty.c_lflag |= ECHO;
   }
   tcsetattr(STDIN_FILENO, TCSANOW, &tty);
#endif
}

////////////////////////////////////////////////////////////////////////////////
PassphraseLambda TerminalPassphrasePrompt::getLambda(const std::string& verbose)
{
   auto ptr = new TerminalPassphrasePrompt(verbose);
   std::shared_ptr<TerminalPassphrasePrompt> smartPtr(ptr);

   auto passLbd = [smartPtr](const std::set<EncryptionKeyId>& idSet)->SecureBinaryData
   {
      return smartPtr->prompt(idSet);
   };
   return passLbd;
}