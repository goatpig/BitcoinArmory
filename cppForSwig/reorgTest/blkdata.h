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

namespace TestChain
{
static const BinaryData addrA = BinaryData::CreateFromHex("62e907b15cbf27d5425399ebf6f0fb50ebb88f18");
static const BinaryData scrAddrA = HASH160PREFIX + addrA;
//const BinaryData privKeyAddrA = BinaryData::CreateFromHex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
static const BinaryData addrB = BinaryData::CreateFromHex("ee26c56fc1d942be8d7a24b2a1001dd894693980");
static const BinaryData scrAddrB = HASH160PREFIX + addrB;
static const BinaryData privKeyAddrB = BinaryData::CreateFromHex("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
static const BinaryData addrC = BinaryData::CreateFromHex("cb2abde8bccacc32e893df3a054b9ef7f227a4ce");
static const BinaryData scrAddrC = HASH160PREFIX + addrC;
static const BinaryData privKeyAddrC = BinaryData::CreateFromHex("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
static const BinaryData addrD = BinaryData::CreateFromHex("c522664fb0e55cdc5c0cea73b4aad97ec8343232");
static const BinaryData scrAddrD = HASH160PREFIX + addrD;
static const BinaryData privKeyAddrD = BinaryData::CreateFromHex("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
static const BinaryData addrE = BinaryData::CreateFromHex("47b8ad0b1d6803260ce428d9e09e2cd99fd3b359");
static const BinaryData scrAddrE = HASH160PREFIX + addrE;
static const BinaryData privKeyAddrE = BinaryData::CreateFromHex("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
static const BinaryData addrF = BinaryData::CreateFromHex("d63b766cd342e6f0f7390dd454065e4bbea26b1b");
static const BinaryData scrAddrF = HASH160PREFIX + addrF;
static const BinaryData privKeyAddrF = BinaryData::CreateFromHex("efefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefef");

// LB1 = AddrB + AddrC
// LB2 = AddrD + AddrE
static const std::string lb1B58ID = "DuR9CQX6";
static const BinaryData lb1ScrAddr = BinaryData::CreateFromHex("fe0102cb2abde8bccacc32e893df3a054b9ef7f227a4ceee26c56fc1d942be8d7a24b2a1001dd894693980");
static const BinaryData lb1ScrAddrP2SH = BinaryData::CreateFromHex("05ae3c7b8b4b00ae7b3d8702d0a23bcda2ac541e34");
static const std::string lb2B58ID = "eqWJZTcT";
static const BinaryData lb2ScrAddr = BinaryData::CreateFromHex("fe020247b8ad0b1d6803260ce428d9e09e2cd99fd3b359c522664fb0e55cdc5c0cea73b4aad97ec8343232");
static const BinaryData lb2ScrAddrP2SH = BinaryData::CreateFromHex("05de46f84ba728121eb3bdb1c30140cd6c0c8b7e25");

static const BinaryData blkHash0 = BinaryData::CreateFromHex("6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000");
static const BinaryData blkHash1 = BinaryData::CreateFromHex("fefba729db13850ba6ecc29a7dd41d80fb4bf7e063d5e093f48f0c5d00000000");
static const BinaryData blkHash2 = BinaryData::CreateFromHex("3641abc766dcc85643af42ad0999c2370ef1a5e521547fa4b55f5735449021ee");
static const BinaryData blkHash3 = BinaryData::CreateFromHex("23c0558cccfdbce453814beb4c5b0f6fa0625e1c0895df55ed810fbabfe04c65");
static const BinaryData blkHash4 = BinaryData::CreateFromHex("7f640833c553582959a144f2738c8683a601ab67a80f3b040cc92d5200000000");
static const BinaryData blkHash5 = BinaryData::CreateFromHex("e4b8313bec211633992fde8b9bb9727f56cb0413e2108ceab280fc7e00000000");
static const BinaryData blkHash4A = BinaryData::CreateFromHex("fb97af2c706d9410d90651d171f8e728d228fbfe7a14b58d475cb55800000000");
static const BinaryData blkHash5A = BinaryData::CreateFromHex("3a6a8a8a1b0a64d28c07b680e3a079530c5997894aafd31684b5f83500000000");

static const unsigned int zcTxSize = 258;
static const std::string zcTxHash256 = "b6b6f145742a9072fd85f96772e63a00eb4101709aa34ec5dd59e8fc904191a7";
static const unsigned int lbZCTxSize = 378;

/*
first chain:
 [0|0], [1|0], [2|0], [3|0], [4|0], [5|0]

reorg chain
 [0|0], [1|0], [2|0], [3|0], [4|1], [5|1]
*/

/////////////////////////////////////////////////////////////////////////////
static const std::vector<std::map<BinaryData, std::vector<uint64_t>>> testAddrBalances {
   { /* block 0 */ },
   { /* block 1 */ },
   { /* block 2 */
      { TestChain::scrAddrB,        { 55 * COIN,  5 * COIN, 55 * COIN, 4 } },
      { TestChain::scrAddrC,        {  0 * COIN,  0 * COIN,  0 * COIN, 0 } },
      { TestChain::scrAddrD,        {  0 * COIN,  0 * COIN,  0 * COIN, 0 } },
      { TestChain::scrAddrE,        {  0 * COIN,  0 * COIN,  0 * COIN, 0 } },
   },
   { /* block 3 */
      { TestChain::scrAddrA,        { 50 * COIN,  0 * COIN, 50 * COIN, 1 } },
      { TestChain::scrAddrB,        { 30 * COIN, 30 * COIN, 30 * COIN, 5 } },
      { TestChain::scrAddrC,        { 55 * COIN,  5 * COIN, 55 * COIN, 2 } },
      { TestChain::scrAddrD,        {  5 * COIN,  5 * COIN,  5 * COIN, 1 } },
      { TestChain::scrAddrE,        { 30 * COIN, 30 * COIN, 30 * COIN, 2 } },
      { TestChain::scrAddrF,        {  5 * COIN,  5 * COIN,  5 * COIN, 3 } },
      { TestChain::lb1ScrAddr,      { 10 * COIN, 10 * COIN, 10 * COIN, 1 } },
      { TestChain::lb1ScrAddrP2SH,  {  0 * COIN,  0 * COIN,  0 * COIN, 2 } },
      { TestChain::lb2ScrAddr,      { 10 * COIN, 10 * COIN, 10 * COIN, 3 } },
      { TestChain::lb2ScrAddrP2SH,  {  5 * COIN,  5 * COIN,  5 * COIN, 1 } },
   },
   { /* block 4 */
      { TestChain::scrAddrA,        { 50 * COIN,  0 * COIN, 50 * COIN, 1 } },
      { TestChain::scrAddrB,        { 30 * COIN, 30 * COIN, 30 * COIN, 5 } },
      { TestChain::scrAddrC,        { 10 * COIN, 10 * COIN, 10 * COIN, 3 } },
      { TestChain::scrAddrD,        { 60 * COIN, 10 * COIN, 60 * COIN, 3 } },
      { TestChain::scrAddrE,        { 30 * COIN, 30 * COIN, 30 * COIN, 2 } },
      { TestChain::scrAddrF,        { 10 * COIN, 10 * COIN, 10 * COIN, 4 } },
      { TestChain::lb1ScrAddr,      {  5 * COIN,  5 * COIN,  5 * COIN, 2 } },
      { TestChain::lb1ScrAddrP2SH,  { 25 * COIN, 25 * COIN, 25 * COIN, 3 } },
      { TestChain::lb2ScrAddr,      { 30 * COIN, 30 * COIN, 30 * COIN, 4 } },
      { TestChain::lb2ScrAddrP2SH,  {  0 * COIN,  0 * COIN,  0 * COIN, 2 } },
   },
   { /* block 5 */
      { TestChain::scrAddrA,        { 50 * COIN,  0 * COIN, 50 * COIN, 1 } },
      { TestChain::scrAddrB,        { 70 * COIN, 20 * COIN, 70 * COIN, 7 } },
      { TestChain::scrAddrC,        { 20 * COIN, 20 * COIN, 20 * COIN, 4 } },
      { TestChain::scrAddrD,        { 65 * COIN, 15 * COIN, 65 * COIN, 4 } },
      { TestChain::scrAddrE,        { 30 * COIN, 30 * COIN, 30 * COIN, 2 } },
      { TestChain::scrAddrF,        {  5 * COIN,  5 * COIN,  5 * COIN, 5 } },
      { TestChain::lb1ScrAddr,      {  5 * COIN,  5 * COIN,  5 * COIN, 2 } },
      { TestChain::lb1ScrAddrP2SH,  { 25 * COIN, 25 * COIN, 25 * COIN, 3 } },
      { TestChain::lb2ScrAddr,      { 30 * COIN, 30 * COIN, 30 * COIN, 4 } },
      { TestChain::lb2ScrAddrP2SH,  {  0 * COIN,  0 * COIN,  0 * COIN, 2 } },
   }
};

static const std::vector<std::map<BinaryData, std::vector<uint64_t>>> testAddrBalances_Reorg {
   { /* block 0 */ },
   { /* block 1 */ },
   { /* block 2 */ },
   { /* block 3 */ },
   { /* block 4A */
      { TestChain::scrAddrA,        { 50 * COIN,  0 * COIN, 50 * COIN, 1 } },
      { TestChain::scrAddrB,        { 30 * COIN, 30 * COIN, 30 * COIN, 5 } },
      { TestChain::scrAddrC,        { 55 * COIN,  5 * COIN, 55 * COIN, 2 } },
      { TestChain::scrAddrD,        {  5 * COIN,  5 * COIN,  5 * COIN, 1 } },
      { TestChain::scrAddrE,        { 30 * COIN, 30 * COIN, 30 * COIN, 2 } },
      { TestChain::scrAddrF,        { 55 * COIN,  5 * COIN, 55 * COIN, 4 } },
      { TestChain::lb1ScrAddr,      { 10 * COIN, 10 * COIN, 10 * COIN, 1 } },
      { TestChain::lb1ScrAddrP2SH,  {  0 * COIN,  0 * COIN,  0 * COIN, 2 } },
      { TestChain::lb2ScrAddr,      { 10 * COIN, 10 * COIN, 10 * COIN, 3 } },
      { TestChain::lb2ScrAddrP2SH,  {  5 * COIN,  5 * COIN,  5 * COIN, 1 } },
   },
   { /* block 5A */
      { TestChain::scrAddrA,        { 50 * COIN,  0 * COIN, 50 * COIN, 1 } },
      { TestChain::scrAddrB,        { 30 * COIN, 30 * COIN, 30 * COIN, 5 } },
      { TestChain::scrAddrC,        { 55 * COIN,  5 * COIN, 55 * COIN, 2 } },
      { TestChain::scrAddrD,        { 60 * COIN, 10 * COIN, 60 * COIN, 3 } },
      { TestChain::scrAddrE,        { 30 * COIN, 30 * COIN, 30 * COIN, 2 } },
      { TestChain::scrAddrF,        { 60 * COIN, 10 * COIN, 60 * COIN, 5 } },
      { TestChain::lb1ScrAddr,      {  5 * COIN,  5 * COIN,  5 * COIN, 2 } },
      { TestChain::lb1ScrAddrP2SH,  {  0 * COIN,  0 * COIN,  0 * COIN, 2 } },
      { TestChain::lb2ScrAddr,      { 10 * COIN, 10 * COIN, 10 * COIN, 3 } },
      { TestChain::lb2ScrAddrP2SH,  {  0 * COIN,  0 * COIN,  0 * COIN, 2 } },
   }
};

////////
static const std::vector<std::vector<uint64_t>> wltBal_BCDE {
   { /* block 0 */ },
   { /* block 1 */ },
   { /* block 2 */ 
       55 * COIN,  5 * COIN,  55 * COIN, 4 },
   { /* block 3 */
      120 * COIN, 70 * COIN, 120 * COIN, 9 },
   { /* block 4 */
      130 * COIN, 80 * COIN, 130 * COIN, 12 },
   { /* block 5 */
      185 * COIN, 85 * COIN, 185 * COIN, 15 }
};

static const std::vector<std::vector<uint64_t>> wltBal_BCDE_Reorg {
   { /* block 0 */ },
   { /* block 1 */ },
   { /* block 2 */ },
   { /* block 3 */
      120 * COIN, 70 * COIN, 120 * COIN, 9 },
   { /* block 4 */
      120 * COIN, 70 * COIN, 120 * COIN, 9 },
   { /* block 5 */
      175 * COIN, 75 * COIN, 175 * COIN, 11 }
};

static const std::vector<std::vector<uint64_t>> wltBal_AFLB {
   { /* block 0 */ },
   { /* block 1 */ },
   { /* block 2 */ },
   { /* block 3 */
      80 * COIN, 30 * COIN, 80 * COIN, 7 },
   { /* block 4 */
      120 * COIN, 70 * COIN, 120 * COIN, 10 },
   { /* block 5 */
      115 * COIN, 65 * COIN, 115 * COIN, 11 }
};

static const std::vector<std::vector<uint64_t>> wltBal_AFLB_Reorg {
   { /* block 0 */ },
   { /* block 1 */ },
   { /* block 2 */ },
   { /* block 3 */
      80 * COIN, 30 * COIN, 80 * COIN, 7 },
   { /* block 4 */
      130 * COIN, 30 * COIN, 130 * COIN, 8 },
   { /* block 5 */
      125 * COIN, 25 * COIN, 125 * COIN, 10 }
};

////////////////////////////////////////////////////////////////////////////////
struct LedgerEntryValue
{
   int64_t balance;
   uint32_t height;
   uint32_t index;
   uint32_t txTime;
   BinaryData txHash;
   bool sts = false;

   bool operator<(const LedgerEntryValue& rhs) const
   {
      //inverted on purpose
      if (height > rhs.height) {
         return true;
      } else if (height == rhs.height) {
         if (index > rhs.index) {
            return true;
         } else if (index == rhs.index) {
            return balance > rhs.balance;
         }
      }
      return false;
   }

   LedgerEntryValue& operator=(const LedgerEntryValue& rhs)
   {
      balance = rhs.balance;
      height = rhs.height;
      index = rhs.index;
      txTime = rhs.txTime;
      txHash = rhs.txHash;
      sts = rhs.sts;
      return *this;
   }

   bool operator==(const LedgerEntryValue& rhs) const
   {
      return balance == rhs.balance &&
         height == rhs.height &&
         index == rhs.index &&
         txTime == rhs.txTime &&
         txHash == rhs.txHash;
   }
};

/* tx hashes */

// block 0
static const BinaryData hash00 = READHEX("3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a");

// block 1
static const BinaryData hash10 = READHEX("ec6ee10ddb21cc8b21524f890e8dd8aed0b7e020032eed9e167a566c8659f35c");

// block 2
static const BinaryData hash20 = READHEX("63a471e560a4a4d3b956173c84175c39eece33dbae2c7987f1edf3bda1fb420c");
static const BinaryData hash21 = READHEX("b46699df740c38435c0430e2595c0a61733658ab923121d8a2edeac3895ae541");
static const BinaryData hash22 = READHEX("9ca7f69284d0e8b0ea5ad7915b50d65817fb5c592fdfc351e473b1291690c76b");

// block 3
static const BinaryData hash30 = READHEX("feac40d88e7931cfb01fd4aee8a0fae1481c2df357e0b9c7f6db33379d8bbc6f");
static const BinaryData hash31 = READHEX("d3f38cb8a7e9294db20b67bd99e64bfb6320cfe286f7f77170b33e685a2827a3");
static const BinaryData hash32 = READHEX("42551f6cb5674bdad8850a4a423101e42e17559568080f7dcd3d2c9480ae53ea");
static const BinaryData hash33 = READHEX("91c1d6c4eb074ce3570641e615a0ab7f6ca81f91186492b79e4118086ab047b6");
static const BinaryData hash34 = READHEX("36d958de246b7e422376803e4eb26cac9c82606a315a523f0b5c9f8c07a44677");
static const BinaryData hash35 = READHEX("9ec8177ca0a4f7aa21ec88a324f236a4d1dce6c610812a90e16febef4603a438");

// block 4
static const BinaryData hash40 = READHEX("d5fcc32ccb7e4c01049df1458c628253298f78c74a7cdd8df8f40125fbbf4f42");
static const BinaryData hash41 = READHEX("2564950008ae20c57bd2d5ffb2ceca67a33e003dc69f0d5b7c31c03a16e78071");
static const BinaryData hash42 = READHEX("1fb865e8cf2e430cca3a80adf8375bea5fddb0080e326b8df668373dfe599c07");
static const BinaryData hash43 = READHEX("6705262a37294c4e99890418668cfb97738b51b253ac86fcbee599a19fe15cea");

// block 5
static const BinaryData hash50 = READHEX("dc62ffa88e123ee99aa38ac5e73a65994e9332fbc0c554ebde7885900f5c06cb");
static const BinaryData hash51 = READHEX("b6b6f145742a9072fd85f96772e63a00eb4101709aa34ec5dd59e8fc904191a7");
static const BinaryData hash52 = READHEX("55501098859122d73e3e360d90574b468b8299f578738ce293789a7eeeb678b4");

/* address ledgers */

//A
static const std::vector<LedgerEntryValue> ledgersA{
   { 50 * COIN          , 0, 0, 1231006505, hash00},
};

static const std::vector<LedgerEntryValue> ledgersA_Reorg = ledgersA;

//B
static const std::vector<LedgerEntryValue> ledgersB{
   { -10 * (int64_t)COIN, 5, 1, 1231009513, hash51},
   { 50 * COIN          , 5, 0, 1231009513, hash50},
   { -25 * (int64_t)COIN, 3, 4, 1231008309, hash34},
   { -20 * (int64_t)COIN, 2, 2, 1231007708, hash22},
   { -25 * (int64_t)COIN, 2, 1, 1231007708, hash21},
   { 50 * COIN          , 2, 0, 1231007708, hash20},
   { 50 * COIN          , 1, 0, 1231007105, hash10},
};

static const std::vector<LedgerEntryValue> ledgersB_Reorg{
   { -25 * (int64_t)COIN, 3, 4, 1231008309, hash34},
   { -20 * (int64_t)COIN, 2, 2, 1231007708, hash22},
   { -25 * (int64_t)COIN, 2, 1, 1231007708, hash21},
   { 50 * COIN          , 2, 0, 1231007708, hash20},
   { 50 * COIN          , 1, 0, 1231007105, hash10},
};

//C
static const std::vector<LedgerEntryValue> ledgersC{
   { 10 * COIN          , 5, 1, 1231009513, hash51},
   { -45 * (int64_t)COIN, 4, 3, 1231008909, hash43},
   { 5 * COIN           , 3, 3, 1231008309, hash33},
   { 50 * COIN          , 3, 0, 1231008309, hash30},
};

static const std::vector<LedgerEntryValue> ledgersC_Reorg{
   { 5 * COIN           , 3, 3, 1231008309, hash33},
   { 50 * COIN          , 3, 0, 1231008309, hash30},
};

//D
static const std::vector<LedgerEntryValue> ledgersD{
   { 5 * COIN           , 5, 2, 1231009513, hash52},
   { 5 * COIN           , 4, 2, 1231008909, hash42},
   { 50 * COIN          , 4, 0, 1231008909, hash40},
   { 5 * COIN           , 3, 1, 1231008309, hash31},
};

static const std::vector<LedgerEntryValue> ledgersD_Reorg{
   { 5 * COIN           , 5, 2, 1231009510, hash42},
   { 50 * COIN          , 5, 0, 1231009510, READHEX("9bce56a46508c1ddfb4d8495c8c8eecc1735dcfe61631533ed85d3908bfa33f3")},
   { 5 * COIN           , 3, 1, 1231008309, hash31},
};

//E
static const std::vector<LedgerEntryValue> ledgersE{
   { 25 * COIN          , 3, 4, 1231008309, hash34},
   { 5 * COIN           , 3, 2, 1231008309, hash32},
};

static const std::vector<LedgerEntryValue> ledgersE_Reorg = ledgersE;

//F
static const std::vector<LedgerEntryValue> ledgersF{
   { -5 * (int64_t)COIN , 5, 2, 1231009513, hash52},
   { 5 * COIN           , 4, 1, 1231008909, hash41},
   { -10 * (int64_t)COIN, 3, 5, 1231008309, hash35},
   { -5 * (int64_t)COIN , 3, 1, 1231008309, hash31},
   { 20 * COIN          , 2, 2, 1231007708, hash22},
};

static const std::vector<LedgerEntryValue> ledgersF_Reorg{
   { 5 * COIN           , 5, 1, 1231009510, hash41},
   { 50 * COIN          , 4, 0, 1231008909, READHEX("45aa44ebb8bc87ad006abad80d0f246172289e92a863cca9be10470d05c6de4d")},
   { -10 * (int64_t)COIN, 3, 5, 1231008309, hash35},
   { -5 * (int64_t)COIN , 3, 1, 1231008309, hash31},
   { 20 * COIN          , 2, 2, 1231007708, hash22},
};

//LB1
static const std::vector<LedgerEntryValue> ledgersLB1{
   { -5 * (int64_t)COIN , 4, 1, 1231008909, hash41},
   { 10 * COIN          , 3, 2, 1231008309, hash32},
};

static const std::vector<LedgerEntryValue> ledgersLB1_Reorg{
   { -5 * (int64_t)COIN , 5, 1, 1231009510, hash41},
   { 10 * COIN          , 3, 2, 1231008309, hash32},
};

static const std::vector<LedgerEntryValue> ledgersLB1_P2SH{
   { 25 * COIN          , 4, 3, 1231008909, hash43},
   { -15 * (int64_t)COIN, 3, 2, 1231008309, hash32},
   { 15 * COIN          , 2, 1, 1231007708, hash21},
};

static const std::vector<LedgerEntryValue> ledgersLB1_P2SH_Reorg{
   { -15 * (int64_t)COIN, 3, 2, 1231008309, hash32},
   { 15 * COIN          , 2, 1, 1231007708, hash21},
};

//LB2
static const std::vector<LedgerEntryValue> ledgersLB2{
   { 20 * COIN          , 4, 3, 1231008909, hash43},
   { 10 * COIN          , 3, 5, 1231008309, hash35},
   { -10 * (int64_t)COIN, 3, 3, 1231008309, hash33},
   { 10 * COIN          , 2, 1, 1231007708, hash21}
};

static const std::vector<LedgerEntryValue> ledgersLB2_Reorg{
   { 10 * COIN          , 3, 5, 1231008309, hash35},
   { -10 * (int64_t)COIN, 3, 3, 1231008309, hash33},
   { 10 * COIN          , 2, 1, 1231007708, hash21}
};

static const std::vector<LedgerEntryValue> ledgersLB2_P2SH{
   { -5 * (int64_t)COIN , 4, 2, 1231008909, hash42},
   { 5 * COIN           , 3, 3, 1231008309, hash33},
};

static const std::vector<LedgerEntryValue> ledgersLB2_P2SH_Reorg{
   { -5 * (int64_t)COIN , 5, 2, 1231009510, hash42},
   { 5 * COIN           , 3, 3, 1231008309, hash33},
};

// wallet ledgers
static const std::vector<LedgerEntryValue> ledgersBCDE{
   { 5 * COIN           , 5, 2, 1231009513, hash52},
   { 30 * COIN          , 5, 1, 1231009513, hash51, true},
   { 50 * COIN          , 5, 0, 1231009513, hash50},
   { -45 * (int64_t)COIN, 4, 3, 1231008909, hash43},
   { 5 * COIN           , 4, 2, 1231008909, hash42},
   { 50 * COIN          , 4, 0, 1231008909, hash40},
   { 55 * COIN          , 3, 4, 1231008309, hash34, true},
   { 5 * COIN           , 3, 3, 1231008309, hash33},
   { 5 * COIN           , 3, 2, 1231008309, hash32},
   { 5 * COIN           , 3, 1, 1231008309, hash31},
   { 50 * COIN          , 3, 0, 1231008309, hash30},
   { -20 * (int64_t)COIN, 2, 2, 1231007708, hash22},
   { -25 * (int64_t)COIN, 2, 1, 1231007708, hash21},
   { 50 * COIN          , 2, 0, 1231007708, hash20},
   { 50 * COIN          , 1, 0, 1231007105, hash10},
};

static const std::vector<LedgerEntryValue> ledgersBCDE_Reorg{
   { 5 * COIN           , 5, 2, 1231009510, hash42},
   { 50 * COIN          , 5, 0, 1231009510, READHEX("9bce56a46508c1ddfb4d8495c8c8eecc1735dcfe61631533ed85d3908bfa33f3")},
   { 55 * COIN          , 3, 4, 1231008309, hash34, true},
   { 5 * COIN           , 3, 3, 1231008309, hash33},
   { 5 * COIN           , 3, 2, 1231008309, hash32},
   { 5 * COIN           , 3, 1, 1231008309, hash31},
   { 50 * COIN          , 3, 0, 1231008309, hash30},
   { -20 * (int64_t)COIN, 2, 2, 1231007708, hash22},
   { -25 * (int64_t)COIN, 2, 1, 1231007708, hash21},
   { 50 * COIN          , 2, 0, 1231007708, hash20},
   { 50 * COIN          , 1, 0, 1231007105, hash10},
};

static const std::vector<LedgerEntryValue> ledgersBC{
   { 30 * COIN          , 5, 1, 1231009513, hash51, true},
   { 50 * COIN          , 5, 0, 1231009513, hash50},
   { -45 * (int64_t)COIN, 4, 3, 1231008909, hash43},
   { -25 * (int64_t)COIN, 3, 4, 1231008309, hash34},
   { 5 * COIN           , 3, 3, 1231008309, hash33},
   { 50 * COIN          , 3, 0, 1231008309, hash30},
   { -20 * (int64_t)COIN, 2, 2, 1231007708, hash22},
   { -25 * (int64_t)COIN, 2, 1, 1231007708, hash21},
   { 50 * COIN          , 2, 0, 1231007708, hash20},
   { 50 * COIN          , 1, 0, 1231007105, hash10},
};

static const std::vector<LedgerEntryValue> ledgersBC_Reorg{
   { -25 * (int64_t)COIN, 3, 4, 1231008309, hash34},
   { 5 * COIN           , 3, 3, 1231008309, hash33},
   { 50 * COIN          , 3, 0, 1231008309, hash30},
   { -20 * (int64_t)COIN, 2, 2, 1231007708, hash22},
   { -25 * (int64_t)COIN, 2, 1, 1231007708, hash21},
   { 50 * COIN          , 2, 0, 1231007708, hash20},
   { 50 * COIN          , 1, 0, 1231007105, hash10},
};

static const std::vector<LedgerEntryValue> ledgersDE{
   { 5 * COIN           , 5, 2, 1231009513, hash52},
   { 5 * COIN           , 4, 2, 1231008909, hash42},
   { 50 * COIN          , 4, 0, 1231008909, hash40},
   { 25 * COIN          , 3, 4, 1231008309, hash34},
   { 5 * COIN           , 3, 2, 1231008309, hash32},
   { 5 * COIN           , 3, 1, 1231008309, hash31},
};

static const std::vector<LedgerEntryValue> ledgersDE_Reorg{
   { 5 * COIN           , 5, 2, 1231009510, hash42},
   { 50 * COIN          , 5, 0, 1231009510, READHEX("9bce56a46508c1ddfb4d8495c8c8eecc1735dcfe61631533ed85d3908bfa33f3")},
   { 25 * COIN          , 3, 4, 1231008309, hash34},
   { 5 * COIN           , 3, 2, 1231008309, hash32},
   { 5 * COIN           , 3, 1, 1231008309, hash31},
};

static const std::vector<LedgerEntryValue> ledgersAFLB{
   { -5 * (int64_t)COIN , 5, 2, 1231009513, hash52},
   { 45 * COIN          , 4, 3, 1231008909, hash43},
   { -5 * (int64_t)COIN , 4, 2, 1231008909, hash42},
   { 10 * COIN          , 4, 1, 1231008909, hash41, true},
   { 15 * COIN          , 3, 5, 1231008309, hash35, true},
   { -5 * (int64_t)COIN , 3, 3, 1231008309, hash33},
   { -5 * (int64_t)COIN , 3, 2, 1231008309, hash32},
   { -5 * (int64_t)COIN , 3, 1, 1231008309, hash31},
   { 20 * COIN          , 2, 2, 1231007708, hash22},
   { 25 * COIN          , 2, 1, 1231007708, hash21},
   { 50 * COIN          , 0, 0, 1231006505, READHEX("3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a")},
};

static const std::vector<LedgerEntryValue> ledgersAFLB_Reorg{
   { -5 * (int64_t)COIN , 5, 2, 1231009510, hash42},
   { 10 * COIN          , 5, 1, 1231009510, hash41, true},
   { 50 * COIN          , 4, 0, 1231008909, READHEX("45aa44ebb8bc87ad006abad80d0f246172289e92a863cca9be10470d05c6de4d")},
   { 15 * COIN          , 3, 5, 1231008309, hash35, true},
   { -5 * (int64_t)COIN , 3, 3, 1231008309, hash33},
   { -5 * (int64_t)COIN , 3, 2, 1231008309, hash32},
   { -5 * (int64_t)COIN , 3, 1, 1231008309, hash31},
   { 20 * COIN          , 2, 2, 1231007708, hash22},
   { 25 * COIN          , 2, 1, 1231007708, hash21},
   { 50 * COIN          , 0, 0, 1231006505, hash00},
};
}

/***
blocks 0 to 5

Block #0|0, 6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000
   Prev: 0000000000000000000000000000000000000000000000000000000000000000
   Txs: 1
   Timestamp: 1231006505

   * Tx [0|0:0], 3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a
      inputs: 1, outputs: 1

      + Coinbase
         amount: 50

      - TxOut [0|0:0-0]
         dest: scrAddrA
         amount: 50


Block #1|0, fefba729db13850ba6ecc29a7dd41d80fb4bf7e063d5e093f48f0c5d00000000
   Prev: 6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000
   Txs: 1
   Timestamp: 1231007105

   * Tx [1|0:0], ec6ee10ddb21cc8b21524f890e8dd8aed0b7e020032eed9e167a566c8659f35c
      inputs: 1, outputs: 1

      + Coinbase
         amount: 50

      - TxOut [1|0:0-0]
         dest: scrAddrB
         amount: 50


Block #2|0, 3641abc766dcc85643af42ad0999c2370ef1a5e521547fa4b55f5735449021ee
   Prev: fefba729db13850ba6ecc29a7dd41d80fb4bf7e063d5e093f48f0c5d00000000
   Txs: 3
   Timestamp: 1231007708

   * Tx [2|0:0], 63a471e560a4a4d3b956173c84175c39eece33dbae2c7987f1edf3bda1fb420c
      inputs: 1, outputs: 1

      + Coinbase
         amount: 50

      - TxOut [2|0:0-0]
         dest: scrAddrB
         amount: 50

   * Tx [2|0:1], b46699df740c38435c0430e2595c0a61733658ab923121d8a2edeac3895ae541
      inputs: 1, outputs: 3

      + TxIn #0
         Outpoint: [1|0:0-0]
         amount: 50
         addr: scrAddrB

      - TxOut [2|0:1-0]
         dest: lb1P2SH
         amount: 15
      - TxOut [2|0:1-1]
         dest: lb2
         amount: 10
      - TxOut [2|0:1-2]
         dest: scrAddrB
         amount: 25

   * Tx [2|0:2], 9ca7f69284d0e8b0ea5ad7915b50d65817fb5c592fdfc351e473b1291690c76b
      inputs: 1, outputs: 2

      + TxIn #0
         Outpoint: [2|0:1-2]
         amount: 25
         addr: scrAddrB

      - TxOut [2|0:2-0]
         dest: scrAddrF
         amount: 20
      - TxOut [2|0:2-1]
         dest: scrAddrB
         amount: 5


Block #3|0, 23c0558cccfdbce453814beb4c5b0f6fa0625e1c0895df55ed810fbabfe04c65
   Prev: 3641abc766dcc85643af42ad0999c2370ef1a5e521547fa4b55f5735449021ee
   Txs: 6
   Timestamp: 1231008309

   * Tx [3|0:0], feac40d88e7931cfb01fd4aee8a0fae1481c2df357e0b9c7f6db33379d8bbc6f
      inputs: 1, outputs: 1

      + Coinbase
         amount: 50

      - TxOut [3|0:0-0]
         dest: scrAddrC
         amount: 50

   * Tx [3|0:1], d3f38cb8a7e9294db20b67bd99e64bfb6320cfe286f7f77170b33e685a2827a3
      inputs: 1, outputs: 2

      + TxIn #0
         Outpoint: [2|0:2-0]
         amount: 20
         addr: scrAddrF

      - TxOut [3|0:1-0]
         dest: scrAddrD
         amount: 5
      - TxOut [3|0:1-1]
         dest: scrAddrF
         amount: 15

   * Tx [3|0:2], 42551f6cb5674bdad8850a4a423101e42e17559568080f7dcd3d2c9480ae53ea
      inputs: 1, outputs: 2

      + TxIn #0
         Outpoint: [2|0:1-0]
         amount: 15
         addr: lb1P2SH

      - TxOut [3|0:2-0]
         dest: scrAddrE
         amount: 5
      - TxOut [3|0:2-1]
         dest: lb1
         amount: 10

   * Tx [3|0:3], 91c1d6c4eb074ce3570641e615a0ab7f6ca81f91186492b79e4118086ab047b6
      inputs: 1, outputs: 2

      + TxIn #0
         Outpoint: [2|0:1-1]
         amount: 10
         addr: lb2

      - TxOut [3|0:3-0]
         dest: scrAddrC
         amount: 5
      - TxOut [3|0:3-1]
         dest: lb2P2SH
         amount: 5

   * Tx [3|0:4], 36d958de246b7e422376803e4eb26cac9c82606a315a523f0b5c9f8c07a44677
      inputs: 2, outputs: 2

      + TxIn #0
         Outpoint: [2|0:0-0]
         amount: 50
         addr: scrAddrB
      + TxIn #1
         Outpoint: [2|0:2-1]
         amount: 5
         addr: scrAddrB

      - TxOut [3|0:4-0]
         dest: scrAddrE
         amount: 25
      - TxOut [3|0:4-1]
         dest: scrAddrB
         amount: 30

   * Tx [3|0:5], 9ec8177ca0a4f7aa21ec88a324f236a4d1dce6c610812a90e16febef4603a438
      inputs: 1, outputs: 2

      + TxIn #0
         Outpoint: [3|0:1-1]
         amount: 15
         addr: scrAddrF

      - TxOut [3|0:5-0]
         dest: lb2
         amount: 10
      - TxOut [3|0:5-1]
         dest: scrAddrF
         amount: 5


Block #4|0, 7f640833c553582959a144f2738c8683a601ab67a80f3b040cc92d5200000000
   Prev: 23c0558cccfdbce453814beb4c5b0f6fa0625e1c0895df55ed810fbabfe04c65
   Txs: 4
   Timestamp: 1231008909

   * Tx [4|0:0], d5fcc32ccb7e4c01049df1458c628253298f78c74a7cdd8df8f40125fbbf4f42
      inputs: 1, outputs: 1

      + Coinbase
         amount: 50

      - TxOut [4|0:0-0]
         dest: scrAddrD
         amount: 50

   * Tx [4|0:1], 2564950008ae20c57bd2d5ffb2ceca67a33e003dc69f0d5b7c31c03a16e78071
      inputs: 1, outputs: 2

      + TxIn #0
         Outpoint: [3|0:2-1]
         amount: 10
         addr: lb1

      - TxOut [4|0:1-0]
         dest: scrAddrF
         amount: 5
      - TxOut [4|0:1-1]
         dest: lb1
         amount: 5

   * Tx [4|0:2], 1fb865e8cf2e430cca3a80adf8375bea5fddb0080e326b8df668373dfe599c07
      inputs: 1, outputs: 1

      + TxIn #0
         Outpoint: [3|0:3-1]
         amount: 5
         addr: lb2P2SH

      - TxOut [4|0:2-0]
         dest: scrAddrD
         amount: 5

   * Tx [4|0:3], 6705262a37294c4e99890418668cfb97738b51b253ac86fcbee599a19fe15cea
      inputs: 2, outputs: 3

      + TxIn #0
         Outpoint: [3|0:0-0]
         amount: 50
         addr: scrAddrC
      + TxIn #1
         Outpoint: [3|0:3-0]
         amount: 5
         addr: scrAddrC

      - TxOut [4|0:3-0]
         dest: lb1P2SH
         amount: 25
      - TxOut [4|0:3-1]
         dest: lb2
         amount: 20
      - TxOut [4|0:3-2]
         dest: scrAddrC
         amount: 10


Block #5|0, e4b8313bec211633992fde8b9bb9727f56cb0413e2108ceab280fc7e00000000
   Prev: 7f640833c553582959a144f2738c8683a601ab67a80f3b040cc92d5200000000
   Txs: 3
   Timestamp: 1231009513

   * Tx [5|0:0], dc62ffa88e123ee99aa38ac5e73a65994e9332fbc0c554ebde7885900f5c06cb
      inputs: 1, outputs: 1

      + Coinbase
         amount: 50

      - TxOut [5|0:0-0]
         dest: scrAddrB
         amount: 50

   * Tx [5|0:1], b6b6f145742a9072fd85f96772e63a00eb4101709aa34ec5dd59e8fc904191a7
      inputs: 1, outputs: 2

      + TxIn #0
         Outpoint: [3|0:4-1]
         amount: 30
         addr: scrAddrB

      - TxOut [5|0:1-0]
         dest: scrAddrC
         amount: 10
      - TxOut [5|0:1-1]
         dest: scrAddrB
         amount: 20

   * Tx [5|0:2], 55501098859122d73e3e360d90574b468b8299f578738ce293789a7eeeb678b4
      inputs: 1, outputs: 1

      + TxIn #0
         Outpoint: [4|0:1-0]
         amount: 5
         addr: scrAddrF

      - TxOut [5|0:2-0]
         dest: scrAddrD
         amount: 5
***/

/***
blocks 4A, 5A

Block #4|1, fb97af2c706d9410d90651d171f8e728d228fbfe7a14b58d475cb55800000000
   Prev: 23c0558cccfdbce453814beb4c5b0f6fa0625e1c0895df55ed810fbabfe04c65
   Txs: 1
   Timestamp: 1231008909

   * Tx [4|1:0], 45aa44ebb8bc87ad006abad80d0f246172289e92a863cca9be10470d05c6de4d
      inputs: 1, outputs: 1

      + Coinbase
         amount: 50

      - TxOut [4|1:0-0]
         dest: scrAddrF
         amount: 50


Block #5|1, 3a6a8a8a1b0a64d28c07b680e3a079530c5997894aafd31684b5f83500000000
   Prev: fb97af2c706d9410d90651d171f8e728d228fbfe7a14b58d475cb55800000000
   Txs: 3
   Timestamp: 1231009510

   * Tx [5|1:0], 9bce56a46508c1ddfb4d8495c8c8eecc1735dcfe61631533ed85d3908bfa33f3
      inputs: 1, outputs: 1

      + Coinbase
         amount: 50

      - TxOut [5|1:0-0]
         dest: scrAddrD
         amount: 50

   * Tx [5|1:1], 2564950008ae20c57bd2d5ffb2ceca67a33e003dc69f0d5b7c31c03a16e78071
      inputs: 1, outputs: 2

      + TxIn #0
         Outpoint: [3|0:2-1]
         amount: 10
         addr: lb1

      - TxOut [5|1:1-0]
         dest: scrAddrF
         amount: 5
      - TxOut [5|1:1-1]
         dest: lb1
         amount: 5

   * Tx [5|1:2], 1fb865e8cf2e430cca3a80adf8375bea5fddb0080e326b8df668373dfe599c07
      inputs: 1, outputs: 1

      + TxIn #0
         Outpoint: [3|0:3-1]
         amount: 5
         addr: lb2P2SH

      - TxOut [5|1:2-0]
         dest: scrAddrD
         amount: 5
***/