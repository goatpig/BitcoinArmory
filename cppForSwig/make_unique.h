////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2018, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#ifndef _H_MAKE_UNIQUE
#define _H_MAKE_UNIQUE


#ifndef _WIN32
#include <memory>
#if __cplusplus < 201402L

template<typename T, typename... Args> std::unique_ptr<T> make_unique(Args&&... args)
{
   return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

#endif
#endif
#endif