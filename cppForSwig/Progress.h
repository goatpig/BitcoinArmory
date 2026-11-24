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

#include <cstdint>
#include <chrono>
#include <functional>

namespace Armory
{
   namespace BDMProgress
   {
      enum class Phase : int
      {
         LoadHeaders = 0,
         OrganizeHeaders,
         ParseNewHeaders,
         ParseNewBlocks,
         ScanBlocks,
         Completed
      };

      class Calculator
      {
         const uint64_t total_;
         std::chrono::milliseconds then_;
         uint64_t lastSample_ = 0;
         double avgSpeed_ = 0.0;

      public:
         Calculator(uint64_t);

         void init(uint64_t);
         void advance(uint64_t);

         uint64_t total(void) const;
         uint64_t lastSample(void) const;
         float fractionCompleted(void) const;
         double unitsPerSecond(void) const;
         uint32_t remainingSeconds(void) const;
      };

      struct Tick
      {
         const Phase phase;
         const float progress;
         const uint32_t etaSec;

         Tick(Phase);
         Tick(Phase, const Calculator&);
         Tick(Phase, float, uint32_t);
      };

      using ReportFunc = std::function<void(const Tick&)>;
   }
}
