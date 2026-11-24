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
#include "Progress.h"

using namespace Armory;
using namespace Armory::BDMProgress;

////////////////////////////////////////////////////////////////////////////////
// Calculator
Calculator::Calculator(uint64_t total)
   : total_(total)
{
   init(0);
}

void Calculator::init(uint64_t to)
{
   auto now = std::chrono::high_resolution_clock::now();
   auto duration = now.time_since_epoch();
   then_ = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
   lastSample_ = to;
}

uint64_t Calculator::total() const
{
   return total_;
}

uint64_t Calculator::lastSample() const
{
   return lastSample_;
}

float Calculator::fractionCompleted() const 
{ 
   if (lastSample_ > total_) {
      return 1.0;
   }
   return lastSample_ / float(total_);
}

double Calculator::unitsPerSecond() const
{
   return avgSpeed_;
}

uint32_t Calculator::remainingSeconds() const
{
   if (lastSample_ > total_) {
      return UINT32_MAX;
   }
   return uint32_t(double(total_ - lastSample_) / unitsPerSecond());
}

void Calculator::advance(uint64_t to)
{
   static const double smoothingFactor=.10;
   if (to == lastSample_) {
      return;
   }

   auto sysclock = std::chrono::high_resolution_clock::now();
   auto duration = sysclock.time_since_epoch();
   auto now = std::chrono::duration_cast<std::chrono::milliseconds>(duration);

   if (now == then_) {
      return;
   }
   auto diff = now - then_;
   double doubleDiff = double(diff.count()) / 1000.0;
   double speed = double(to - lastSample_) / doubleDiff;

   if (lastSample_ == 0) {
      avgSpeed_ = speed;
   }
   lastSample_ = to;
   avgSpeed_ = smoothingFactor * speed + (1 - smoothingFactor) * avgSpeed_;
   then_ = now;
}

////////////////////////////////////////////////////////////////////////////////
// Tick
Tick::Tick(Phase ph) :
   phase{ph}, progress{0.0f}, etaSec{UINT32_MAX}
{}

Tick::Tick(Phase ph, const Calculator& calc) :
   phase{ph}, progress{calc.fractionCompleted()}, etaSec{calc.remainingSeconds()}
{}

Tick::Tick(Phase ph, float prg, uint32_t eta) :
   phase{ph}, progress{prg}, etaSec{eta}
{}