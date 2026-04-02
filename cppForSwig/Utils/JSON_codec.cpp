////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

//TODO: replace with something like nlohmann json

#include <iostream>
#include <cstring>
#include "JSON_codec.h"

using namespace JSON;
using namespace std::string_view_literals;

Exception::Exception(const std::string& str) :
   std::runtime_error(str)
{}

////////////////////////////////////////////////////////////////////////////////
// Value
Value::~Value()
{}

////////////////////////////////////////////////////////////////////////////////
// String
String::String(const std::string_view& val) :
   val(val)
{}

////////
void String::serialize(std::ostream& s) const
{
   s << "\"" << val << "\"";
}

String String::unserialize(std::istream& s)
{
   auto val = s.get();
   if (val != '\"') {
      throw Exception("invalid string encapsulation");
   }

   std::string result;
   while (true) {
      std::string str;
      std::getline(s, str, '\"');
      if (s.rdstate() != std::ios_base::goodbit) {
         throw Exception("invalid string encapsulation");
      }
      result.append(str);

      //make sure that delimiting '\"' was no exited
      auto len = str.size();
      if (!str.empty() && str.c_str()[len - 1] != '\\') {
         break;
      }
      result.append("\"");
   }
   return String{result};
}

bool String::Comparator::operator()(
   const String& lhs, const String& rhs) const
{
   return lhs.val < rhs.val;
}

bool String::Comparator::operator()(
   const String& lhs, const std::string_view& rhs) const
{
   return lhs.val < rhs;
}

bool String::Comparator::operator()(
   const std::string_view& lhs, const String& rhs) const
{
   return lhs < rhs.val;
}

bool String::Comparator::operator()(
   const String& lhs, const std::string& rhs) const
{
   return lhs.val < rhs;
}

bool String::Comparator::operator()(
   const std::string& lhs, const String& rhs) const
{
   return lhs < rhs.val;
}

////////////////////////////////////////////////////////////////////////////////
// Number
Number::Number(double val) :
   val(val)
{}

Number::Number(int val) :
   val(double(val))
{}

Number::Number(unsigned val) :
   val(double(val))
{}

void Number::serialize(std::ostream& s) const
{
   s << val;
}

Number Number::unserialize(std::istream& s)
{
   double val;
   s >> val;
   return Number{val};
}

////////////////////////////////////////////////////////////////////////////////
// State
State::State(StateEnum s) :
   state(s)
{}

void State::serialize(std::ostream& s) const
{
   switch (state)
   {
      case StateEnum::Null:
         s << "null";
         break;

      case StateEnum::True:
         s << "true";
         break;

      case StateEnum::False:
         s << "false";
         break;
   }
}

