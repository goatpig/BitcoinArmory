////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <atomic>
#include <vector>
#include <string>
#include <stdexcept>

#include "log.h"

#define DEFAULT_BUFFER_SIZE 32*1048576

#define READHEX        BinaryData::CreateFromHex

#define READ_UINT8_LE  BinaryData::StrToIntLE<uint8_t>
#define READ_UINT16_LE BinaryData::StrToIntLE<uint16_t>
#define READ_UINT32_LE BinaryData::StrToIntLE<uint32_t>
#define READ_UINT64_LE BinaryData::StrToIntLE<uint64_t>

#define READ_UINT8_BE  BinaryData::StrToIntBE<uint8_t>
#define READ_UINT16_BE BinaryData::StrToIntBE<uint16_t>
#define READ_UINT32_BE BinaryData::StrToIntBE<uint32_t>
#define READ_UINT64_BE BinaryData::StrToIntBE<uint64_t>

#define READ_UINT8_HEX_LE(A)  (READ_UINT8_LE(READHEX(A)))
#define READ_UINT16_HEX_LE(A) (READ_UINT16_LE(READHEX(A)))
#define READ_UINT32_HEX_LE(A) (READ_UINT32_LE(READHEX(A)))
#define READ_UINT64_HEX_LE(A) (READ_UINT64_LE(READHEX(A)))

#define READ_UINT8_HEX_BE(A)  (READ_UINT8_BE(READHEX(A)))
#define READ_UINT16_HEX_BE(A) (READ_UINT16_BE(READHEX(A)))
#define READ_UINT32_HEX_BE(A) (READ_UINT32_BE(READHEX(A)))
#define READ_UINT64_HEX_BE(A) (READ_UINT64_BE(READHEX(A)))

#define WRITE_UINT8_LE  BinaryData::IntToStrLE<uint8_t>
#define WRITE_UINT16_LE BinaryData::IntToStrLE<uint16_t>
#define WRITE_UINT32_LE BinaryData::IntToStrLE<uint32_t>
#define WRITE_UINT64_LE BinaryData::IntToStrLE<uint64_t>

#define WRITE_UINT8_BE  BinaryData::IntToStrBE<uint8_t>
#define WRITE_UINT16_BE BinaryData::IntToStrBE<uint16_t>
#define WRITE_UINT32_BE BinaryData::IntToStrBE<uint32_t>
#define WRITE_UINT64_BE BinaryData::IntToStrBE<uint64_t>

enum ENDIAN
{
   ENDIAN_LITTLE,
   ENDIAN_BIG
};

#define LE ENDIAN_LITTLE
#define BE ENDIAN_BIG

class BinaryDataRef;

inline constexpr char hexLookupTable[16] = {
   '0','1','2','3',
   '4','5','6','7',
   '8','9','a','b',
   'c','d','e','f'
};

