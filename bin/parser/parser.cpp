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

/*
	parser of flats.

	See documentation for details

	Uses "name : type" notation

	named types:
		v : variant { i:int32, f:float32 } 
		f : flat { m : int32 mv : v }
		e : enum { a:2 b:7 c d }
		vv : view of f
        v2 : view of f {m}

	unnamed types
		vector (size determined at construction time) and optional types are anonymous: vector<int32>
		optional: optional<int32>
		array (fixed-sized, designated by suffix): int32[number_of_elements]) 

	strings have their size determined at construction time. For fixed-sized strings use char[size]

	"???" indicates something that most likely will need to be changed
*/

#include "include/flats/flat_types.h" // needed to know the sizes of Flats types
#include "flat.h"

// application types. They don't really belong here, but we need their sizes for the object map
#include "application_types.h" 

using namespace std;

//---------------------------------
// simple error reporting

static int line_number = 0;

[[noreturn]] void error(const string& s, const string& s2, const string& s3)
{
  throw runtime_error("flats parser error: " + s + " " + s2 + " " + s3);
}

[[noreturn]] void error(const string& s, int x)
{
  throw runtime_error("flats parser error: " + s + " " + to_string(x));
}

//---------------------------------
//struct TimeStamp { long x, y;  };   // fake: should be included from "application source"

vector<Predef> predefined_types = {
// eventually "preset_types.h will be machine generated
#include "preset_types.h" // beware "magic built-in file name" ???
};

struct Table
{ // symbol table type;
  map<string, owner<Type*>> entries; // it owns the types pointed to

  Table(vector<Predef>& pre) // pre-load with predefined types
  {
    for (auto& p : pre)
    { // presumably redundant basic (in)sanity checks:
      if (p.name == "")
        error("name missing in predefined type");
      if (p.cpp_native_name == "")
        error("native C++ name missing in predefined type");
      if (p.java_native_name == "")
        error("native Java name missing in predefined type");
      if (Type_id(p.id) <= Type_id::flat)
        error("bad Type_id");
      if (p.size <= 0)
        error("negative size");
      if (p.align <= 0)
        error("negative alignment");
      if (find(p.name))
        error("repeated fundamental type name ", p.name);

      entries[p.name] = new Type{p};
    }
  }

  ~Table()
  {
    for (auto& [name, p] : entries)
      delete p; // C++17
  }

  Type* find(const string& n)
  {
    auto e = entries.find(n); // just look for n (C++17)
    return (e == entries.end()) ? nullptr : e->second;
  }

  Type* insert(Flat* flt)
  {
    owner<Type*> t = new Type{flt};
    entries[flt->name] = t;
    return t;
  }

  auto begin()
  {
    return entries.begin();
  }
  auto end()
  {
    return entries.end();
  }

  int size()
  {
    return static_cast<int>(entries.size());
  } // bah, humbug!
};

//----------------------------------------
// Global objects:

Table symbol_table{predefined_types};

vector<Flat*> flats; // Flats in declaration order

//----------------------------------------
// lexer:

void put_back()
{
  is().unget();
}

char get_char();

bool is_char(char x) // consumes the next character
{
  char ch = get_char();
  return ch == x;
}

void eat_block_comment()
// we have seen the initial /*
{
  char ch;
  while (is().get(ch))
    while (ch == '*')
      if (is_char('/'))
        return;
}

void eat_line_comment()
{
  char ch;
  while (is().get(ch) && ch != '\n')
    ; // do nothing
  ++line_number;
}

char get_char() // return first non-whitespace char; whitespace includes commments
{
  char ch;
  while (is().get(ch))
  {
    switch (ch)
    {
      case '\n':
        ++line_number;
        continue;
      case '/':
        if (is_char('/'))
        {
          eat_line_comment();
          continue;
        }
        else
          put_back();
        if (is_char('*'))
        {
          eat_block_comment();
          continue;
        }
        else
          put_back();
        return '/';
      default:
        if (iswspace(ch))
          continue;
        return ch;
    }
  }

  error("unexpected end of input");
}

void eat_terminator() // optional terminator: semicolon or comma
{
  if (!is_char(';'))
    put_back();
  if (!is_char(','))
    put_back();
}

string get_name()
// a name is composed of letters, digits, and underscores
// at least one character, the first character must not be an underscore or a digit
{
  string s;
  char ch = get_char();
  if (!(isalpha(ch) || ch == '_'))
    error("letter or undescore expected in name");
  s += ch;
  while (is().get(ch) && (isalnum(ch) || ch == '_'))
    s += ch;
  is().unget();
  return s;
}

//----------------------------------------------------------------------------
//parser:

Type* get_type(Type_id id);

