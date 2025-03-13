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
#include <stdio.h>
#include <thread>
#include <mutex>
#include <memory>
#include <chrono>
#include <atomic>
#include <filesystem>

#define FILEANDLINE "(" << __FILE__ << ":" << __LINE__ << ") "
#define LOGERR    (LoggerObj(LogLvlError ).getLogStream() << FILEANDLINE )
#define LOGWARN   (LoggerObj(LogLvlWarn  ).getLogStream() << FILEANDLINE )
#define LOGINFO   (LoggerObj(LogLvlInfo  ).getLogStream() << FILEANDLINE )
#define LOGDEBUG  (LoggerObj(LogLvlDebug ).getLogStream() << FILEANDLINE )
#define LOGDEBUG1 (LoggerObj(LogLvlDebug1).getLogStream() << FILEANDLINE )
#define LOGDEBUG2 (LoggerObj(LogLvlDebug2).getLogStream() << FILEANDLINE )
#define LOGDEBUG3 (LoggerObj(LogLvlDebug3).getLogStream() << FILEANDLINE )
#define LOGDEBUG4 (LoggerObj(LogLvlDebug4).getLogStream() << FILEANDLINE )
#define STARTLOGGING(LOGFILE, LOGLEVEL) \
   Log::setLogFile(LOGFILE); \
   Log::setLogLevel(LOGLEVEL);
#define LOGDISABLESTDOUT()  Log::suppressStdout(true)
#define LOGENABLESTDOUT()   Log::suppressStdout(false)
#define SETLOGLEVEL(LOGLVL) Log::setLogLevel(LOGLVL)
#define FLUSHLOG()          Log::flush()
#define CLEANUPLOG()        Log::cleanUp()

#define LOGTIMEBUFLEN 30
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
   virtual LogStream& operator<<(std::string const & str) = 0;
};

////////////////////////////////////////////////////////////////////////////////
class DualStream : public LogStream
{
private:
   std::ofstream           fout_;
   std::filesystem::path   path_;
   bool                    noStdout_;
   std::mutex              mu_;

public:
   DualStream(void) : noStdout_(false)
   {}

   void enableStdOut(bool);
   void setLogFile(const std::filesystem::path&, size_t maxSz=MAX_LOG_FILE_SIZE);
   void truncateFile(const std::filesystem::path&, size_t);
   void flush(void);
   void close(void);
   bool isOpen(void);
   const std::filesystem::path& path(void) const;

   LogStream& operator<<(std::string const & str) override;
};

////////////////////////////////////////////////////////////////////////////////
class NullStream : public LogStream
{
public:
   LogStream& operator<<(std::string const &) override { return *this; }
};

////////////////////////////////////////////////////////////////////////////////
class Log
{
private:
   Log(const Log&);
   Log& operator=(const Log&);

public:
   Log(void);
   ~Log(void);

   LogStream& get(LogLevel level=LogLvlInfo);

   static Log& getInstance(const std::filesystem::path& path={});
   static void setLogFile(const std::filesystem::path&);
   static void closeLogFile(void);
   static void setLogLevel(LogLevel);
   static void suppressStdout(bool b=true);
   static const std::string_view& toString(LogLevel);

   static bool isOpen(void);
   static const std::filesystem::path& filename(void);
   static void flush(void);
   static void cleanUp(void);

protected:
   DualStream ds_;
   NullStream ns_;
   LogLevel logLevel_ = LogLvlInfo;
   bool isInitialized_ = false;
   bool disableStdout_;

private:
   static std::atomic<Log*> theOneLog_;
   static std::mutex mu_;
};

////////////////////////////////////////////////////////////////////////////////
class LoggerObj
{
private:
   std::stringstream buffer_;
   LogLevel logLevel_;

public:
   LoggerObj(LogLevel lvl) : logLevel_(lvl)
   {}

   std::stringstream& getLogStream(void)
   {
      buffer_ << "-" << Log::toString(logLevel_);
      buffer_ << "- " << NowTime() << ": ";
      return buffer_;
   }

   ~LoggerObj(void)
   { 
      //terminate buffer with newline
      buffer_ << "\n";

      //push buffer to log stream
      LogStream & lg = Log::getInstance().get(logLevel_);
      lg << buffer_.str();
   }
};

inline std::string NowTime()
{
    // Getting current time in ms is way trickier than it should be.
   std::chrono::system_clock::time_point curTime = std::chrono::system_clock::now();
    std::chrono::system_clock::duration timeDur = curTime.time_since_epoch();
    timeDur -= std::chrono::duration_cast<std::chrono::seconds>(timeDur);
    unsigned int ms = static_cast<unsigned>(timeDur / std::chrono::milliseconds(1));

    // Print time.
    time_t curTimeTT = std::chrono::system_clock::to_time_t(curTime);
    tm* tStruct = localtime(&curTimeTT);
    std::string timeStr = "%04i-%02i-%02i - %02i:%02i:%02i.%03i";
    char result[LOGTIMEBUFLEN] = {0};
    snprintf(result, sizeof(result), timeStr.c_str(), tStruct->tm_year + 1900, \
                                                      tStruct->tm_mon + 1, \
                                                      tStruct->tm_mday, \
                                                      tStruct->tm_hour, \
                                                      tStruct->tm_min, \
                                                      tStruct->tm_sec, \
                                                      ms);
    return result;
}

#endif //__LOG_H__
