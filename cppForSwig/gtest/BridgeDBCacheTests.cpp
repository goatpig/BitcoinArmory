////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BridgeTestUtils.h"

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BridgeDBCacheTests : public ::testing::Test
{
protected:
   //the 2 txs the inputs of the BIP174 test vector spend from
   const BinaryData rawTx1 = READHEX("0200000000010158e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd7501000000171600145f275f436b09a8cc9a2eb2a2f528485c68a56323feffffff02d8231f1b0100000017a914aed962d6654f9a2b36608eb9d64d2b260db4f1118700c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e88702483045022100a22edcc6e5bc511af4cc4ae0de0fcd75c7e04d8c1c3a8aa9d820ed4b967384ec02200642963597b9b1bc22c75e9f3e117284a962188bf5e8a74c895089046a20ad770121035509a48eb623e10aace8bfd0212fdb8a8e5af3c94b0b133b95e114cab89e4f7965000000");
   const BinaryData rawTx2 = READHEX("0200000001aad73931018bd25f84ae400b68848be09db706eac2ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d481c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b6393e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a270100000017a91429ca74f8a08f81999428185c97b5d852e4063f618765000000");
   const Types::TxKey minedKey = Types::constructTxKey(10, 3);
   const Types::TxKey zcKey = Types::constructZCKey(5);

   static void add(Ledgers::DBCache& cache,
      const BinaryData& raw, Types::TxKey key)
   {
      Tx tx(raw);
      tx.setTxKey(key);
      cache.addTx(tx);
   }

   static Types::TxKey keyForHash(const Ledgers::DBCache& cache,
      const BinaryData& raw)
   {
      return cache.getTxByHash(Tx(raw).getThisHash()).getDBKey();
   }
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, ZcThenMined)
{
   Ledgers::DBCache cache;
   add(cache, rawTx1, zcKey);
   EXPECT_EQ(keyForHash(cache, rawTx1), zcKey);

   //the mined copy takes the hash, the zc entry stays for the ledgers
   add(cache, rawTx1, minedKey);
   EXPECT_EQ(keyForHash(cache, rawTx1), minedKey);
   EXPECT_EQ(cache.getTx(zcKey).serialize(), rawTx1);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, MinedThenZc)
{
   Ledgers::DBCache cache;
   add(cache, rawTx1, minedKey);
   add(cache, rawTx1, zcKey);
   EXPECT_EQ(keyForHash(cache, rawTx1), minedKey);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, MinedThenRemined)
{
   //a reorg mines the tx again in another block, the new copy wins
   Ledgers::DBCache cache;
   const auto reminedKey = Types::constructTxKey(11, 0);
   add(cache, rawTx1, minedKey);
   add(cache, rawTx1, reminedKey);
   EXPECT_EQ(keyForHash(cache, rawTx1), reminedKey);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, EraseZcKeepsMined)
{
   Ledgers::DBCache cache;
   add(cache, rawTx1, zcKey);
   add(cache, rawTx1, minedKey);

   //dropping the zc copy leaves the mined one
   cache.eraseTx(zcKey);
   EXPECT_THROW(cache.getTx(zcKey), std::out_of_range);
   EXPECT_EQ(keyForHash(cache, rawTx1), minedKey);

   //erasing the key the hash points to still drops the hash
   cache.eraseTx(minedKey);
   EXPECT_THROW(cache.getTxByHash(Tx(rawTx1).getThisHash()),
      std::out_of_range);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, Candidates)
{
   //mined > pooled > unpooled, newest first within a kind
   auto unpooledKey = Types::constructUnpooledKey(7);
   auto minedKey2 = Types::constructTxKey(11, 0);

   Ledgers::TxKeyCandidates candidates;
   EXPECT_TRUE(candidates.empty());
   EXPECT_EQ(candidates.best(), Types::INVALID_TX_KEY);

   candidates.add(unpooledKey);
   EXPECT_EQ(candidates.best(), unpooledKey);
   candidates.add(zcKey);
   EXPECT_EQ(candidates.best(), zcKey);
   candidates.add(minedKey);
   EXPECT_EQ(candidates.best(), minedKey);
   candidates.add(minedKey2);
   EXPECT_EQ(candidates.best(), minedKey2);
   candidates.add(unpooledKey);
   EXPECT_EQ(candidates.keys(), (std::vector<Types::TxKey>{
      minedKey2, minedKey, zcKey, unpooledKey}));

   EXPECT_TRUE(candidates.remove(minedKey2));
   EXPECT_FALSE(candidates.remove(minedKey2));
   EXPECT_EQ(candidates.best(), minedKey);
   EXPECT_TRUE(candidates.remove(minedKey));
   EXPECT_EQ(candidates.best(), zcKey);
   EXPECT_TRUE(candidates.remove(zcKey));
   EXPECT_EQ(candidates.best(), unpooledKey);
   EXPECT_TRUE(candidates.remove(unpooledKey));
   EXPECT_TRUE(candidates.empty());

   //each key goes ahead of its kind and behind the better kinds: within a
   //kind the newest comes first, whatever its id
   auto minedKey0 = Types::constructTxKey(9, 0);
   auto zcKey2 = Types::constructZCKey(6);
   auto zcKey0 = Types::constructZCKey(4);
   auto unpooledKey2 = Types::constructUnpooledKey(8);
   auto unpooledKey0 = Types::constructUnpooledKey(6);
   for (auto txKey : {minedKey, zcKey, unpooledKey, zcKey2, minedKey2,
      unpooledKey2, zcKey0, minedKey0, unpooledKey0}) {
      candidates.add(txKey);
   }
   EXPECT_EQ(candidates.keys(), (std::vector<Types::TxKey>{
      minedKey0, minedKey2, minedKey, zcKey0, zcKey2, zcKey,
      unpooledKey0, unpooledKey2, unpooledKey}));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, Unpooled)
{
   Ledgers::DBCache cache;
   auto hash1 = Tx(rawTx1).getThisHash();

   //a raw tx gets the next unpooled key, once
   auto key1 = cache.addUnpooledTx(rawTx1);
   EXPECT_EQ(key1, Types::constructUnpooledKey(0));
   EXPECT_EQ(cache.getTx(key1).getBlockId(), Types::INVALID_BLOCK_ID);
   EXPECT_EQ(cache.getTx(key1).getTxIndex(), UINT16_MAX);
   EXPECT_EQ(cache.addUnpooledTx(rawTx1), key1);
   EXPECT_EQ(cache.addUnpooledTx(rawTx2), Types::constructUnpooledKey(1));
   EXPECT_EQ(cache.getTxByHash(hash1).serialize(), rawTx1);
   EXPECT_EQ(keyForHash(cache, rawTx1), key1);

   //the pooled and mined copies take over, the unpooled one stays below
   add(cache, rawTx1, zcKey);
   EXPECT_EQ(keyForHash(cache, rawTx1), zcKey);
   add(cache, rawTx1, minedKey);
   EXPECT_EQ(keyForHash(cache, rawTx1), minedKey);
   EXPECT_EQ(cache.addUnpooledTx(rawTx1), minedKey);

   //a purged hash keeps its mined copy only
   EXPECT_EQ(cache.purgeTxs({hash1}), (std::set<Types::TxKey>{zcKey, key1}));
   EXPECT_EQ(keyForHash(cache, rawTx1), minedKey);
   cache.eraseTx(minedKey);
   EXPECT_THROW(cache.getTxByHash(hash1), std::out_of_range);

   //an erased copy falls back to what's left
   auto zcKey2 = Types::constructZCKey(6);
   add(cache, rawTx2, zcKey2);
   EXPECT_EQ(keyForHash(cache, rawTx2), zcKey2);
   cache.eraseTx(zcKey2);
   EXPECT_EQ(keyForHash(cache, rawTx2), Types::constructUnpooledKey(1));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, EraseHashes)
{
   //every copy of the hash goes, other hashes stay
   Ledgers::DBCache cache;
   auto unpooledKey = cache.addUnpooledTx(rawTx1);
   add(cache, rawTx1, zcKey);
   add(cache, rawTx1, minedKey);
   auto zcKey2 = Types::constructZCKey(6);
   add(cache, rawTx2, zcKey2);

   cache.eraseHashes({Tx(rawTx1).getThisHash()});
   EXPECT_THROW(cache.getTxByHash(Tx(rawTx1).getThisHash()),
      std::out_of_range);
   for (auto txKey : {unpooledKey, zcKey, minedKey}) {
      EXPECT_THROW(cache.getTx(txKey), std::out_of_range);
   }
   EXPECT_EQ(keyForHash(cache, rawTx2), zcKey2);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, ZcIdReuse)
{
   //zc ids can be reused after ArmoryDB restarts: a key can come back for
   //another tx, the old tx goes
   Ledgers::DBCache cache;
   add(cache, rawTx1, zcKey);
   add(cache, rawTx2, zcKey);
   EXPECT_EQ(cache.getTx(zcKey).serialize(), rawTx2);
   EXPECT_EQ(keyForHash(cache, rawTx2), zcKey);
   EXPECT_THROW(cache.getTxByHash(Tx(rawTx1).getThisHash()),
      std::out_of_range);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, PurgeSkipsMined)
{
   Ledgers::DBCache cache;
   add(cache, rawTx1, minedKey);
   add(cache, rawTx2, zcKey);

   auto purged = cache.purgeTxs({
      Tx(rawTx1).getThisHash(), Tx(rawTx2).getThisHash()});
   EXPECT_EQ(purged, std::set<Types::TxKey>{zcKey});
   EXPECT_EQ(keyForHash(cache, rawTx1), minedKey);
   EXPECT_THROW(cache.getTxByHash(Tx(rawTx2).getThisHash()),
      std::out_of_range);
   EXPECT_THROW(cache.getTx(zcKey), std::out_of_range);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, InvalidKeys)
{
   //INVALID_TX_KEY and txio keys aren't tx keys, they are no candidates
   using Kind = Ledgers::TxKeyCandidates::Kind;
   const auto txioKey = Types::constructTxIOKey(10, 3, 1);
   const auto unpooledKey = Types::constructUnpooledKey(7);
   EXPECT_EQ(Ledgers::TxKeyCandidates::kindOf(Types::INVALID_TX_KEY),
      Kind::Invalid);
   EXPECT_EQ(Ledgers::TxKeyCandidates::kindOf(txioKey), Kind::Invalid);
   EXPECT_EQ(Ledgers::TxKeyCandidates::kindOf(minedKey), Kind::Mined);
   EXPECT_EQ(Ledgers::TxKeyCandidates::kindOf(zcKey), Kind::Pooled);
   EXPECT_EQ(Ledgers::TxKeyCandidates::kindOf(unpooledKey), Kind::Unpooled);

   Ledgers::TxKeyCandidates candidates;
   candidates.add(Types::INVALID_TX_KEY);
   candidates.add(txioKey);
   EXPECT_TRUE(candidates.empty());
   candidates.add(unpooledKey);
   candidates.add(Types::INVALID_TX_KEY);
   EXPECT_EQ(candidates.keys(), std::vector<Types::TxKey>{unpooledKey});

   //a tx starts with an invalid key, the cache doesn't take a tx without
   //a valid one
   Ledgers::DBCache cache;
   Tx unkeyed(rawTx2);
   EXPECT_EQ(unkeyed.getDBKey(), Types::INVALID_TX_KEY);
   cache.addTx(unkeyed);
   add(cache, rawTx1, Types::INVALID_TX_KEY);
   add(cache, rawTx1, txioKey);
   EXPECT_THROW(cache.getTxByHash(Tx(rawTx1).getThisHash()),
      std::out_of_range);
   EXPECT_THROW(cache.getTxByHash(Tx(rawTx2).getThisHash()),
      std::out_of_range);
   EXPECT_THROW(cache.getTx(Types::INVALID_TX_KEY), std::out_of_range);
   EXPECT_THROW(cache.getTx(txioKey), std::out_of_range);

   //a valid key still goes in
   add(cache, rawTx1, zcKey);
   EXPECT_EQ(keyForHash(cache, rawTx1), zcKey);
}
