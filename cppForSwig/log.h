////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
//
// This is a convenient little C++ logging class that was based on a Dr. Dobbs
// article on the subject.  The logger was rewritten to include a DualStream
// that pushes the log data to both std output AND file.  This could easily 
// be extended to use an arbitrary number of streams, with a different log lvl
// set on each one.   At the moment, it only supports stdout and one file 
// simultaneously at the same loglvl, though you can use LOGDISABLESTDOUT()
// to turn off the cout portion but still write to the log file.
//
// Usage:
//
// If you do not initialize the logger, the default behavior is to log nothing. 
// All LOGERR, LOGWARN, etc, calls wll execute without error, but they will be
// diverted to a NullStream object (which throws them away).  
//
// To use the logger, all you need to do is make one call to STARTLOGGING with
// the file name and log level, and then all subsequent calls to LOGERR, etc, 
// will work as expected.
//
//    STARTLOGGING("logfile.txt", LogLvlWarn); // ignore anything below LOGWARN
//
//    LOGERR   << "This is an error message, pretty much always logged";
//    LOGWARN  << "This is a warning";
//    LOGINFO  << "Given the LogLvlWarn above, this message will be ignored";
//    LOGDEBUG << "This one will also be ignored"
//
//    FLUSHLOG();          // force-flush all write buffers
//    LOGDISABLESTDOUT();  // Stop writing log msgs to cout, only write to file
//    LOGENABLESTDOUT();   // Okay nevermind, use cout again
//
// All logged lines begin with the msg type (ERROR, WARNING, etc), the current
// time down to one second, and the file:line.  Then the message is printed.
// Newlines are added automatically to the end of each line, so there is no 
// need to use "<< endl" at the end of any log messages (in fact, it will
// croak if you try to).  Here's what the messages look like:
//
//  -ERROR - 22:16:26: (code.cpp:129) I am recording an error!
//  -WARN  - 22:16:26: (code.cpp:130) This is just a warning, don't be alarmed!
//  -DEBUG4- 22:16:26: (code.cpp:131) A seriously low-level debug message.
//
// If you'd like to change the format of the messages, you can modify the 
// #define'd FILEANDLINE just below the #include's, and/or modify the 
// getLogStream() method in the LoggerObj class (just note, you cannot 
// move the __FILE__ and/or __LINE__ commands into the getLogStream() method
// because then it will always print "log.h:282" for the file and line).
//
////////////////////////////////////////////////////////////////////////////////
#ifndef __LOG_H__
#define __LOG_H__

#include <sstream>
#include <ctime>
#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <stdio.h>
#include <thread>
#include <mutex>
#include <memory>
#include <chrono>
#include <atomic>
#include "OS_TranslatePath.h"

#define FILEANDLINE "(" << __FILE__ << ":" << __LINE__ << ") "
#define LOGERR    (LoggerObj(LogLvlError ).getLogStream() << FILEANDLINE )
#define LOGWARN   (LoggerObj(LogLvlWarn  ).getLogStream() << FILEANDLINE )
#define LOGINFO   (LoggerObj(LogLvlInfo  ).getLogStream() << FILEANDLINE )
#define LOGDEBUG  (LoggerObj(LogLvlDebug ).getLogStream() << FILEANDLINE )
#define LOGDEBUG1 (LoggerObj(LogLvlDebug1).getLogStream() << FILEANDLINE )
#define LOGDEBUG2 (LoggerObj(LogLvlDebug2).getLogStream() << FILEANDLINE )
#define LOGDEBUG3 (LoggerObj(LogLvlDebug3).getLogStream() << FILEANDLINE )
#define LOGDEBUG4 (LoggerObj(LogLvlDebug4).getLogStream() << FILEANDLINE )
#define STARTLOGGING(LOGFILE, LOGLEVEL)         \
                  Log::SetLogFile(LOGFILE);     \
                  Log::SetLogLevel(LOGLEVEL);