State State::unserialize(std::istream& s)
{
   auto c = s.peek();
   switch (c)
   {
      case 'n':
      {
         char val_null[4];
         s.read(val_null, 4);
         if (std::memcmp(val_null, "null", 4) != 0) {
            throw Exception("invalid state");
         }
         return State{StateEnum::Null};
      }

      case 't':
      {
         char val_true[4];
         s.read(val_true, 4);
         if (std::memcmp(val_true, "true", 4) != 0) {
            throw Exception("invalid state");
         }
         return State{StateEnum::True};
      }

      case 'f':
      {
         char val_false[5];
         s.read(val_false, 5);
         if (std::memcmp(val_false, "false", 5) != 0) {
            throw Exception("invalid state");
         }
         return State{StateEnum::False};
      }

      default:
      {
         if (s.fail()) {
            s.clear();
         }
         throw Exception("unexpected state at deser");
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
// Array
void Array::append(const std::string& val)
{
   values.emplace_back(std::make_shared<String>(val));
}

void Array::append(unsigned val)
{
   values.emplace_back(std::make_shared<Number>(val));
}

void Array::append(std::shared_ptr<Value> valptr)
{
   values.emplace_back(valptr);
}

////////
void Array::serialize(std::ostream& s) const
{
   s << "[";
   if (!values.empty()) {
      auto iter = values.begin();
      while (true) {
         (*iter)->serialize(s);
         ++iter;
         if (iter == values.end()) {
            break;
         }
         s << ", ";
      }
   }
   s << "]";
}

Array Array::unserialize(std::istream& s)
{
   auto val = s.get();
   if (val != '[') {
      throw Exception("invalid array encapsulation");
   }

   Array result;
   while (s.rdstate() == std::ios_base::goodbit) {
      auto c = s.peek();
      switch (c)
      {
         case ' ':
         case ',':
         {
            s.get();
            continue;
         }

         case '\"':
         {
            auto strPtr = std::make_shared<String>(
               std::move(String::unserialize(s)));
            result.append(strPtr);
            break;
         }

         case '[':
         {
            auto arrayPtr = std::make_shared<Array>(
               std::move(Array::unserialize(s)));
            result.append(arrayPtr);
            break;
         }

         case ']':
         {
            s.get();
            return result;
         }

         case '{':
         {
            auto objPtr = std::make_shared<Object>(
               std::move(Object::unserialize(s)));
            result.append(objPtr);
            break;
         }

         case '0':
         case '1':
         case '2':
         case '3':
         case '4':
         case '5':
         case '6':
         case '7':
         case '8':
         case '9':
         case '-':
         {
            auto numPtr = std::make_shared<Number>(\
               std::move(Number::unserialize(s)));
            result.append(numPtr);
            break;
            break;
         }

         default:
            throw Exception("unexpected encapsulation");
      }
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
// Object
int Object::idCounter_ = 0;

Object::Object() :
   id(idCounter_++)
{
   if (idCounter_ > 10000) {
      idCounter_ = 0;
   }
}

Object::Object(const std::string_view& key, const std::string_view& val) :
   id(idCounter_++)
{
   if (idCounter_ > 10000) {
      idCounter_ = 0;
   }
   append(key, val);
}

////////
bool Object::append(const std::string_view& key, std::shared_ptr<Value> val)
{
   auto iter = kvMap_.emplace(String{key}, val);
   return iter.second;
}

bool Object::append(const std::string_view& key, const std::string_view& val)
{
   return append(key, std::make_shared<String>(val));
}

bool Object::append(const std::string& key, Array& val)
{
   return append(key, std::make_shared<Array>(std::move(val)));
}

bool Object::append(const std::string& key, float val)
{
   return append(key, std::make_shared<Number>(val));
}

bool Object::append(const std::string& key, int val)
{
   return append(key, std::make_shared<Number>(val));
}

////////
std::shared_ptr<Value> Object::getValForKey(const std::string_view& key) const
{
   auto pairIter = kvMap_.find(key);
   if (pairIter == kvMap_.end()) {
      return nullptr;
   }
   return pairIter->second;
}

bool Object::isResponseValid(int id)
{
   //check id
   auto idVal = getValForKey("id"sv);
   auto idObj = std::dynamic_pointer_cast<Number>(idVal);
   if (idObj == nullptr) {
      return false;
   }
   if (int(idObj->val) != id) {
      return false;
   }

   //check "error": null
   auto errorVal = getValForKey("error"sv);
   auto errorObj = std::dynamic_pointer_cast<State>(errorVal);

   if (errorObj == nullptr) {
      return true;
   }
   if (errorObj->state != StateEnum::Null) {
      return false;
   }
   return true;
}

////////
void Object::serialize(std::ostream& s) const
{
   s << "{";
   if (!kvMap_.empty()) {
      auto iter = kvMap_.begin();

      while (true) {
         iter->first.serialize(s);
         s << ": ";
         iter->second->serialize(s);

         ++iter;
         if (iter == kvMap_.end()) {
            break;
         }
         s << ", ";
      }
   }
   s << "}";
}

Object Object::unserialize(std::istream& s)
{
   auto val = s.get();
   if (val != '{') {
      throw Exception("invalid object encapsulation");
   }
   Object result;

   std::vector<std::string> keys;
   while (s.good()) {
      auto c = s.peek();
      switch (c)
      {
         case ' ':
         case ':':
         case ',':
         {
            s.get();
            continue;
         }

         case '\"':
         {
            auto jString = String::unserialize(s);
            if (keys.empty()) {
               keys.emplace_back(jString.val);
               break;
            }

            const auto& key = keys.back();
            result.append(key, std::make_shared<String>(std::move(jString)));
            keys.pop_back();
            break;
         }

         case '[':
         {
            if (keys.empty()) {
               throw Exception("missing object key");
            }

            auto jArray = Array::unserialize(s);
            auto key = keys.back();
            result.append(key, std::make_shared<Array>(std::move(jArray)));
            keys.pop_back();
            break;
         }

         case '{':
         {
            if (keys.empty()) {
               throw Exception("missing object key");
            }

            auto jObject = Object::unserialize(s);
            auto key = keys.back();
            result.append(key, std::make_shared<Object>(std::move(jObject)));
            keys.pop_back();
            break;
         }

         case '}':
         {
            s.get();
            return result;
         }

         case '0':
         case '1':
         case '2':
         case '3':
         case '4':
         case '5':
         case '6':
         case '7':
         case '8':
         case '9':
         case '-':
         {
            if (keys.empty()) {
               throw Exception("missing object key");
            }

            auto jNum = Number::unserialize(s);
            auto key = keys.back();
            result.append(key, std::make_shared<Number>(std::move(jNum)));
            keys.pop_back();
            break;
         }

         case 't':
         case 'n':
         case 'f':
         {
            if (keys.empty()) {
               throw Exception("missing object key");
            }

            auto jState = State::unserialize(s);
            auto key = keys.back();
            result.append(key, std::make_shared<State>(std::move(jState)));
            keys.pop_back();
            break;
         }

         default:
            throw Exception("unexpected encapsulation");
      }
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
std::string JSON::encode(Object& jObj)
{
   //make sure json_obj has jsonrpc, params and id key
   auto rpcVer = jObj.getValForKey("jsonrpc"sv);
   if (rpcVer == nullptr) {
      jObj.append("jsonrpc", "2.0");
   }

   auto params = jObj.getValForKey("params"sv);
   if (params == nullptr) {
      jObj.append("params", std::make_shared<Array>());
   }

   auto idPtr = jObj.getValForKey("id"sv);
   if (idPtr == nullptr) {
      jObj.append("id", jObj.id);
   }

   std::stringstream ss;
   jObj.serialize(ss);
   return ss.str();
}

Object JSON::decode(const std::string& str)
{
   std::stringstream ss(str);
   return Object::unserialize(ss);
}
