////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2024, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _PROTO_COMMAND_PARSER_H
#define _PROTO_COMMAND_PARSER_H

#include "BinaryData.h"

namespace Armory
{
   namespace Bridge
   {
      class CppBridge;
      namespace ProtoCommandParser
      {
         bool processData(std::shared_ptr<CppBridge>, BinaryDataRef);
      }
   }; //namespace Bridge
}; //namespace Armory

#endif