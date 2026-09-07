////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "KDF.h"
#include "Utils/Cryptography.h"

#define KDF_ROMIX_VERSION  0x00000001
#define KDF_ROMIX_PREFIX   0xC100

using namespace Armory::Wallets;
using namespace Armory::Wallets::Encryption;
using namespace std::chrono_literals;
using namespace std::string_view_literals;

namespace {
   constexpr std::string_view hashFunctionName = "sha512"sv;
   constexpr uint32_t hashOutputBytes = 64;
   constexpr uint32_t kdfOutputBytes = 32;
}

////////////////////////////////////////////////////////////////////////////////
//// KdfRomix
////////////////////////////////////////////////////////////////////////////////
KdfRomix::KdfRomix(void) :
   numIterations_(0)
{}

////////
KdfRomix::KdfRomix(uint32_t memReqts, uint32_t numIter,
   const SecureBinaryData& salt)
{
   usePrecomputedKdfParams(memReqts, numIter, salt);
}

////////////////////////////////////////////////////////////////////////////////
uint32_t KdfRomix::getMemoryReqtBytes() const
{
   return memoryReqtBytes_;
}

uint32_t KdfRomix::getNumIterations() const
{
   return numIterations_;
}

const SecureBinaryData& KdfRomix::getSalt() const
{
   return salt_;
}

////////////////////////////////////////////////////////////////////////////////
void KdfRomix::computeKdfParams(
   std::chrono::milliseconds targetCompute,
   uint32_t minMemReqtsB, bool verbose)
{
   // Create a random salt, even though this is probably unnecessary;
   // the variation in numIter and memReqts is probably effective enough
   salt_ = Cryptography::PRNG::generateRandomStrong(32);

   //12 bytes test key
   auto testKey = SecureBinaryData::fromString("- Test Key -");

   // Start the search for a memory value at 1kB or higher
   memoryReqtBytes_ = std::max(minMemReqtsB, uint32_t(DEFAULT_KDF_START_MEMORY_B));
   memoryReqtBytes_ = std::min(memoryReqtBytes_, uint32_t(KDF_MAX_MEMORY_B));

   std::chrono::milliseconds approx{0};
   while (true) {
      sequenceCount_ = memoryReqtBytes_ / hashOutputBytes;
      auto start = std::chrono::system_clock::now();
      testKey = DeriveKey_OneIter(testKey);
      auto end = std::chrono::system_clock::now();

      if ((end-start) >= targetCompute / 4 || memoryReqtBytes_ >= KDF_MAX_MEMORY_B) {
         break;
      }
      memoryReqtBytes_ *= 2;
   }

   // Recompute here, in case we didn't enter the search above
   sequenceCount_ = memoryReqtBytes_ / hashOutputBytes;

   // Depending on the search above (or if a low max memory was chosen,
   // we may need to do multiple iterations to achieve the desired compute
   // time on this system.
   std::chrono::milliseconds allIters{0};
   uint32_t numTest = 1;
   while (true) {
      auto _testKey = testKey;
      auto start = std::chrono::system_clock::now();
      for (uint32_t i = 0; i < numTest; i++) {
         _testKey = DeriveKey_OneIter(_testKey);
      }
      auto end = std::chrono::system_clock::now();
      allIters = std::chrono::duration_cast<std::chrono::milliseconds>(end-start);

      if (allIters >= 100ms) {
         break;
      }
      numTest *= 2;
   }

   uint64_t perIterMSec = allIters.count() / numTest;
   numIterations_ = (uint32_t)(targetCompute.count() / (perIterMSec + 1));
   numIterations_ = (numIterations_ < 1 ? 1 : numIterations_ + 1);
   if (verbose) {
      std::cout << "System speed test results    : " << std::endl;
      std::cout << "   Total test of the KDF took: " << allIters.count() << " ms" << std::endl;
      std::cout << "                   to execute: " << numTest << " iterations" << std::endl;
      std::cout << "   Target computation time is: " << targetCompute.count() << " ms" << std::endl;
      std::cout << "    Lookup tabke mem usage is: " << memoryReqtBytes_ << " bytes" << std::endl;
      std::cout << "   Setting numIterations to  : " << numIterations_ << std::endl << std::endl;
   }
}

