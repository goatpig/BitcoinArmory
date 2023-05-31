////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "EncryptionUtils.h"

using namespace std;

/////////////////////////////////////////////////////////////////////////////
KdfRomix::KdfRomix(void) :
   hashFunctionName_("sha512"),
   hashOutputBytes_(64),
   kdfOutputBytes_(32),
   memoryReqtBytes_(32),
   numIterations_(0)
{
   // Nothing to do here
}

/////////////////////////////////////////////////////////////////////////////
KdfRomix::KdfRomix(uint32_t memReqts, uint32_t numIter, SecureBinaryData salt) :
   hashFunctionName_("sha512"),
   hashOutputBytes_(64),
   kdfOutputBytes_(32)
{
   usePrecomputedKdfParams(memReqts, numIter, salt);
}

/////////////////////////////////////////////////////////////////////////////
void KdfRomix::computeKdfParams(
   double targetComputeMSec,
   uint32_t maxMemReqts,
   bool verbose)
{
   // Create a random salt, even though this is probably unnecessary:
   // the variation in numIter and memReqts is probably effective enough
   salt_ = CryptoPRNG::generateRandom(32);

   // If target compute is 0s, then this method really only generates
   // a random salt, and sets the other params to default minimum.
   if (targetComputeMSec <= 4)
   {
      numIterations_ = 1;
      memoryReqtBytes_ = 1024;
      return;
   }


   // Here, we pick the largest memory reqt that allows the executing system
   // to compute the KDF is less than the target time.  A maximum can be
   // specified, in case the target system is likely to be memory-limited
   // more than compute-speed limited

   //12 bytes test key
   auto testKey = SecureBinaryData::fromString("- Test Key -");

   // Start the search for a memory value at 1kB
   memoryReqtBytes_ = 1024;
   uint64_t approxMSec = 0;
   while (approxMSec <= targetComputeMSec / 4 &&
      memoryReqtBytes_ < maxMemReqts)
   {
      memoryReqtBytes_ *= 2;

      sequenceCount_ = memoryReqtBytes_ / hashOutputBytes_;
      lookupTable_.resize(memoryReqtBytes_);

      auto start = chrono::system_clock::now();
      testKey = DeriveKey_OneIter(testKey);
      auto end = chrono::system_clock::now();
      approxMSec = chrono::duration_cast<chrono::milliseconds>(
         end - start).count();
   }

   // Recompute here, in case we didn't enter the search above
   sequenceCount_ = memoryReqtBytes_ / hashOutputBytes_;
   lookupTable_.resize(memoryReqtBytes_);


   // Depending on the search above (or if a low max memory was chosen,
   // we may need to do multiple iterations to achieve the desired compute
   // time on this system.
   uint64_t allItersMSec = 0;
   uint32_t numTest = 1;
   while (allItersMSec < 100)
   {
      numTest *= 2;
      auto start = chrono::system_clock::now();
      for (uint32_t i = 0; i < numTest; i++)
      {
         auto&& _testKey = testKey;
         _testKey = DeriveKey_OneIter(_testKey);
      }
      auto end = chrono::system_clock::now();
      allItersMSec = chrono::duration_cast<chrono::milliseconds>(
         end - start).count();
   }

   uint64_t perIterMSec = allItersMSec / numTest;
   numIterations_ = (uint32_t)(targetComputeMSec / (perIterMSec + 1));
   numIterations_ = (numIterations_ < 1 ? 1 : numIterations_ + 1);
   if (verbose)
   {
      cout << "System speed test results    :  " << endl;
      cout << "   Total test of the KDF took:  " << allItersMSec << " ms" << endl;
      cout << "                   to execute:  " << numTest << " iterations" << endl;
      cout << "   Target computation time is:  " << targetComputeMSec << " ms" << endl;
      cout << "   Setting numIterations to:    " << numIterations_ << endl;
   }
}

/////////////////////////////////////////////////////////////////////////////
void KdfRomix::usePrecomputedKdfParams(uint32_t memReqts,
   uint32_t numIter,
   SecureBinaryData salt)
{
   memoryReqtBytes_ = memReqts;
   sequenceCount_ = memoryReqtBytes_ / hashOutputBytes_;
   numIterations_ = numIter;
   salt_ = salt;
}

/////////////////////////////////////////////////////////////////////////////
void KdfRomix::printKdfParams(void)
{
   // SHA512 computes 64-byte outputs
   cout << "KDF Parameters:" << endl;
   cout << "   HashFunction : " << hashFunctionName_ << endl;
   cout << "   Memory/thread: " << memoryReqtBytes_ << " bytes" << endl;
   cout << "   SequenceCount: " << sequenceCount_ << endl;
   cout << "   NumIterations: " << numIterations_ << endl;
   cout << "   Salt         : " << salt_.toHexStr() << endl;
}


/////////////////////////////////////////////////////////////////////////////
SecureBinaryData KdfRomix::DeriveKey_OneIter(SecureBinaryData const & password)
{
   // Concatenate the salt/IV to the password
   SecureBinaryData saltedPassword = password + salt_;

   // Prepare the lookup table
   lookupTable_.resize(memoryReqtBytes_);
   lookupTable_.fill(0);
   uint32_t const HSZ = hashOutputBytes_;
   uint8_t* frontOfLUT = lookupTable_.getPtr();
   uint8_t* nextRead = NULL;
   uint8_t* nextWrite = NULL;

   // First hash to seed the lookup table, input is variable length anyway
   CryptoSHA2::getSha512(saltedPassword.getRef(), frontOfLUT);

   // Compute <sequenceCount_> consecutive hashes of the passphrase
   // Every iteration is stored in the next 64-bytes in the Lookup table
   for (uint32_t nByte = 0; nByte < memoryReqtBytes_ - HSZ; nByte += HSZ)
   {
      // Compute hash of slot i, put result in slot i+1
      nextRead = frontOfLUT + nByte;
      nextWrite = nextRead + hashOutputBytes_;
      BinaryDataRef bdr_next(nextRead, hashOutputBytes_);
      CryptoSHA2::getSha512(bdr_next, nextWrite);
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
   // This still provides suffient LUT operations, but allows us to use more
   // memory in the same amount of time (and this is the justification for
   // the scrypt algorithm -- it is basically ROMix, modified for more 
   // flexibility in controlling compute-time vs memory-usage).
   uint32_t const nLookups = sequenceCount_ / 2;
   for (uint32_t nSeq = 0; nSeq < nLookups; nSeq++)
   {
      // Interpret last 4 bytes of last result (mod seqCt) as next LUT index
      newIndex = *(uint32_t*)(X.getPtr() + HSZ - 4) % sequenceCount_;

      // V represents the hash result at <newIndex>
      V64ptr = (uint64_t*)(frontOfLUT + HSZ * newIndex);

      // xor X with V, and store the result in X
      for (uint32_t i = 0; i < nXorOps; i++)
         *(Y64ptr + i) = *(X64ptr + i) ^ *(V64ptr + i);

      // Hash the xor'd data to get the next index for lookup
      BinaryDataRef bdrY(Y.getPtr(), HSZ);
      CryptoSHA2::getSha512(bdrY, X.getPtr());
   }
   // Truncate the final result to get the final key
   lookupTable_.destroy();
   return X.getSliceCopy(0, kdfOutputBytes_);
}

/////////////////////////////////////////////////////////////////////////////
SecureBinaryData KdfRomix::DeriveKey(SecureBinaryData const & password)
{
   SecureBinaryData masterKey(password);
   for (uint32_t i = 0; i < numIterations_; i++)
      masterKey = DeriveKey_OneIter(masterKey);

   return masterKey;
}
