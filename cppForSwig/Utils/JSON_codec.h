////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdexcept>
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <sstream>

namespace JSON
{
   enum class StateEnum : int
   {
      Null,
      True,
      False
   };

   class Exception : public std::runtime_error
   {
   public:
      Exception(const std::string&);
   };

   ////////
   struct Value
   {
      virtual ~Value(void) = 0;
      virtual void serialize(std::ostream&) const = 0;
   };

   ////////
   struct String : Value
   {
      struct Comparator
      {
         using is_transparent = void;
         bool operator()(const String&, const String&) const;
         bool operator()(const String&, const std::string_view&) const;
         bool operator()(const std::string_view&, const String&) const;
         bool operator()(const String&, const std::string&) const;
         bool operator()(const std::string&, const String&) const;
      };

      const std::string val;

      String(const std::string_view&);

      void serialize(std::ostream&) const override;
      static String unserialize(std::istream&);
   };

   ////////
   struct Number : Value
   {
      const double val;

      Number(double);
      Number(int);
      Number(unsigned);

      void serialize(std::ostream&) const override;
      static Number unserialize(std::istream&);
   };

   ////////
   struct State : Value
   {
      const StateEnum state;

      State(StateEnum);

      void serialize(std::ostream&) const override;
      static State unserialize(std::istream&);
   };

   ////////
   struct Array : public Value
   {
      std::vector<std::shared_ptr<Value>> values;

      void append(const std::string&);
      void append(unsigned);
      void append(std::shared_ptr<Value>);

      void serialize(std::ostream&) const override;
      static Array unserialize(std::istream&);
   };

   ////////
   class Object : public Value
   {
   private:
      static int idCounter_;
      std::map<String, std::shared_ptr<Value>, String::Comparator> kvMap_;

   public:
      const int id;

      Object(void);
      Object(const std::string_view&, const std::string_view&);

      bool append(const std::string_view&, std::shared_ptr<Value>);
      bool append(const std::string_view&, const std::string_view&);
      bool append(const std::string&, Array&);
      bool append(const std::string&, float);
      bool append(const std::string&, int);

      void serialize(std::ostream&) const override;
      static Object unserialize(std::istream&);

      std::shared_ptr<Value> getValForKey(const std::string_view&) const;
      bool isResponseValid(int);
   };

   std::string encode(Object&);
   Object decode(const std::string&);
} //namespace JSON
