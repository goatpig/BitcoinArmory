////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "log.h"

std::atomic<Log*> Log::theOneLog_ = { nullptr };
std::mutex Log::mu_;

void DualStream::enableStdOut(bool val)
{
   noStdout_ = !val;

   //uncomment to never disable stdout
   //noStdout_ = false;
}

////////////////////////////////////////////////////////////////////////////////
LogStream& DualStream::operator<<(const char * str)
{ 
   std::unique_lock<std::mutex> lock(mu_);
   if (!noStdout_) std::cout << str;  
   if (fout_.is_open()) fout_ << str; 
   return *this; 
}

////////////////////////////////////////////////////////////////////////////////
LogStream& DualStream::operator<<(std::string const & str)
{ 
   std::unique_lock<std::mutex> lock(mu_);
   if (!noStdout_) std::cout << str.c_str();
   if (fout_.is_open()) fout_ << str.c_str(); 
   return *this; 
}

////////////////////////////////////////////////////////////////////////////////
LogStream& DualStream::operator<<(int i)
{ 
   std::unique_lock<std::mutex> lock(mu_);
   if (!noStdout_) std::cout << i;
   if (fout_.is_open()) fout_ << i; 
   return *this; 
}

////////////////////////////////////////////////////////////////////////////////
LogStream& DualStream::operator<<(unsigned int i)
{ 
   std::unique_lock<std::mutex> lock(mu_);
   if (!noStdout_) std::cout << i;
   if (fout_.is_open()) fout_ << i; 
   return *this; 
}

////////////////////////////////////////////////////////////////////////////////
LogStream& DualStream::operator<<(unsigned long long int i)
{ 
   std::unique_lock<std::mutex> lock(mu_);
   if (!noStdout_) std::cout << i;
   if (fout_.is_open()) fout_ << i; 
   return *this; 
}

////////////////////////////////////////////////////////////////////////////////
LogStream& DualStream::operator<<(float f)
{ 
   std::unique_lock<std::mutex> lock(mu_);
   if (!noStdout_) std::cout << f;
   if (fout_.is_open()) fout_ << f; 
   return *this; 
}

////////////////////////////////////////////////////////////////////////////////
LogStream& DualStream::operator<<(double d)
{ 
   std::unique_lock<std::mutex> lock(mu_);
   if (!noStdout_) std::cout << d;
   if (fout_.is_open()) fout_ << d; 
   return *this; 
}

////////////////////////////////////////////////////////////////////////////////
#if !defined(_MSC_VER) && !defined(__MINGW32__) && defined(__LP64__)
LogStream& DualStream::operator<<(size_t i)
{ 
   std::unique_lock<std::mutex> lock(mu_);
   if (!noStdout_) std::cout << i;
   if (fout_.is_open()) fout_ << i; 
   return *this; 
}
#endif
