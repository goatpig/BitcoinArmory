////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <filesystem>

namespace Armory
{
   namespace Accounts
   {
      class AccountType;
   };

   namespace Wallets
   {
      namespace Progress
      {
         enum class StateEnum : int
         {
            CreateFile,
            InitFile,
            ReadFile,
            CreateAccount,
            ExtendChain
         };

         class State
         {
         private:
            const StateEnum se_;

         public:
            State(StateEnum);
            virtual ~State(void) = 0;

            const StateEnum& type(void) const;
         };

         using Func = std::function<void(std::unique_ptr<State>)>;

         ////////
         class CreateFile : public State
         {
         private:
            const std::filesystem::path path_;

         public:
            CreateFile(const std::filesystem::path&);
            const std::filesystem::path& path(void) const;
         };

         //
         class InitFile : public State
         {
         private:
            const std::string& masterId_;

         public:
            InitFile(const std::string&);
            const std::string& masterId(void) const;
         };

         //
         class ReadFile : public State
         {
         private:
            const std::string& masterId_;

         public:
            ReadFile(const std::string&);
            const std::string& masterId(void) const;
         };

         ////
         class CreateAccount : public State
         {
         private:
            const std::shared_ptr<Accounts::AccountType> accType_;

         public:
            CreateAccount(std::shared_ptr<Accounts::AccountType>);
            std::shared_ptr<Accounts::AccountType> accPtr(void) const;
         };

         //
         class ExtendChain : public State
         {
         private:
            const uint32_t lookup_;

         public:
            ExtendChain(uint32_t);
            uint32_t lookup(void) const;
         };

      }; //Progress
   }; //Wallets
}; //Armory
