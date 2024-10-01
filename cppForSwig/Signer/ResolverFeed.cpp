////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ResolverFeed.h"
#include "../Wallets/Assets.h"

using namespace std;
using namespace Armory::Signing;
using namespace Armory::Assets;
using namespace Armory::Wallets;

////////////////////////////////////////////////////////////////////////////////
////
//// BIP32_PublicDerivedRoot
////
////////////////////////////////////////////////////////////////////////////////
BIP32_PublicDerivedRoot::BIP32_PublicDerivedRoot(
   const string& xpub, const vector<uint32_t>& path, uint32_t fingerprint) :
   xpub_(xpub), path_(path), seedFingerprint_(fingerprint)
{}

////////////////////////////////////////////////////////////////////////////////
bool BIP32_PublicDerivedRoot::isValid() const
{
   return (seedFingerprint_ != UINT32_MAX && !path_.empty() && !xpub_.empty());
}

////////////////////////////////////////////////////////////////////////////////
uint32_t BIP32_PublicDerivedRoot::getSeedFingerprint() const
{
   return seedFingerprint_;
}

////////////////////////////////////////////////////////////////////////////////
uint32_t BIP32_PublicDerivedRoot::getThisFingerprint() const
{
   if (thisFingerprint_ == UINT32_MAX)
   {
      BIP32_Node node;
      BinaryDataRef xpubRef;
      xpubRef.setRef(xpub_);
      node.initFromBase58(xpubRef);
      thisFingerprint_ = node.getThisFingerprint();
   }

   return thisFingerprint_;
}

////////////////////////////////////////////////////////////////////////////////
const vector<uint32_t>& BIP32_PublicDerivedRoot::getPath() const
{
   return path_;
}

////////////////////////////////////////////////////////////////////////////////
const string& BIP32_PublicDerivedRoot::getXPub(void) const
{
   return xpub_;
}

////////////////////////////////////////////////////////////////////////////////
////
//// BIP32_AssetPath
////
////////////////////////////////////////////////////////////////////////////////
BIP32_AssetPath::BIP32_AssetPath(
   const BinaryData& pubkey,
   const vector<uint32_t>& path, uint32_t fingerprint, 
   shared_ptr<BIP32_PublicDerivedRoot> rootPtr) :
   pubkey_(pubkey), path_(path), fingerprint_(fingerprint), root_(rootPtr)
{}

////////////////////////////////////////////////////////////////////////////////
bool BIP32_AssetPath::operator==(const BIP32_AssetPath& rhs) const
{
   return (fingerprint_ == rhs.fingerprint_ &&
      path_ == rhs.path_);
}

////////////////////////////////////////////////////////////////////////////////
bool BIP32_AssetPath::operator!=(const BIP32_AssetPath& rhs) const
{
   return !(*this == rhs);
}

////////////////////////////////////////////////////////////////////////////////
bool BIP32_AssetPath::isValid() const 
{ 
   return fingerprint_ != UINT32_MAX && !path_.empty(); 
}

////////////////////////////////////////////////////////////////////////////////
uint32_t BIP32_AssetPath::getSeedFingerprint() const
{
   if (hasRoot())
      return root_->getSeedFingerprint();
      
   return fingerprint_;
}

////////////////////////////////////////////////////////////////////////////////
uint32_t BIP32_AssetPath::getThisFingerprint() const
{
   return fingerprint_;
}

////////////////////////////////////////////////////////////////////////////////
vector<uint32_t> BIP32_AssetPath::getDerivationPathFromSeed() const
{
   vector<uint32_t> path;
   if (hasRoot())
      path = root_->getPath();

   for (auto& step : path_)
      path.emplace_back(step);

   return path;
}

////////////////////////////////////////////////////////////////////////////////
const vector<uint32_t>& BIP32_AssetPath::getPath() const
{
   return path_;
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& BIP32_AssetPath::getPublicKey() const
{
   return pubkey_;
}

////////////////////////////////////////////////////////////////////////////////
bool BIP32_AssetPath::hasRoot() const 
{
   return (root_ != nullptr && root_->isValid());
}

////////////////////////////////////////////////////////////////////////////////
void BIP32_AssetPath::setRoot(shared_ptr<BIP32_PublicDerivedRoot> ptr)
{
   root_ = ptr;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<BIP32_PublicDerivedRoot> BIP32_AssetPath::getRoot() const 
{
   if (!hasRoot())
      throw runtime_error("asset path has no root");
      
   return root_;
}

////////////////////////////////////////////////////////////////////////////////
void BIP32_AssetPath::toPSBT(BinaryWriter& bw) const
{
   bw.put_var_int((path_.size() + 1) * 4);
   bw.put_uint32_t(fingerprint_);
   for (auto& step : path_)
      bw.put_uint32_t(step);         
}

////////////////////////////////////////////////////////////////////////////////
BIP32_AssetPath BIP32_AssetPath::fromPSBT(
   const BinaryDataRef& key, const BinaryDataRef& val)
{
   auto pubKey = key.getSliceRef(1, key.getSize() - 1);

   BinaryRefReader valReader(val);
   auto fingerprint = valReader.get_uint32_t();
   vector<uint32_t> path;
   while (valReader.getSizeRemaining() > 0)
      path.emplace_back(valReader.get_uint32_t());

   return BIP32_AssetPath(pubKey, path, fingerprint, nullptr);
}

////////////////////////////////////////////////////////////////////////////////
////
//// ResolverFeed
////
////////////////////////////////////////////////////////////////////////////////
ResolverFeed::~ResolverFeed()
{}
