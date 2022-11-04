/*
  Morgan Stanley makes this available to you under the Apache License,
  Version 2.0 (the "License"). You may obtain a copy of the License at
 
       http://www.apache.org/licenses/LICENSE-2.0.
 
  See the NOTICE file distributed with this work for additional information
  regarding copyright ownership. Unless required by applicable law or agreed
  to in writing, software distributed under the License is distributed on an
  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions
  and limitations under the License.
*/
 
 #pragma once
/*
	Flats parser 

	parse() reads text representing Flat definitions from the istream is() and builds a vector<Field>.
	From that vector<Field>, we can then generate C++, Java, etc.

	The parser is recursive descent using a table of (name,Flat*) as a symbol table

	print() prints a collections of Type definitions to cout

	flats, enumerations, and variants can be used before they are defined

Example:
	Header : flat { n : int32 }
	Var : variant { i:int32 ; s:string }	// variants must be named
	E : enum { a:7 b; c }

	flat Mess {
		h : Header
		i : int32
		c : char[10]	// [10] indicates an array of 10 elements
		s : string
		var : Var
		opt : optional<string>	// < ... > indicates parameterization; a string is a vector<char>
		vec : vector < int32 >
		sicko : optional<vector<int32[10]>[20]>[30]
		var : Var
	}

	Semicolons as separators are optional.

	v : view of Mess	// view accessors to all Mess fields
	vv : view of Mess { var:Var i:int32 }	// view to subset of Mess fields; order can differ

	fundamental types are #included from preset_type.h

	the word "end" or end of file terminates parsing.

	error handling is primitive: write message and terminate.

	No need to optimize; this parser is not likely to be run often or as part performance critical code. And it's not slow.

*/

#include <string>
#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <cstdint>

template <class T>
using owner = T; // owner<X> indicates that X is responsible for deleting the X pointer to

enum class Type_id
{
  undefined,
  bad,
  flat,
  view,
  message,
  char8, // can't be called "char"
  int8,
  int16,
  int24,
  int32,
  int64,
  uint8,
  uint16,
  uint24,
  uint32,
  uint64,
  float32,
  float64,
  string,
  vector,
  array,
  optional,
  variant,
  enumeration, // array is fixed sized
  varray, // varray (also known as Fixed_vector) is an array that keep track of the number of elements used
  Preset = 100 // preset types gets their Type_ids starting here
};

//inline bool operator<=(Type_id a, Type_id b) { return int(a) <= int(b); }

enum class Status
{
  ordinary,
  deprecated,
  deleted,
  deprecating,
  deleting
};

struct Predef
{ // pre-defined types
  std::string name;
  std::string cpp_native_name;
  std::string java_native_name;
  std::string java_flat_name;
  int id; // to become Type_id
  int size; // sizeof
  int align; //alignment
};

struct Type;

struct Field
{
  std::string name;
  Type* typ = nullptr;
  int value = 0; // for enumerators
  int index = 0; // for ordinary fields  (the index is stable over all versions of a field)
  int offset = 0;
  int size = 0; // the number of bytes in the fixed part
  Status status = Status::ordinary;
};

struct Bad_variable_part
{
};

struct Variable_part
{ // to be initialized by the object_map generator
  int starting_offset;
  int next_offset;
  int max = 4 * 1024; // max message size. Magic number ???

  int allocate(int n)
  {
    int nx = next_offset;
    next_offset += n;
    if (max < next_offset)
      throw Bad_variable_part{};
    return nx;
  }
  // int deallocate();    // can't. Is it needed?
};

struct Flat
{
  Type_id id; // flat, view, variant, or enum
  std::string name;
  std::vector<Field> fields = {};
  Type* t = nullptr; // for a view or a message, the underlying Flat
  Variable_part var = {};
  bool used_as_optional = false;
  bool packed = false;
  struct Object_map* omap = nullptr;

  void push_back(const Field& fld) // add a field at end
  {
    fields.push_back(fld);
  }

  Field* find(const std::string& s) // find a field named s
  {
    for (auto& fld : fields)
      if (fld.name == s)
        return &fld;
    return nullptr;
  }
  int no_of_fields() const
  {
    return static_cast<int>(fields.size());
  } // just to shut up warning
};

struct Type
{
  std::string name;
  Type_id id;
  union
  { // a form of static polymorphism
    Flat* fl = nullptr; // for Flat, variant, or enum
    Type* t; // for vector, optional, and view
  };
  std::string cpp_native_name; // for native types
  std::string java_native_name; // for native types
  std::string java_flat_name;
  short count = 1; // for array
  int size = 0; // for calculating offsets
  int align = alignof(Flat); // assuming that every struct have the same alignment
  // int offset = 0;
  // int index;

  Type(Flat* flt) : name{flt->name}, id{flt->id}, fl{flt}
  {
  }
  Type(Type_id ii, Type* p) : id{ii}, t{p}
  {
  }
  Type(Type_id ii) : id{ii}
  {
  }
  Type(const std::string& n, Type_id id) : name{n}, id{id}
  {
  }
  Type(const Predef& p)
    : name{p.name},
      id{Type_id(p.id)},
      fl{nullptr},
      cpp_native_name{p.cpp_native_name},
      java_native_name{p.java_native_name},
      java_flat_name{p.java_flat_name},
      size{p.size},
      align{p.align}
  {
  }
};

[[noreturn]] void
  error(const std::string& s, const std::string& s2 = "", const std::string& s3 = ""); // print error and quit
[[noreturn]] void error(const std::string& s, int); // print error and quit

std::ostream& os(); // output
std::istream& is(); // input

std::vector<Flat*> parse(); // parser
void print(const Flat& flt); // print flats back out as text

enum Language
{
  debug,
  cpp,
  java
};

void print(const Type& t, Language = cpp, std::ostream& = std::cout); // print type* ???
std::string as_string(const Type& t, Language = cpp);
std::string as_string(int);
std::string as_string_cpp(const Type& t);
std::string as_string_java(const Type& t);
std::string as_string_java_flat(const Type& t);
void print_struct(const Flat& flt, std::ostream& out, bool packed);
