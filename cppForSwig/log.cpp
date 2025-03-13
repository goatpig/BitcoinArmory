////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "log.h"
#include <array>

using namespace std::string_view_literals;

////////////////////////////////////////////////////////////////////////////////
// DualStream
////////////////////////////////////////////////////////////////////////////////
void DualStream::enableStdOut(bool val)
{
   noStdout_ = !val;

   //uncomment to never disable stdout
   //noStdout_ = false;
}

////////////////////////////////////////////////////////////////////////////////
LogStream& DualStream::operator<<(std::string const & str)
{
   std::unique_lock<std::mutex> lock(mu_);
   if (!noStdout_) {
      std::cout << str.c_str();
   }
   if (fout_.is_open()) {
      fout_ << str.c_str();
   }
   return *this;
}

////////////////////////////////////////////////////////////////////////////////
void DualStream::setLogFile(const std::filesystem::path& path, size_t maxSz)
{
   path_ = path;
   truncateFile(path_, maxSz);
   fout_.open(path_, std::ios::app);
   fout_ << "\n\nLog file opened at " << NowTime() << ": " << path_ << std::endl;
}

////////////////////////////////////////////////////////////////////////////////
void DualStream::truncateFile(const std::filesystem::path& path,
   size_t maxSizeInBytes)
{
   std::ifstream is(path, std::ios::in|std::ios::binary);

   // If file does not exist, nothing to do
   if(!is.is_open()) {
      return;
   }

   // Check the filesize
   is.seekg(0, std::ios::end);
   unsigned long long int fsize = (size_t)is.tellg();
   is.close();

   if(fsize < maxSizeInBytes) {
      // If it's already smaller than max, we're done
      return;
   } else {
      // Otherwise, seek to <maxSize> before end of log file
      is.seekg(fsize - maxSizeInBytes);

      // Allocate buffer to hold the rest of the file (about maxSizeInBytes)
      unsigned long long int bytesToCopy = fsize - is.tellg();
      char* lastBytes = new char[(unsigned int)bytesToCopy];
      is.read(lastBytes, bytesToCopy);
      is.close();

      // Create temporary file and dump the bytes there
      auto tempfile = path.stem();
      tempfile += "temp"sv;
      tempfile += path.extension();
      std::ofstream os(tempfile, std::ios::out| std::ios::binary);
      os.write(lastBytes, bytesToCopy);
      os.close();
      delete[] lastBytes;

      // Remove the original and rename the temp file to original
      remove(path);
      rename(tempfile, path);
   }
}

////////////////////////////////////////////////////////////////////////////////
void DualStream::flush()
{
   std::unique_lock<std::mutex> lock(mu_);
   if (!noStdout_) {
      std::cout.flush();
   }
   if (fout_.is_open()) {
      fout_.flush();
   }
}

////////////////////////////////////////////////////////////////////////////////
void DualStream::close()
{
   std::unique_lock<std::mutex> lock(mu_);
   if (fout_.is_open()) {
      fout_.close();
   }
}

////////////////////////////////////////////////////////////////////////////////
bool DualStream::isOpen()
{
   std::unique_lock<std::mutex> lock(mu_);
   return fout_.is_open();
}

////////////////////////////////////////////////////////////////////////////////
const std::filesystem::path& DualStream::path(void) const
{
   return path_;
}

////////////////////////////////////////////////////////////////////////////////
// Log
////////////////////////////////////////////////////////////////////////////////
std::atomic<Log*> Log::theOneLog_ = { nullptr };
std::mutex Log::mu_;

////////////////////////////////////////////////////////////////////////////////
Log::Log() :
   isInitialized_(false), disableStdout_(false)
{}

////
Log::~Log()
{
   closeLogFile();
}

////////////////////////////////////////////////////////////////////////////////
Log& Log::getInstance(const std::filesystem::path& path)
{
   while (true) {
      //lock free check and return if instance is valid
      auto logPtr = theOneLog_.load(std::memory_order_acquire);
      if (logPtr != nullptr) {
         return *logPtr;
      }

      //lock and instantiate
      std::unique_lock<std::mutex> lock(mu_, std::defer_lock);
      if (!lock.try_lock()) {
         continue;
      }

      // Create a Log object
      Log* newLogPtr = new Log;

      // Open the filestream if available
      if (!path.empty()) {
         newLogPtr->ds_.setLogFile(path);
         newLogPtr->isInitialized_ = true;
      }

      theOneLog_.store(newLogPtr, std::memory_order_release);
      lock.unlock();
      return *newLogPtr;
   }
}

////////////////////////////////////////////////////////////////////////////////
LogStream& Log::get(LogLevel level)
{
   if(level > logLevel_) {
      return ns_;
   } else {
      return ds_;
   }
}

////////////////////////////////////////////////////////////////////////////////
void Log::setLogFile(const std::filesystem::path& path)
{
   getInstance(path);
}

////////////////////////////////////////////////////////////////////////////////
void Log::closeLogFile()
{
   getInstance().ds_.flush();
   getInstance().ds_ << "Closing logfile.\n";
   getInstance().ds_.close();
   getInstance().isInitialized_ = false;
   getInstance().logLevel_ = LogLvlDisabled;
}

////////////////////////////////////////////////////////////////////////////////
void Log::setLogLevel(LogLevel level)
{
   getInstance().logLevel_ = level;
}

void Log::suppressStdout(bool b)
{
   getInstance().ds_.enableStdOut(!b);
}

////////////////////////////////////////////////////////////////////////////////
const std::string_view& Log::toString(LogLevel level)
{
   static constexpr std::array<std::string_view, 9> buffer{
      "DISABLED"sv,
      "ERROR "sv,
      "WARN  "sv,
      "INFO  "sv,
      "DEBUG "sv,
      "DEBUG1"sv,
      "DEBUG2"sv,
      "DEBUG3"sv,
      "DEBUG4"sv
   };
   return buffer[level];
}

////////////////////////////////////////////////////////////////////////////////
bool Log::isOpen()
{
   return getInstance().ds_.isOpen();
}

////////////////////////////////////////////////////////////////////////////////
const std::filesystem::path& Log::filename()
{
   return getInstance().ds_.path();
}

////////////////////////////////////////////////////////////////////////////////
void Log::flush()
{
   getInstance().ds_.flush();
}

////////////////////////////////////////////////////////////////////////////////
void Log::cleanUp()
{
   std::unique_lock<std::mutex> lock(mu_, std::defer_lock);
   auto logPtr = theOneLog_.load(std::memory_order_acquire);
   if (logPtr == nullptr) {
      return;
   }
   theOneLog_.store(nullptr, std::memory_order_release);
   delete logPtr;
}
