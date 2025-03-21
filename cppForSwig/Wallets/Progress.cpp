////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Progress.h"
#include "Accounts/AccountTypes.h"

using namespace Armory;
using namespace Armory::Wallets::Progress;

////////////////////////////////////////////////////////////////////////////////
// State
////////////////////////////////////////////////////////////////////////////////
State::State(StateEnum se) :
   se_(se)
{}

State::~State()
{}

const StateEnum& State::type() const
{
   return se_;
}

////////////////////////////////////////////////////////////////////////////////
// State Children
////////////////////////////////////////////////////////////////////////////////
CreateFile::CreateFile(const std::filesystem::path& path) :
   State(StateEnum::CreateFile), path_(path)
{}

const std::filesystem::path& CreateFile::path() const
{
   return path_;
}

////////////////////////////////////////////////////////////////////////////////
InitFile::InitFile(const std::string& masterId) :
   State(StateEnum::InitFile), masterId_(masterId)
{}

const std::string& InitFile::masterId() const
{
   return masterId_;
}

////////////////////////////////////////////////////////////////////////////////
ReadFile::ReadFile(const std::string& masterId) :
   State(StateEnum::ReadFile), masterId_(masterId)
{}

const std::string& ReadFile::masterId() const
{
   return masterId_;
}

////////////////////////////////////////////////////////////////////////////////
CreateAccount::CreateAccount(std::shared_ptr<Accounts::AccountType> accType) :
   State(StateEnum::CreateAccount), accType_(accType)
{}

std::shared_ptr<Accounts::AccountType> CreateAccount::accPtr() const
{
   return accType_;
}

////////////////////////////////////////////////////////////////////////////////
ExtendChain::ExtendChain(uint32_t lookup) :
   State(StateEnum::ExtendChain), lookup_(lookup)
{}

uint32_t ExtendChain::lookup() const
{
   return lookup_;
}