inline constexpr uint8_t binLookupTable[256] = {
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0, 0, 0, 0, 0, 0,
   0, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

////////////////////////////////////////////////////////////////////////////////
class BinaryData
{
public:
   BinaryData(void);
   BinaryData(const uint8_t*, size_t);
   BinaryData(const char*, size_t);
   BinaryData(const uint8_t*, const uint8_t*);
   BinaryData(const BinaryData&);
   BinaryData(BinaryData&&);
   BinaryData(std::vector<uint8_t>&&);
   BinaryData(const BinaryDataRef&);
   ~BinaryData(void);

   size_t getSize(void) const;
   bool empty(void) const;
   bool isZero(void) const;

   /////////////////////////////////////////////////////////////////////////////
   BinaryData& operator=(const BinaryData&);
   BinaryData& operator=(BinaryData&&);
   uint8_t& operator[](ssize_t);
   uint8_t operator[](ssize_t) const;
   BinaryData operator+(const BinaryData&) const;
   bool operator<(const BinaryData&) const;
   bool operator<(const BinaryDataRef&) const;
   bool operator==(const BinaryData&) const;
   bool operator!=(const BinaryData&) const;
   bool operator==(const BinaryDataRef&) const;
   bool operator!=(const BinaryDataRef&) const;
   bool operator>(const BinaryData&) const;
   bool operator>=(const BinaryData&) const;

   /////////////////////////////////////////////////////////////////////////////
   const uint8_t* getPtr(void) const;
   uint8_t* getPtr(void);
   const char* getCharPtr(void) const;
   char* getCharPtr(void);
   const std::vector<uint8_t>& getDataVector(void) const;
   BinaryDataRef getRef(void) const;

   /////////////////////////////////////////////////////////////////////////////
   // We allocate space as necesssary
   void copyFrom(const uint8_t*, const uint8_t*);
   void copyFrom(const std::string&);
   void copyFrom(const BinaryData&);
   void copyFrom(const BinaryDataRef&);
   void copyFrom(const char*, size_t);
   void copyFrom(const uint8_t*, size_t);

   /////////////////////////////////////////////////////////////////////////////
   // UNSAFE -- you don't know if outData holds enough space for this
   void copyTo(uint8_t*) const;
   void copyTo(uint8_t*, size_t) const;
   void copyTo(uint8_t*, size_t, size_t) const;
   void copyTo(BinaryData&) const;
   void fill(uint8_t);

   // This are always memory-safe
   void copyTo(std::string&);

   /////////////////////////////////////////////////////////////////////////////
   BinaryData& append(const BinaryData&);
   BinaryData& append(const BinaryDataRef&);
   BinaryData& append(const uint8_t*, size_t);
   BinaryData& append(uint8_t);

   /////////////////////////////////////////////////////////////////////////////
   int32_t find(const BinaryDataRef&, uint32_t=0);
   int32_t find(const BinaryData&, uint32_t=0);
   bool contains(const BinaryDataRef&, uint32_t=0);
   bool contains(const BinaryData&, uint32_t=0);

   /////////////////////////////////////////////////////////////////////////////
   bool startsWith(const BinaryDataRef&) const;
   bool startsWith(const BinaryData&) const;
   bool endsWith(const BinaryDataRef&) const;
   bool endsWith(const BinaryData&) const;

   /////////////////////////////////////////////////////////////////////////////
   BinaryDataRef getSliceRef(ssize_t, size_t) const;
   BinaryData    getSliceCopy(ssize_t, size_t) const;

   /////////////////////////////////////////////////////////////////////////////
   std::string toBinStr(bool=false) const;

   void resize(size_t);
   void reserve(size_t);

   /////////////////////////////////////////////////////////////////////////////
   // Swap endianness of the bytes in the index range [pos1, pos2)
   BinaryData& swapEndian(size_t=0, size_t=0);

   /////////////////////////////////////////////////////////////////////////////
   // Swap endianness of the bytes in the index range [pos1, pos2)
   BinaryData copySwapEndian(size_t=0, size_t=0) const;

   /////////////////////////////////////////////////////////////////////////////
   // This is an architecture-agnostic way to serialize integers to little- or
   // big-endian.  Bit-shift & mod will always return the lowest significant
   // bytes, so we can put them into an array of bytes in the desired order.
   template<typename INTTYPE>
   static BinaryData IntToStrLE(INTTYPE val)
   {
      static const uint8_t SZ = sizeof(INTTYPE);
      BinaryData out;
      out.resize(SZ);
      for (uint8_t i = 0; i < SZ; i++, val >>= 8) {
         out[i] = val % 256;
      }
      return out;
   }

   template<typename INTTYPE>
   inline static BinaryData IntToStrBE(INTTYPE val)
   {
      static const uint8_t SZ = sizeof(INTTYPE);
      BinaryData out;
      out.resize(SZ);
      for (uint8_t i = 0; i < SZ; i++, val >>= 8) {
         out[(SZ - 1) - i] = val % 256;
      }
      return out;
   }

   template<typename INTTYPE>
   static INTTYPE StrToIntLE(const BinaryData& binstr)
   {
      uint8_t const SZ = sizeof(INTTYPE);
      if (binstr.getSize() != SZ) {
         LOGERR << "StrToInt: strsz: " << binstr.getSize() << " intsz: " << SZ;
         return (INTTYPE)0;
      }
      return *(INTTYPE*)binstr.getPtr();
   }

   template<typename INTTYPE>
   static INTTYPE StrToIntBE(const BinaryData& binstr)
   {
      uint8_t const SZ = sizeof(INTTYPE);
      if (binstr.getSize() != SZ) {
         LOGERR << "StrToInt: strsz: " << binstr.getSize() << " intsz: " << SZ;
         return (INTTYPE)0;
      }

      INTTYPE out = 0;
      for (uint8_t i = 0; i < SZ; i++) {
         out |= ((INTTYPE)binstr[i]) << (8 * ((SZ - 1) - i));
      }
      return out;
   }

   template<typename INTTYPE>
   static INTTYPE StrToIntLE(const uint8_t* ptr)
   {
      return *(INTTYPE*)ptr;
   }

   template<typename INTTYPE>
   static INTTYPE StrToIntBE(const uint8_t* ptr)
   {
      uint8_t const SZ = sizeof(INTTYPE);
      INTTYPE out = 0;
      for (uint8_t i = 0; i < SZ; i++) {
         out |= ((INTTYPE)ptr[i]) << (8 * ((SZ - 1) - i));
      }
      return out;
   }

   /////////////////////////////////////////////////////////////////////////////
   static BinaryData fromString(const std::string&, size_t=SIZE_MAX);
   static BinaryData fromString(const std::string_view&, size_t=SIZE_MAX);

   /////////////////////////////////////////////////////////////////////////////
   void createFromHex(const std::string&);
   void createFromHex(const BinaryDataRef&);
   static BinaryData CreateFromHex(const std::string&);
   std::string toHexStr(bool=false) const;

   // For deallocating all the memory that is currently used by this BD
   void clear(void);
   std::vector<uint8_t> release(void);

   //for unordered containers
   struct Hasher
   {
      using is_transparent = void;
      std::size_t operator()(const BinaryData&) const;
      std::size_t operator()(const BinaryDataRef&) const;
   };

   struct IsEqual
   {
      using is_transparent = void;
      bool operator()(const BinaryData&, const BinaryData&) const;
      bool operator()(const BinaryData&, const BinaryDataRef&) const;
      bool operator()(const BinaryDataRef&, const BinaryData&) const;
   };

protected:
   std::vector<uint8_t> data_;

private:
   void alloc(size_t);
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BinaryDataRef
{
public:
   BinaryDataRef(void);
   BinaryDataRef(const uint8_t* , size_t);
   BinaryDataRef(const uint8_t*, const uint8_t*);
   BinaryDataRef(const BinaryDataRef&);
   BinaryDataRef(const BinaryData&);
   void reset(void);

   /////////////////////////////////////////////////////////////////////////////
   const uint8_t* getPtr(void) const;
   size_t getSize(void) const;
   bool empty(void) const;

   /////////////////////////////////////////////////////////////////////////////
   void setRef(const uint8_t*, size_t);
   void setRef(const uint8_t*, const uint8_t*);
   void setRef(const std::string&);
   void setRef(const BinaryData&);

   static BinaryDataRef fromString(const std::string&, size_t=SIZE_MAX);
   static BinaryDataRef fromStringView(const std::string_view&, size_t=SIZE_MAX);

   /////////////////////////////////////////////////////////////////////////////
   // UNSAFE -- you don't know if outData holds enough space for this
   void copyTo(uint8_t*) const;
   void copyTo(uint8_t*, size_t) const;
   void copyTo(uint8_t*, size_t, size_t) const;
   void copyTo(BinaryData&) const;

   /////////////////////////////////////////////////////////////////////////////
   // These are always memory-safe
   void copyTo(std::string&);
   BinaryData copy(void) const;

   /////////////////////////////////////////////////////////////////////////////
   const uint8_t& operator[](ssize_t) const;
   BinaryDataRef& operator=(const BinaryDataRef&);
   bool operator<(const BinaryDataRef&) const;
   bool operator==(const BinaryDataRef&) const;
   bool operator==(const BinaryData&) const;
   bool operator!=(const BinaryDataRef&) const;
   bool operator!=(const BinaryData&) const;
   bool operator>(const BinaryDataRef&) const;

   /////////////////////////////////////////////////////////////////////////////
   std::string toBinStr(bool=false) const;
   const char* toCharPtr(void) const;

   /////////////////////////////////////////////////////////////////////////////
   bool isValid(void) const;

   /////////////////////////////////////////////////////////////////////////////
   int32_t find(const BinaryDataRef&, uint32_t=0);
   int32_t find(const BinaryData&, uint32_t=0);
   bool contains(const BinaryDataRef&, uint32_t=0);
   bool contains(const BinaryData&, uint32_t=0);

   /////////////////////////////////////////////////////////////////////////////
   bool startsWith(const BinaryDataRef&) const;
   bool startsWith(const BinaryData&) const;
   bool endsWith(const BinaryDataRef&) const;
   bool endsWith(const BinaryData&) const;

   /////////////////////////////////////////////////////////////////////////////
   BinaryDataRef getSliceRef(ssize_t, size_t) const;
   BinaryData getSliceCopy(ssize_t, size_t) const;

   /////////////////////////////////////////////////////////////////////////////
   bool isSameRefAs(const BinaryDataRef&) const;
   std::string toHexStr(bool=false) const;
   bool isZero(void) const;

private:
   const uint8_t* ptr_;
   size_t nBytes_;
};


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BinaryReader
{
public:
   BinaryReader(int sz=0);
   BinaryReader(const BinaryData&);
   BinaryReader(uint8_t*, size_t);

   void setNewData(const BinaryData &);
   void setNewData(const uint8_t*, size_t);

   void advance(size_t);
   void rewind(size_t);
   void resize(size_t);

   uint64_t get_var_int(uint8_t* nRead=NULL);
   uint8_t get_uint8_t(void);
   uint16_t get_uint16_t(ENDIAN e=LE);
   uint32_t get_uint32_t(ENDIAN e=LE);
   int32_t get_int32_t(ENDIAN e=LE);
   uint64_t get_uint64_t(ENDIAN e=LE);

   void get_BinaryData(BinaryData&, size_t);
   void get_BinaryData(uint8_t*, size_t);
   BinaryDataRef get_BinaryDataRef(size_t nBytes);

   /////////////////////////////////////////////////////////////////////////////
   // Take the remaining buffer and shift it to the front
   // then return a pointer to where the old data ends
   //
   //
   //  Before:                             pos
   //                                       |
   //                                       V
   //             [ a b c d e f g h i j k l m n o p q r s t]
   //
   //  After:      pos           return*
   //               |               |
   //               V               V
   //             [ m n o p q r s t - - - - - - - - - - - -]
   //
   std::pair<uint8_t*, size_t> rotateRemaining(void);

   /////////////////////////////////////////////////////////////////////////////
   void           resetPosition(void);
   size_t         getPosition(void) const;
   size_t         getSize(void) const;
   size_t         getSizeRemaining(void) const;
   bool           isEndOfStream(void) const;
   uint8_t*       exposeDataPtr(void);
   const uint8_t* getCurrPtr(void);

private:
   BinaryData bdStr_;
   size_t     pos_;
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BinaryRefReader
{
public:
   /////////////////////////////////////////////////////////////////////////////
   BinaryRefReader(size_t sz=0);
   BinaryRefReader(const BinaryRefReader&);
   BinaryRefReader(const BinaryData&);
   BinaryRefReader(const BinaryDataRef&);
   BinaryRefReader(const uint8_t*, size_t nBytes=UINT32_MAX);

   void setNewData(const BinaryData&);
   void setNewData(const BinaryDataRef&);
   void setNewData(const uint8_t*, size_t nBytes=UINT32_MAX);

   BinaryRefReader& operator=(const BinaryRefReader&);

   /////////////////////////////////////////////////////////////////////////////
   uint64_t get_var_int(uint8_t* nRead=NULL);
   uint8_t get_uint8_t(void);
   uint16_t get_uint16_t(ENDIAN e=LE);
   uint32_t get_uint32_t(ENDIAN e=LE);
   int32_t get_int32_t(ENDIAN e = LE);
   uint64_t get_uint64_t(ENDIAN e=LE);
   int64_t get_int64_t(ENDIAN e = LE);
   double get_double(void);
   BinaryDataRef get_BinaryDataRef(uint32_t);
   BinaryRefReader fork(void) const;
   void get_BinaryData(BinaryData&, uint32_t);
   BinaryData get_BinaryData(uint32_t);
   void get_BinaryData(uint8_t*, uint32_t);
   std::string get_String(uint32_t);

   /////////////////////////////////////////////////////////////////////////////
   void advance(size_t);
   void rewind(uint32_t);
   void resetPosition(void);
   size_t getPosition(void) const;
   size_t getSize(void) const;
   bool empty(void) const;
   size_t getSizeRemaining(void) const;
   bool isEndOfStream(void) const;
   uint8_t const* exposeDataPtr(void);
   uint8_t const* getCurrPtr(void);

   /////////////////////////////////////////////////////////////////////////////
   BinaryDataRef getRawRef(void);

private:
   BinaryDataRef bdRef_;
   size_t totalSize_;

   /*
   On at least AMD Ryzen CPUs, gcc O1/2 compilation has demonstrated that reset
   and advance operations can result in out of order execution on pos_ leading to
   unexpected offset position, when pos_ is a simple size_t.

   Upgrading pos_ to either volatile or atomic<size_t> enforces the sequential
   execution of operations on pos_, fixing the issue.

   Since the only desirable additional feature is sequentiality, relaxed atomic
   operations were prefered to volatile, as they are generally cheaper at least
   on Windows (where volatiles come with acq_rel semantics by default).
   */
   std::atomic<size_t> pos_;
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// This is only intended to be used for the four datatypes:
//    uint8_t, uint16_t, uint32_t, uint64_t
// Simplicity is what makes this so useful 
template<typename DTYPE>
class BitPacker
{
public:
   BitPacker(void)
      : intVal_(0), bitsUsed_(0)
   {}

   void putBits(DTYPE val, uint32_t bitWidth)
   {
      uint8_t const SZ = sizeof(DTYPE);
      if (bitsUsed_ + bitWidth > SZ*8) {
         LOGERR << "Tried to put bits beyond end of bit field";
      }

      if (bitsUsed_==0 && bitWidth==SZ*8) {
         bitsUsed_ = SZ*8;
         intVal_ = val;
         return;
      }

      uint32_t shiftAmt = SZ*8 - (bitsUsed_ + bitWidth);
      DTYPE mask = (DTYPE)((1ULL<<bitWidth) - 1);
      intVal_ |= (val & mask) << shiftAmt;
      bitsUsed_ += bitWidth;
   }

   void putBit(bool val)
   {
      DTYPE bit = (val ? 1 : 0);
      putBits(bit, 1);
   }

   uint32_t getBitsUsed(void) {return bitsUsed_;} const

   BinaryData getBinaryData(void) const
   {
      return BinaryData::IntToStrBE<DTYPE>(intVal_);
   }

   // Disabling this to avoid inadvertantly using it to write out 
   // data in the wrong endianness.  (instead, always use getBinaryData
   // or writeToStream
   //DTYPE getValue(void)      { return intVal_; }
   void reset(void)
   {
      intVal_ = 0;
      bitsUsed_ = 0;
   }

private:
   DTYPE    intVal_;
   uint32_t bitsUsed_;
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// This is only intended to be used for the four datatypes:
//    uint8_t, uint16_t, uint32_t, uint64_t
// Simplicity is what makes this so useful 
template<typename DTYPE>
class BitUnpacker
{
public:
   BitUnpacker(void)
   {
      bitsRead_=0xffffffff;
   }

   BitUnpacker(DTYPE valToRead)
   {
      setValue(valToRead);
   }

   BitUnpacker(BinaryRefReader & brr)
   {
      BinaryData bytes = brr.get_BinaryData(sizeof(DTYPE));
      setValue(BinaryData::StrToIntBE<DTYPE>(bytes));
   }

   void setValue(DTYPE val)
   {
      intVal_ = val; bitsRead_ = 0;
   }

   DTYPE getBits(uint32_t bitWidth)
   {
      uint8_t const SZ = sizeof(DTYPE);
      if (bitsRead_==0 && bitWidth==SZ*8) {
         bitsRead_ = bitWidth;
         return intVal_;
      }
      uint32_t shiftAmt = SZ*8 - (bitsRead_ + bitWidth);
      DTYPE mask = (DTYPE)((1ULL<<bitWidth) - 1);
      bitsRead_ += bitWidth;
      return ((intVal_ >> shiftAmt) & mask);
   }

   bool getBit(void)
   {
      return (getBits(1) > 0);
   }

   void reset(void)
   {
      intVal_ = 0;
      bitsRead_ = 0;
   }

private:
   DTYPE    intVal_;
   uint32_t bitsRead_;
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BinaryWriter
{
public:
   /////////////////////////////////////////////////////////////////////////////
   // Using the argument to pre-allocate a certain amount of capacity.  Not 
   // required, but will improve performance if you can take a reasonable guess
   // about the final size of the output data
   BinaryWriter(size_t=0);

   /////////////////////////////////////////////////////////////////////////////
   // These write data properly regardless of the architecture
   void put_uint8_t (const uint8_t&);
   void put_uint16_t(const uint16_t&, ENDIAN=LE);
   void put_uint32_t(const uint32_t&, ENDIAN=LE);
   void put_int32_t(const int32_t&  , ENDIAN=LE);
   void put_uint64_t(const uint64_t&, ENDIAN=LE);
   void put_double(const double&);

   uint8_t put_var_int(const uint64_t&);
   void put_BinaryData(const BinaryData&, size_t = 0, uint32_t = 0);
   void put_BinaryDataRef(const BinaryDataRef&, size_t = 0, uint32_t = 0);
   void put_BinaryData(const uint8_t*, uint32_t);
   void put_String(const std::string&);
   void put_StringView(const std::string_view&);

   /////////////////////////////////////////////////////////////////////////////
   template<typename T>
   void put_BitPacker(BitPacker<T>& bp)
   {
      put_BinaryData(bp.getBinaryData());
   }

   const BinaryData& getData(void) const;
   size_t getSize(void) const;
   bool empty(void) const;
   BinaryDataRef getDataRef(void) const;
   std::string toString(void) const;
   std::string toHex(void) const;

   void reserve(size_t);
   void reset(void);

private:
   BinaryData theString_;
};
