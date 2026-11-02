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

#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <future>
#include "bdmenums.h"

class BlockDataManager;

////////
class BlockDataManagerThread
{
private:
   struct BlockDataManagerThreadImpl
   {
      std::shared_ptr<BlockDataManager> bdm;
      BdmInitMode mode = BdmInitMode::RESUME;
      volatile bool run = false;
      bool failure = false;
      std::thread tID;
      std::promise<bool> readyPromise;
   };
   std::unique_ptr<BlockDataManagerThreadImpl> pimpl;

private:
   BlockDataManagerThread(const BlockDataManagerThread&);
   void run(void);

public:
   BlockDataManagerThread(void);
   ~BlockDataManagerThread(void);

   // start the BDM thread
   void start(BdmInitMode);
   std::shared_ptr<BlockDataManager> bdm(void);

   // return true if the caller should wait on callback notification
   bool shutdown(void);
   void join(void);

};