#define LOGDISABLESTDOUT()  Log::SuppressStdout(true)
#define LOGENABLESTDOUT()   Log::SuppressStdout(false)
#define SETLOGLEVEL(LOGLVL) Log::SetLogLevel(LOGLVL)
#define FLUSHLOG()          Log::FlushStreams()
#define CLEANUPLOG()        Log::CleanUp()

#define MAX_LOG_FILE_SIZE (500*1024)

inline std::string NowTime();

typedef enum 
{
   LogLvlDisabled, 
   LogLvlError, 
   LogLvlWarn, 
   LogLvlInfo, 
   LogLvlDebug, 
   LogLvlDebug1, 
   LogLvlDebug2, 
   LogLvlDebug3, 
   LogLvlDebug4 
} LogLevel;


////////////////////////////////////////////////////////////////////////////////
class LogStream
{
public:
   virtual LogStream& operator<<(const char * str) = 0;
   virtual LogStream& operator<<(std::string const & str) = 0;
   virtual LogStream& operator<<(int i) = 0;
   virtual LogStream& operator<<(unsigned int i) = 0;
   virtual LogStream& operator<<(unsigned long long int i) = 0;
   virtual LogStream& operator<<(float f) = 0;
   virtual LogStream& operator<<(double d) = 0;
#if !defined(_MSC_VER) && !defined(__MINGW32__) && defined(__LP64__)
   virtual LogStream& operator<<(size_t i) = 0;
#endif
};

////////////////////////////////////////////////////////////////////////////////
class DualStream : public LogStream
{
public:
   DualStream(void) : noStdout_(false) 
   {}

   void enableStdOut(bool newbool) { noStdout_ = !newbool; }

   void setLogFile(std::string logfile, unsigned long long maxSz=MAX_LOG_FILE_SIZE)
   { 
      fname_ = logfile;
      truncateFile(fname_, maxSz);
      fout_.open(OS_TranslatePath(fname_.c_str()), std::ios::app); 
      fout_ << "\n\nLog file opened at " << NowTime() << ": " << fname_.c_str() << std::endl;
   }

   
   void truncateFile(std::string logfile, unsigned long long int maxSizeInBytes)
   {
      std::ifstream is(OS_TranslatePath(logfile.c_str()), std::ios::in|std::ios::binary);

      // If file does not exist, nothing to do
      if(!is.is_open())
         return;
   
      // Check the filesize
      is.seekg(0, std::ios::end);
      unsigned long long int fsize = (size_t)is.tellg();
      is.close();

      if(fsize < maxSizeInBytes)
      {
         // If it's already smaller than max, we're done
         return;
      }
      else
      {
         // Otherwise, seek to <maxSize> before end of log file
         is.seekg(fsize - maxSizeInBytes);

         // Allocate buffer to hold the rest of the file (about maxSizeInBytes)
         unsigned long long int bytesToCopy = fsize - is.tellg();
         char* lastBytes = new char[(unsigned int)bytesToCopy];
         is.read(lastBytes, bytesToCopy);
         is.close();
         
         // Create temporary file and dump the bytes there
         std::string tempfile = logfile + std::string("temp");
         std::ofstream os(OS_TranslatePath(tempfile.c_str()), std::ios::out| std::ios::binary);
         os.write(lastBytes, bytesToCopy);
         os.close();
         delete[] lastBytes;

         // Remove the original and rename the temp file to original
			#ifndef _MSC_VER
				remove(logfile.c_str());
				rename(tempfile.c_str(), logfile.c_str());
			#else
				_wunlink(OS_TranslatePath(logfile).c_str());
				_wrename(OS_TranslatePath(tempfile).c_str(), OS_TranslatePath(logfile).c_str());
			#endif
      }
   }