Type* get_opt_or_vec(Type_id id) // <T>
{
  if (!is_char('<'))
    error("'<' expected after ", "vector or optional");
  auto t = get_type(id);
  if (!is_char('>'))
    error("'>' expected after ", "vector or optional");
  if (id == Type_id::optional)
    switch (t->id)
    {
      case Type_id::optional:
      case Type_id::variant:
      case Type_id::vector:
      case Type_id::string: // optional eliminated
        return t;
      case Type_id::flat:
        t->fl->used_as_optional = true;
        break;
      default: // To silence warnings
        break;
    }
  if (id == Type_id::vector && t->id == Type_id::variant)
    error("vector of variant is not supported");
  return new Type{id, t};
}

int get_number();

Type* get_varray() // <T,N>
{
  if (!is_char('<'))
    error("'<' expected after 'fixed_vector'");
  auto t = get_type(Type_id::varray);
  if (!is_char(','))
    error("',' expected after type in fixed_vector");
  int sz = get_number();
  if (sz < 1)
    error("fixed_array needs positive number of elements", sz);
  if (!is_char('>'))
    error("'>' expected after size in fixed_vector");
  if (t->id == Type_id::variant)
    error("fixed_vector of variant is not supported");

  auto tt = new Type{Type_id::varray, t};
  tt->count = sz;
  return tt;
}

int enum_value(const string& en, const string& n)
// en::n
{
  Type* t = symbol_table.find(en);
  if (t == nullptr)
    error("undefined enum (qualifier not found)", en);
  if (t->fl == nullptr)
    error("undefined enum (qualifier not defined)", en);
  Field* fld = t->fl->find(n);
  if (fld == nullptr)
    error("undefined enum in", en);
  return fld->value;
}

int get_number() // interger literal or enumerator (no general expression)
{
  char ch = get_char();
  if (isdigit(ch))
  { // number
    put_back();
    int n;
    is() >> n;
    return n;
  }
  if (isalpha(ch))
  { // enumerator: E::x
    put_back();
    auto enum_name = get_name();
    if (!(is_char(':') && is_char(':')))
      error(":: expected");
    auto name = get_name();
    return enum_value(enum_name, name);
  }
  error("number expected");
}

int get_count()
// number ']'
{
  int n = get_number();
  if (n < 1)
    error("non-positive array count");
  if (!is_char(']'))
    error("']' expected after array count");
  return n;
}

Type* get_type(Type_id id)
// name | optional<T> | vector<T> | fixed_vector<T> | variant<...> all with an optional [n] suffix
// e.g., optional<int32>[10] represented as Array<Optional<int32>,10>
{
  // odd: a field owns its vector or optional type, but not its flat or variant type
  // Core Guidelines violation ???

  string s = get_name();
  Type* t = nullptr; // in case additional information is needed
  if (s == "optional")
  {
    t = get_opt_or_vec(Type_id::optional); // may optimize away "optional"
  }
  else if (s == "vector")
  {
    t = get_opt_or_vec(Type_id::vector);
  }
  else if (s == "fixed_vector")
  {
    t = get_varray();
  }
  else if (s == "string")
  {
    t = symbol_table.find("string");
  }
  else
  {
    t = symbol_table.find(s); // just look for s (C++17)
    if (t && t->id == Type_id::undefined && id == Type_id::flat)
      error("recursive definition of flat", s);
    if (!t)
    { // Oops: not yet defined
      if (id == Type_id::flat)
        error(s, "is undefined type in flat");
      Flat* flt = new Flat{Type_id::undefined, s};
      t = symbol_table.insert(flt); // for lookup; symbol_table now owns flt
    }
  }

  switch (t->id)
  { // beware of alignment
    case Type_id::vector:
    case Type_id::string:
      t->size = sizeof(Flats::Vector<char>); // all Vectors are of the same size
      break;
    case Type_id::optional:
      //		t->size = t->t->align + t->size;
      t->size = ((t->id == Type_id::char8) ? sizeof(Flats::Size) : t->t->align) +
        t->t->size;
      break;
    case Type_id::varray:
      t->size = ((t->id == Type_id::char8) ? sizeof(Flats::Size) : t->t->align) +
        t->count * t->t->size;
      break;
    default:; // suppress spurious warning
  }

  while (is_char('['))
  {
    t = new Type{Type_id::array, t};
    t->count = get_count();
    t->size = t->count * t->t->size;
  }
  put_back();

  return t;
}

Field modify_field(Flat* flt, Status s)
{
  string n = get_name();
  auto e = flt->find(n);
  if (!e)
    error(s == Status::deprecated ? "deprecated type not found" : "deleted type not found", n);
  e->status = s;

  // make a deleting or deprecating field
  Field fld{n, nullptr}; // no type!
  fld.status = (s == Status::deprecating ? Status::deprecated : Status::deleting);
  return fld;
}

