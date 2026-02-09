////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2024, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>

class BinaryDataRef;

namespace Armory
{
   namespace Bridge
   {
      class CppBridge;

      namespace ProtoCommandParser
      {
         bool processData(std::shared_ptr<CppBridge>, BinaryDataRef);
      }
   } //namespace Bridge
} //namespace Armory