////////////////////////////////////////////////////////////////////////////////
void KdfRomix::usePrecomputedKdfParams(uint32_t memReqts, uint32_t numIter,
   const SecureBinaryData& salt)
{
   memoryReqtBytes_ = memReqts;
   sequenceCount_ = memoryReqtBytes_ / hashOutputBytes;
   numIterations_ = numIter;
   salt_ = salt;
}

////////////////////////////////////////////////////////////////////////////////
void KdfRomix::printKdfParams(void)
{
   // SHA512 computes 64-byte outputs
   std::cout << "KDF Parameters:" << std::endl;
   std::cout << "   HashFunction : " << hashFunctionName << std::endl;
   std::cout << "   Memory/thread: " << memoryReqtBytes_ << " bytes" << std::endl;
   std::cout << "   SequenceCount: " << sequenceCount_ << std::endl;
   std::cout << "   NumIterations: " << numIterations_ << std::endl;
   std::cout << "   Salt         : " << salt_.toHexStr() << std::endl;
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData KdfRomix::DeriveKey_OneIter(SecureBinaryData const & password)
{
   // Concatenate the salt/IV to the password
   SecureBinaryData saltedPassword = password + salt_;

   // Prepare the lookup table
   lookupTable_.resize(memoryReqtBytes_);
   lookupTable_.fill(0);
   uint32_t const HSZ = hashOutputBytes;
   uint8_t* frontOfLUT = lookupTable_.getPtr();
   uint8_t* nextRead = NULL;
   uint8_t* nextWrite = NULL;

   // First hash to seed the lookup table, input is variable length anyway
   Cryptography::Hash::getSha512(saltedPassword.getRef(), frontOfLUT);

   // Compute <sequenceCount_> consecutive hashes of the passphrase
   // Every iteration is stored in the next 64-bytes in the Lookup table
   for (uint32_t nByte = 0; nByte < memoryReqtBytes_ - HSZ; nByte += HSZ) {
      // Compute hash of slot i, put result in slot i+1
      nextRead = frontOfLUT + nByte;
      nextWrite = nextRead + hashOutputBytes;
      BinaryDataRef bdr_next(nextRead, hashOutputBytes);
      Cryptography::Hash::getSha512(bdr_next, nextWrite);
   }

   // LookupTable should be complete, now start lookup sequence.
   // Start with the last hash from the previous step
   SecureBinaryData X(frontOfLUT + memoryReqtBytes_ - HSZ, HSZ);
   SecureBinaryData Y(HSZ);

   // We "integerize" a hash value by taking the last 4 bytes of
   // as a uint32_t, and take modulo sequenceCount
   uint64_t* X64ptr = (uint64_t*)(X.getPtr());
   uint64_t* Y64ptr = (uint64_t*)(Y.getPtr());
   uint64_t* V64ptr = NULL;
   uint32_t newIndex;
   uint32_t const nXorOps = HSZ / sizeof(uint64_t);

   // Pure ROMix would use sequenceCount_ for the number of lookups.
   // We divide by 2 to reduce computation time RELATIVE to the memory usage
   // This still provides sufficient LUT operations, but allows us to use more
   // memory in the same amount of time (and this is the justification for
   // the scrypt algorithm -- it is basically ROMix, modified for more
   // flexibility in controlling compute-time vs memory-usage).
   uint32_t const nLookups = sequenceCount_ / 2;
   for (uint32_t nSeq = 0; nSeq < nLookups; nSeq++) {
      // Interpret last 4 bytes of last result (mod seqCt) as next LUT index
      newIndex = *(uint32_t*)(X.getPtr() + HSZ - 4) % sequenceCount_;

      // V represents the hash result at <newIndex>
      V64ptr = (uint64_t*)(frontOfLUT + HSZ * newIndex);

      // xor X with V, and store the result in X
      for (uint32_t i = 0; i < nXorOps; i++) {
         *(Y64ptr + i) = *(X64ptr + i) ^ *(V64ptr + i);
      }

      // Hash the xor'd data to get the next index for lookup
      BinaryDataRef bdrY(Y.getPtr(), HSZ);
      Cryptography::Hash::getSha512(bdrY, X.getPtr());
   }

   // Truncate the final result to get the final key
   lookupTable_.destroy();
   return X.getSliceCopy(0, kdfOutputBytes);
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData KdfRomix::DeriveKey(const SecureBinaryData& password)
{
   SecureBinaryData masterKey = password;
   for (uint32_t i = 0; i < numIterations_; i++) {
      masterKey = DeriveKey_OneIter(masterKey);
   }
   return masterKey;
}

////////////////////////////////////////////////////////////////////////////////
//// KeyDerivationFunction
////////////////////////////////////////////////////////////////////////////////
KeyDerivationFunction::KeyDerivationFunction()
{}

KeyDerivationFunction::~KeyDerivationFunction()
{}

bool KeyDerivationFunction::operator<(const KeyDerivationFunction& rhs)
{
   return getId() < rhs.getId();
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<KeyDerivationFunction> KeyDerivationFunction::deserialize(
   const BinaryDataRef& data)
{
   BinaryRefReader brr(data);

   //check size
   auto totalLen = brr.get_var_int();
   if (totalLen != brr.getSizeRemaining()) {
      throw std::runtime_error("invalid serialized kdf size");
   }
   std::shared_ptr<KeyDerivationFunction> kdfPtr = nullptr;

   auto version = brr.get_uint32_t();
   auto prefix = brr.get_uint16_t();
   switch (prefix)
   {
      case KDF_ROMIX_PREFIX:
      {
         switch (version)
         {
            case 0x00000001:
            {
               //iterations
               auto iterations = brr.get_uint32_t();

               //memTarget
               auto memTarget = brr.get_uint32_t();

               //salt
               auto len = brr.get_var_int();
               SecureBinaryData salt{std::move(brr.get_BinaryData(len))};

               kdfPtr = std::make_shared<KeyDerivationFunction_Romix>(
                  iterations, memTarget, std::move(salt));
               break;
            }

            default:
               throw std::runtime_error("unsupported kdf version");
         }

         break;
      }

      default:
         throw std::runtime_error("unexpected kdf prefix");
   }

   return kdfPtr;
}

////////////////////////////////////////////////////////////////////////////////
// KeyDerivationFunction_Romix
////////////////////////////////////////////////////////////////////////////////
KeyDerivationFunction_Romix::KeyDerivationFunction_Romix(
   const std::chrono::milliseconds& unlockTime, uint32_t memTargetMB) :
   KeyDerivationFunction(), salt_(initialize(unlockTime, memTargetMB))
{}

////
KeyDerivationFunction_Romix::KeyDerivationFunction_Romix(
   unsigned iterations, unsigned memTargetBytes, SecureBinaryData salt) :
   KeyDerivationFunction(),
   iterations_(iterations), memTargetBytes_(memTargetBytes), salt_(std::move(salt))
{}

////
KeyDerivationFunction_Romix::~KeyDerivationFunction_Romix()
{}

////////////////////////////////////////////////////////////////////////////////
KdfId KeyDerivationFunction_Romix::computeID() const
{
   BinaryWriter bw;
   bw.put_BinaryData(salt_);
   bw.put_uint32_t(iterations_);
   bw.put_uint32_t(memTargetBytes_);

   BinaryData bd;
   bd.resize(32);
   Cryptography::Hash::getHash256(bw.getData(), bd.getPtr());
   return KdfId::fromBinaryData(bd);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData KeyDerivationFunction_Romix::initialize(
   const std::chrono::milliseconds& unlockTime, uint32_t memTargetMB)
{
   KdfRomix kdf;
   kdf.computeKdfParams(unlockTime, memTargetMB * 1024 * 1024);
   iterations_ = kdf.getNumIterations();
   memTargetBytes_ = kdf.getMemoryReqtBytes();
   return kdf.getSalt();
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData KeyDerivationFunction_Romix::deriveKey(
   const SecureBinaryData& rawKey) const
{
   KdfRomix kdfObj{memTargetBytes_, iterations_, salt_.getRef()};
   return kdfObj.DeriveKey(rawKey);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData KeyDerivationFunction_Romix::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(KDF_ROMIX_VERSION);
   bw.put_uint16_t(KDF_ROMIX_PREFIX);
   bw.put_uint32_t(iterations_);
   bw.put_uint32_t(memTargetBytes_);
   bw.put_var_int(salt_.getSize());
   bw.put_BinaryData(salt_);

   BinaryWriter finalBw;
   finalBw.put_var_int(bw.getSize());
   finalBw.put_BinaryDataRef(bw.getDataRef());

   return finalBw.getData();
}

////////////////////////////////////////////////////////////////////////////////
const KdfId& KeyDerivationFunction_Romix::getId() const
{
   if (!id_.isValid()) {
      id_ = std::move(computeID());
   }
   return id_;
}

////////////////////////////////////////////////////////////////////////////////
std::string KeyDerivationFunction_Romix::getDisplayName() const
{
   return "ROMIX";
}

////////////////////////////////////////////////////////////////////////////////
bool KeyDerivationFunction_Romix::isSame(
   const KeyDerivationFunction* kdf) const
{
   auto kdfromix = dynamic_cast<const KeyDerivationFunction_Romix*>(kdf);
   if (kdfromix == nullptr) {
      return false;
   }

   return iterations_ == kdfromix->iterations_ &&
      memTargetBytes_ == kdfromix->memTargetBytes_ &&
      salt_ == kdfromix->salt_;
}

bool KeyDerivationFunction_Romix::isSimilar(
   const KeyDerivationFunction* kdf) const
{
   auto kdfromix = dynamic_cast<const KeyDerivationFunction_Romix*>(kdf);
   if (kdfromix == nullptr) {
      return false;
   }

   return iterations_ == kdfromix->iterations_ &&
      memTargetBytes_ == kdfromix->memTargetBytes_;
}

////////////////////////////////////////////////////////////////////////////////
unsigned KeyDerivationFunction_Romix::memTarget() const
{
   return memTargetBytes_;
}

////////
unsigned KeyDerivationFunction_Romix::iterations() const
{
   return iterations_;
}

////////
void KeyDerivationFunction_Romix::prettyPrint() const
{
   std::cout << "KDF Parameters:" << std::endl;
   std::cout << "   HashFunction : " << "sha512" << std::endl;
   std::cout << "   Memory/thread: " << memTargetBytes_ << " bytes" << std::endl;
   std::cout << "   NumIterations: " << iterations_ << std::endl;
   std::cout << "   Salt         : " << salt_.toHexStr() << std::endl;
}

////////////////////////////////////////////////////////////////////////////////
// KeyDerivationFunction_Passthrough
////////////////////////////////////////////////////////////////////////////////
KeyDerivationFunction_Passthrough::KeyDerivationFunction_Passthrough() :
   KeyDerivationFunction(), id_{passthroughKdfId}
{}

KeyDerivationFunction_Passthrough::~KeyDerivationFunction_Passthrough()
{}

const KdfId& KeyDerivationFunction_Passthrough::getId() const
{
   return id_;
}

std::string KeyDerivationFunction_Passthrough::getDisplayName() const
{
   return "Passthrough";
}

SecureBinaryData KeyDerivationFunction_Passthrough::deriveKey(
   const SecureBinaryData& key) const
{
   return key;
}

bool KeyDerivationFunction_Passthrough::isSame(
   const KeyDerivationFunction* rhs) const
{
   return rhs->getId() == getId();
}

bool KeyDerivationFunction_Passthrough::isSimilar(
   const KeyDerivationFunction* rhs) const
{
   return rhs->getId() == getId();
}

BinaryData KeyDerivationFunction_Passthrough::serialize() const
{
   throw std::runtime_error("passthrough kdf cannot be serialized");
}
