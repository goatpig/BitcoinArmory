////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <future>
#include "bdmenums.h"

struct BlockDataManagerConfig;
class BinaryData;
class BlockDataManager;

class BDM_CallBack
{
public:
   virtual ~BDM_CallBack(void);
   virtual void run(BDMAction, void*, int=0)=0;
   virtual void progress(
      BDMPhase,
      const std::vector<std::string>&,
      float, unsigned, unsigned)=0;
};

////////
class BlockDataManagerThread
{
   struct BlockDataManagerThreadImpl
   {
      std::shared_ptr<BlockDataManager> bdm;
      BdmInitMode mode = BdmInitMode::RESUME;
      volatile bool run = false;
      bool failure = false;
      std::thread tID;
   };
   std::unique_ptr<BlockDataManagerThreadImpl> pimpl;

public:
   BlockDataManagerThread(void);
   ~BlockDataManagerThread(void);

   // start the BDM thread
   void start(BdmInitMode);
   std::shared_ptr<BlockDataManager> bdm(void);

   // return true if the caller should wait on callback notification
   bool shutdown(void);
   void join(void);

private:
   void run(std::promise<void>);

private:
   BlockDataManagerThread(const BlockDataManagerThread&);
};