   LogStream& operator<<(const char * str)   { if(!noStdout_) std::cout << str;  if(fout_.is_open()) fout_ << str; return *this; }
   LogStream& operator<<(std::string const & str) { if(!noStdout_) std::cout << str.c_str(); if(fout_.is_open()) fout_ << str.c_str(); return *this; }
   LogStream& operator<<(int i)              { if(!noStdout_) std::cout << i;    if(fout_.is_open()) fout_ << i; return *this; }
   LogStream& operator<<(unsigned int i)     { if(!noStdout_) std::cout << i;    if(fout_.is_open()) fout_ << i; return *this; }
   LogStream& operator<<(unsigned long long int i) { if(!noStdout_) std::cout << i;    if(fout_.is_open()) fout_ << i; return *this; }
   LogStream& operator<<(float f)            { if(!noStdout_) std::cout << f;    if(fout_.is_open()) fout_ << f; return *this; }
   LogStream& operator<<(double d)           { if(!noStdout_) std::cout << d;    if(fout_.is_open()) fout_ << d; return *this; }
#if !defined(_MSC_VER) && !defined(__MINGW32__) && defined(__LP64__)
   LogStream& operator<<(size_t i)           { if(!noStdout_) std::cout << i;    if(fout_.is_open()) fout_ << i; return *this; }
#endif

   void FlushStreams(void) {std::cout.flush(); fout_.flush();}

   void newline(void) { *this << "\n"; }
   void close(void) { fout_.close(); }

   std::ofstream fout_;
   std::string   fname_;
   bool     noStdout_;
};

////////////////////////////////////////////////////////////////////////////////
class NullStream : public LogStream
{
public:
   LogStream& operator<<(const char*)              { return *this; }
   LogStream& operator<<(std::string const&)       { return *this; }
   LogStream& operator<<(int)                      { return *this; }
   LogStream& operator<<(unsigned int)             { return *this; }
   LogStream& operator<<(unsigned long long int)   { return *this; }
   LogStream& operator<<(float)                    { return *this; }
   LogStream& operator<<(double)                   { return *this; }
#if !defined(_MSC_VER) && !defined(__MINGW32__) && defined(__LP64__)
   LogStream& operator<<(size_t)                   { return *this; }
#endif

   void FlushStreams(void) {}
};

////////////////////////////////////////////////////////////////////////////////
class Log
{
public:
   Log(void) : isInitialized_(false), disableStdout_(false) {}

   static Log & GetInstance(const char * filename=nullptr)
   {
      while (true)
      {
         //lock free check and return if instance is valid
         auto logPtr = theOneLog_.load(std::memory_order_acquire);
         if (logPtr != nullptr)
            return *logPtr;

         //lock and instantiate
         std::unique_lock<std::mutex> lock(mu_, std::defer_lock);
         if (!lock.try_lock())
            continue;
    
         // Create a Log object
         Log* newLogPtr = new Log;
    
         // Open the filestream if available
         if (filename != nullptr)
         {
            newLogPtr->ds_.setLogFile(std::string(filename));
            newLogPtr->isInitialized_ = true;
         }

         theOneLog_.store(newLogPtr, std::memory_order_release);
         lock.unlock();

         return *newLogPtr;
      }
   }

   ~Log(void)
   {
      CloseLogFile();
   }

   LogStream& Get(LogLevel level = LogLvlInfo)
   {
      if((int)level > logLevel_ || !isInitialized_)
         return ns_;
      else 
         return ds_;
   }

   static void SetLogFile(std::string logfile) { GetInstance(logfile.c_str()); }
   static void CloseLogFile(void)
   { 
      GetInstance().ds_.FlushStreams();
      GetInstance().ds_ << "Closing logfile.\n";
      GetInstance().ds_.close();
      // This doesn't actually seem to stop the StdOut logging... not sure why yet
      GetInstance().isInitialized_ = false;
      GetInstance().logLevel_ = LogLvlDisabled;
   }

   static void SetLogLevel(LogLevel level) { GetInstance().logLevel_ = (int)level; }
   static void SuppressStdout(bool b=true) { GetInstance().ds_.enableStdOut(!b);}

