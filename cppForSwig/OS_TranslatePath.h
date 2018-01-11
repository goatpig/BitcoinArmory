////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or https://www.gnu.org/licenses/agpl.html                 //
//                                                                            //
//  Copyright (C) 2016-2018, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
//Defines path translation for unicode support. Currently only active in Windows

#ifndef _MSC_VER
	#define OS_TranslatePath(X) X
#else
	#include "./Win_TranslatePath.h"
	#define OS_TranslatePath Win_TranslatePath
#endif
