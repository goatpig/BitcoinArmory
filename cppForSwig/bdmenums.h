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

#define NODE_PORT_MAINNET 8333
#define NODE_PORT_TESTNET 18333
#define NODE_PORT_REGTEST 18444

#define LISTEN_PORT_MAINNET 9001
#define LISTEN_PORT_TESTNET 19001
#define LISTEN_PORT_REGTEST 19002

#define RPC_PORT_MAINNET 8332
#define RPC_PORT_TESTNET 18332
#define RPC_PORT_REGTEST 18443

#define BDM_FATAL_ERROR_CODE INT_MIN

enum class BDMState : int
{
   Uninitialized,
   Initializing,
   Ready
};

enum BDMPhase
{
   BDMPhase_DBHeaders = 1,
   BDMPhase_OrganizingChain,
   BDMPhase_BlockHeaders,
   BDMPhase_BlockData,
   BDMPhase_Rescan,
   BDMPhase_Balance,
   BDMPhase_SearchHashes,
   BDMPhase_ResolveHashes,
   BDMPhase_Completed
};

enum BDMAction
{
   BDMAction_Ready=1,
   BDMAction_Registered,
   BDMAction_NewBlock,
   BDMAction_ZC,
   BDMAction_InvalidatedZC,
   BDMAction_Refresh,
   BDMAction_Exited,
   BDMAction_ErrorMsg,
   BDMAction_NodeStatus,
   BDMAction_BDV_Error
};

enum class ARMORY_DB_TYPE : int
{
   Bare = 1,
   Full,
   Super
};

enum SOCKET_SERVICE
{
   SERVICE_WEBSOCKET,
   SERVICE_UNITTEST,
   SERVICE_UNITTEST_WITHWS
};

enum class BdmInitMode : int
{
   RESUME,
   RESCAN,
   REBUILD,
};

enum class SocketType : int
{
   Http,
   WS,
   BitcoinP2P,
   Simple,
   CppBridge,
};

enum BDV_Action
{
   BDV_Init,
   BDV_NewBlock,
   BDV_ZC,
   BDV_Error,
   BDV_Progress,
   BDV_NodeStatus,
   BDV_Refresh
};

enum BDV_refresh
{
   BDV_dontRefresh,
   BDV_refreshSkipRescan,
   BDV_refreshAndRescan,
   BDV_filterChanged,
   BDV_registrationCompleted
};