Field get_field(Flat* flt, Type_id id)
{
  string n = get_name();
  if (n == "deprecate")
    return modify_field(flt, Status::deprecated); // make a deprecating field
  if (n == "delete")
    return modify_field(flt, Status::deleted); // make a deleting field
  if (flt->find(n))
    error("member defined twice", n);
  if (!is_char(':'))
    error("colon missing after member name", n);
  auto t = get_type(id);
  if (!t)
    error("internal error: very weird ", n);
  eat_terminator();
  return {n, t};
}

owner<Flat*> get_flat(const string& n, Type_id id) // 'flat' name already seen:'{' members '}'
{
  //	cerr << "get_flat(): " << n << '\n';
  owner<Flat*> flt = new Flat{id, n};
  if (!is_char('{'))
    error("'{' expected");
  while (!is_char('}'))
  {
    put_back();
    auto fld = get_field(flt, id);
    fld.index = flt->no_of_fields();
    flt->push_back(fld);
  }
  return flt;
}

Flat* get_view(const string& n)
/*
	already seen: n ':' 'view'
	expected: 'of' name '{' members '}' or just 'of' name
	name is the nameof the flat that the view is a view of
*/
{
  //	cerr << "get_view(): " << n << '\n';
  auto oo = get_name();
  if (oo != "of")
    error("'of' expected");
  auto fn = get_name();
  auto t = symbol_table.find(fn);
  if (t == nullptr || t->id != Type_id::flat)
    error(n, " flat definition not found");
  Flat* flt = new Flat{Type_id::view, n}; // the view

  if (is_char('{'))
  { // saw '{' partial view:
    while (!is_char('}'))
    {
      put_back();
      auto n = get_name();
      eat_terminator();
      Field* fld = t->fl->find(n);
      if (fld == nullptr)
        error(n, "is not a member of", fn);
      flt->push_back(Field{n, fld->typ});
    }
  }
  else
  { // complete view
    put_back();
    flt->t = t;
  }
  return flt;
}

void check_for_undefined()
{
  int nerr = 0;
  for (auto& p : symbol_table)
    if (p.second->id == Type_id::undefined)
    {
      ++nerr;
    }

  if (nerr)
    error("undefined variants or flats");
}

Field get_enumerator(Flat* flt)
{
  auto n = get_name();
  int x = 0;
  if (is_char(':'))
    x = get_number();
  else
  {
    put_back();
    x = (flt->fields.size()) ? flt->fields.back().value + 1 : 0; // C++default value rule
  }
  eat_terminator();
  return {n, nullptr, x};
}

Flat* get_enumeration(const string& n)
{
  Flat* flt = new Flat{Type_id::enumeration, n};
  if (!is_char('{'))
    error("'{' expected");
  while (!is_char('}'))
  {
    put_back();
    auto fld = get_enumerator(flt);
    flt->push_back(fld);
  }
  return flt;
}

Flat* get_message(const string& n)
///// m : message of flat_nam
{
  //    std::cerr << "get_message(): " << n << '\n';
  auto oo = get_name();
  if (oo != "of")
    error("'of' expected");
  auto fn = get_name();
  auto t = symbol_table.find(fn);
  if (t == nullptr || t->id != Type_id::flat)
    error(n, " flat definition not found");
  Flat* flt = new Flat{Type_id::message, n};
  flt->t = t;
  return flt;
}
/*
Flat* get_alias(const string&)
{
  return nullptr;
}
*/
vector<Flat*> parse()
{
  while (is())
  {
    string n = get_name();
    if (n == "end")
      break;

    auto t = symbol_table.find(n);
    if (t && t->id != Type_id::undefined)
      error(n, " defined twice");
    if (!t)
      t = symbol_table.insert(new Flat{Type_id::undefined, n}); // symbol_table owns the flat
    // here, t denotes an undefined type

    if (!is_char(':'))
      error("colon missing after global name", n);
    string s = get_name();
    owner<Flat*> flt = (s == "flat") ? get_flat(n, Type_id::flat)
      : (s == "view")                ? get_view(n)
      : (s == "variant")             ? get_flat(n, Type_id::variant)
      : (s == "enum")                ? get_enumeration(n)
      : (s == "message")             ? get_message(n)
    //  : (s == "alias")               ? get_alias(n)
      : (error("unexpected:", s, "at start of declaration"), nullptr);
    eat_terminator();

    // replace undefined type:
    delete t->fl;
    t->id = flt->id;
    t->fl = flt; // transfer ownership

    if (s != "message")
      flt->t = t;

    flats.push_back(flt); // for order
  }

  check_for_undefined();
  return flats;
}