   static std::string ToString(LogLevel level)
   {
	   static const char* const buffer[] = {"DISABLED", "ERROR ", "WARN  ", "INFO  ", "DEBUG ", "DEBUG1", "DEBUG2", "DEBUG3", "DEBUG4"};
      return buffer[level];
   }

    static bool isOpen(void) {return GetInstance().ds_.fout_.is_open();}
    static std::string filename(void) {return GetInstance().ds_.fname_;}
    static void FlushStreams(void) {GetInstance().ds_.FlushStreams();}

    static void CleanUp(void) { delete &GetInstance(); }

protected:
    DualStream ds_;
    NullStream ns_;
    int logLevel_ = LogLvlInfo;
    bool isInitialized_ = false;
    bool disableStdout_;
private:
    Log(const Log&);
    Log& operator =(const Log&);
    
private:
    static std::atomic<Log*> theOneLog_;
    static std::mutex mu_;

};

////////////////////////////////////////////////////////////////////////////////
class StreamBuffer : public LogStream
{
private:
   std::stringstream ss_;

public:
   StreamBuffer(void)
   {}

   LogStream& operator<<(const char * str) { ss_ << str; return *this; }
   LogStream& operator<<(std::string const & str) { ss_ << str.c_str(); return *this; }
   LogStream& operator<<(int i) { ss_ << i; return *this; }
   LogStream& operator<<(unsigned int i) { ss_ << i; return *this; }
   LogStream& operator<<(unsigned long long int i) { ss_ << i; return *this; }
   LogStream& operator<<(float f) { ss_ << f; return *this; }
   LogStream& operator<<(double d) { ss_ << d; return *this; }
#if !defined(_MSC_VER) && !defined(__MINGW32__) && defined(__LP64__)
   LogStream& operator<<(size_t i) { ss_ << i; return *this; }
#endif

   std::string str(void) { return ss_.str(); }
};


////////////////////////////////////////////////////////////////////////////////
class LoggerObj
{
private:
   StreamBuffer buffer_;

public:
   LoggerObj(LogLevel lvl) : logLevel_(lvl) 
   {}

   LogStream & getLogStream(void) 
   { 
      buffer_ << "-" << Log::ToString(logLevel_);
      buffer_ << "- " << NowTime() << ": ";
      return buffer_;
   }

   ~LoggerObj(void) 
   { 
      //terminate buffer with newline
      buffer_ << "\n";

      //push buffer to log stream
      LogStream & lg = Log::GetInstance().Get(logLevel_);
      lg << buffer_.str();

      //flush streams
      //Log::GetInstance().FlushStreams();
   }

private:
   LogLevel logLevel_;
};

inline std::string NowTime()
{
    // Getting current time in ms is way trickier than it should be.
   std::chrono::system_clock::time_point curTime = std::chrono::system_clock::now();
    std::chrono::system_clock::duration timeDur = curTime.time_since_epoch();
    timeDur -= std::chrono::duration_cast<std::chrono::seconds>(timeDur);
    unsigned int ms = static_cast<unsigned>(timeDur / std::chrono::milliseconds(1));

    // Print time. snprintf() causes warnings under clang and requires weird
    // workarounds to stop them. Just use stringstream, which is cleaner.
    time_t curTimeTT = std::chrono::system_clock::to_time_t(curTime);
    tm* tStruct = localtime(&curTimeTT);
    auto&& oss = std::ostringstream();
    oss << std::setw(4) << tStruct->tm_year + 1900 << "-" << std::setfill('0')
        << std::setw(2) << tStruct->tm_mon + 1 << "-" << tStruct->tm_mday
        << " - " << tStruct->tm_hour << ":" << tStruct->tm_min << ":"
        << tStruct->tm_sec << "." << std::setw(3) << ms;

    return oss.str();
}

#endif //__LOG_H__
