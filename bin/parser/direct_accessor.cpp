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
	simple accessor generator.

    It takes in a vector<Flat> from the parser and writes C++ for accessing messages and flats to output.

    For example:

	input:

		Message : flat { x : int32 s : string }

        M : message of Message

	output:

    struct Message {
        int32_t x;
        String s;   // String is a Flat's type, not std::string
    };

	struct Message_direct {
	    Message* mbuf;
	    Allocator* allo;
	    Message_direct(Message* pp, Allocator* m) : mbuf{ pp },  allo{ m }, {}

        // accessors:

		int& x() { return mbuf->x; }         // for direct read/write manipulation
		Span<char> s() { return mbuf->s; }  // a Strings is presented to users as a Span

        // initializers

        void x(int32_t arg) { new(&mbuf->x) int32_t{arg}; } // place arg in x
        void s(std::string arg) { new(&mbuf->s) String{arg}; }  // place the characters from arg in S (allocated in the tail)
	};
    
    struct M { // M : message of Message
        // ...
    };

	Message* place_M(Byte* buf, int size_of_buffer, int size_of_tail)
    {
        return new(buf) M{size_of_buffer,size_of_tail};
    }
 
    Some functions write directly to an out argument; others compose a string to be output.
    The forment was how it started, but the latter seems simpler.
*/

#include "flat.h"
#include "object_map.h"
using namespace std;

// run-time checks for initialization being done can be inserted into code

constexpr bool initialize_check = false;
constexpr bool default_init = true; // false only for testing

static void print_member(const Field& m, std::ostream& out)
{
  out << "   " << as_string_cpp(*m.typ) << " " << m.name << ";\n";
}

void close_struct(ostream& out, bool /*packed*/)
{
  //   if (packed)
  //		out << "} __attribute__((packed));\n\n"; // GCC: non-standard
  //   else
  out << "};\n";
}

void print_variant(const Flat& flt, std::ostream& out, bool packed = false)
/*
    A bit like cross between Vector and Optional generated on a per-variant basis

    struct XXX {
        char utag = 0;	                // which variant is set; 0 means uninitialized
        Offset pos;                     // offset of selected variant
        union U { A a; B b; F f};       // union of alternatives
        // constructors
        // accessors
   };
*/
{
  out << "struct " << flt.name << " {\n";
  out << "   char utag = 0;\n   Offset pos = 0;\n   union U {\n";
  for (auto m : flt.fields)
    print_member(m, out);
  close_struct(out, packed);

  out << "   // constructors:\n";
  out << "   " << flt.name << "() = default;\n";
  int count = 1;
  for (auto m : flt.fields)
  {
    switch (m.typ->id)
    {
      case Type_id::string: // std::string and C-style string initializers
        out << "   " << flt.name << "(Allocator* allo, const char* arg)\n";
        out << "      :utag{" << count << "}, pos{allo->allocate(sizeof(String))}\n";
        out << "   {\n";
        out << "      pos -= reinterpret_cast<Byte*>(this) - allo->flat();		// position relative to this\n";
        out << "      auto p = &reinterpret_cast<" << flt.name
            << "::U*>(reinterpret_cast<Byte*>(this) + pos)->" << m.name << ";\n";
        out << "      auto r = allo->place(arg);\n";
        out << "      p->pos = size_of<String>(); // characters follow immediately\n";
        out << "      p->sz = r.sz;\n";
        out << "   }\n";

        out << "   " << flt.name << "(Allocator* allo, const std::string& arg)\n";
        out << "      :utag{" << count << "}, pos{allo->allocate(sizeof(String))}\n";
        out << "   {\n";
        out << "      pos -= reinterpret_cast<Byte*>(this) - allo->flat();		// position relative to this\n";
        out << "      auto p = &reinterpret_cast<" << flt.name
            << "::U*>(reinterpret_cast<Byte*>(this) + pos)->" << m.name << "; \n";
        out << "      p->pos = size_of<String>(); // characters follow immediately\n";
        out << "      p->sz = size_of(arg);\n";
        out << "      allo->allocate(arg.size());\n";
        out << "      Byte* q = reinterpret_cast<Byte*>(p)+size_of<String>();\n";
        out << "      for (auto x : arg) *q++ = Byte(x);\n";
        out << "   }\n";
        break;
      case Type_id::flat: // default constructor with allocator ???
        break;
      default:
        out << "   " << flt.name << "(Allocator* allo," << as_string_cpp(*m.typ)
            << " arg)\n";
        out << "      :utag{" << count << "}, pos{ allo->allocate(sizeof("
            << as_string_cpp(*m.typ) << ")) }\n";
        out << "      { reinterpret_cast<" << flt.name
            << "::U*>(reinterpret_cast<Byte*>(this)+pos)->" << m.name
            << " = arg; }\n";
    }
    ++count;
  }

  out << "   auto tag() { return utag; }\n";
  out << "   bool is_present() { return utag; }\n"; // for consistent use

  out << "\n   // variant accessors:\n";
  count = 1;
  for (auto m : flt.fields)
  {
    switch (m.typ->id)
    {
      case Type_id::string:
        out << "   Span<char> " << m.name << "()\n";
        out << "   {\n";
        out << "      expect([&] { return utag ==" << count
            << ";}, Error_code::variant_tag);\n";
        out << "      auto p = &reinterpret_cast<" << flt.name
            << "::U*>(reinterpret_cast<Byte*>(this) + pos)->" << m.name << ";\n";
        out << "      return {p->begin(), p->end()};\n";
        out << "   }\n";
        break;
      case Type_id::vector:
      case Type_id::array:
      case Type_id::varray:
        out << "   Span<";
        print(*m.typ->t, Language::cpp, out);
        out << "> " << m.name << "()\n";
        out << "   {\n";
        out << "      expect([&] { return utag ==" << count
            << ";}, Error_code::variant_tag);\n";
        out << "      auto p = &reinterpret_cast<" << flt.name
            << "::U*>(reinterpret_cast<Byte*>(this) + pos)->" << m.name << ";\n";
        out << "      return {p->begin(), p->end()};\n";
        out << "   }\n";
        break;
      case Type_id::variant: // needs and allocator to return a direct accessor
      case Type_id::flat: // needs and allocator to return a direct accessor
        out << "   " << m.typ->name << "_direct " << m.name << "(Allocator* a)\n";
        out << "   {\n";
        out << "      expect([&] { return utag ==" << count
            << ";}, Error_code::variant_tag);\n";
        out << "      auto p = &reinterpret_cast<" << flt.name
            << "::U*>(reinterpret_cast<Byte*>(this) + pos)->" << m.name << ";\n";
        out << "      return {p,a};\n";
        out << "   }\n";
        break;
      default:
        out << "   ";
        print(*m.typ, Language::cpp, out);
        out << "& ";
        out << m.name << "() { expect([&]{ return utag==" << count
            << ";  }, Error_code::variant_tag);"
               "return reinterpret_cast<"
            << flt.name << "::U*>(reinterpret_cast<Byte*>(this)+pos)->"
            << m.name << "; }\n";
    }
    ++count;
  }
  out << "};\n";
}

void print_struct(const Flat& flt, std::ostream& out, bool packed)
// the C++ struct defining the layout of a Flat
{
  switch (flt.id)
  {
    case Type_id::variant:
    case Type_id::enumeration:
    case Type_id::message:
      return;
    default:
      break;
  }
  out << "\n\n// struct (memory layout):\n";
  out << "struct " << flt.name << "{\n";
  //   out << "   using value_type = void;\n";  // dummy to bypass compiler problem     ???
  out << "   " << flt.name << "(){}\n"; // default constructor

  for (auto m : flt.fields)
    print_member(m, out);
  close_struct(out, packed); // just in case we need special treatment of packed structs; otherwise just "};"
}

string as_string_accessor(const Type& t, Language = Language::cpp)
{
  switch (t.id)
  {
    case Type_id::string:
      return "Span<char> ";
    case Type_id::vector:
    case Type_id::array:
    case Type_id::varray:
    {
      string tt = as_string(*t.t);
      if (t.t->id == Type_id::flat)
        return "Span_ref<" + tt + ", " + tt + "_direct> ";
      else
        return "Span<" + tt + "> ";
    }
    case Type_id::variant:
      if (t.t->id == Type_id::string)
        return "Span_ref<char> ";
      // [[fallthrough]]
      return as_string(t) + "& ";
    default:
      return as_string(t) + "& ";
  }
}

void print_accessor_type(const Type& t, Language lang, ostream& out)
{
  out << as_string_accessor(t, lang);
}

string as_string_initializer_type(const Type& t, Language lang);

string as_string_initializer_element(const Type& t, Language lang)
{
  return (t.id == Type_id::string) ? "std::string"
                                   : as_string_initializer_type(t, lang);
}


string as_string_initializer_type(const Type& t, Language lang = Language::cpp)
{
  switch (t.id)
  {
    case Type_id::string:
      return "const std::string& ";
    case Type_id::vector: // initializer_list<element type>
    case Type_id::array:
      switch (t.t->id)
      {
        case Type_id::variant:
          error("vectors and arrays of variants are not supported");
        case Type_id::optional:
          return "std::initializer_list<Optional_init<" +
            as_string_initializer_element(*t.t->t, lang) + ">>";
        default:
          return "std::initializer_list<" +
            as_string_initializer_element(*t.t, lang) + ">";
      }
    case Type_id::optional:
      switch (t.t->id)
      {
        case Type_id::array:
          return "std::initializer_list<" +
            as_string_initializer_element(*t.t->t, lang) + ">";
        default:
          return as_string_initializer_type(*t.t, Language::cpp);
      }
    default:
      return as_string_cpp(t);
  }
}

void print_initializer_type(const Type& t, Language lang, ostream& out)
{
  out << as_string_initializer_type(t, lang);
}

bool needs_allocator(const Flat& flt);

bool needs_allocator(const Type* t)
{
  while (t)
    switch (t->id)
    {
      case Type_id::flat:
      case Type_id::variant:
        return needs_allocator(*t->fl);
      case Type_id::optional:
      case Type_id::array:
        return needs_allocator(t->t);
      case Type_id::string:
      case Type_id::vector:
        return true;
      default:
        return false;
    };
  return false;
}

bool needs_allocator(const Flat& flt)
{
  for (auto m : flt.fields)
    if (needs_allocator(m.typ))
      return true;
  return false;
}

string as_string_allo(Flat* flt, string prefix, string infix, string suffix)
{
  return prefix + (needs_allocator(*flt) ? infix : "") + suffix;
}

string as_string_allo(Type* t, string prefix, string infix, string suffix)
{
  return prefix + (needs_allocator(t) ? infix : "") + suffix;
}

string member_ref(Type& t, int offset)
// "mbuf->" + m.name for unaligned fields
{
  return "reinterpret_cast<" + as_string(t) +
    "&>(*(reinterpret_cast<Byte*>(mbuf)+" + as_string(offset) + ") )";
}

string member_ref(const Field& m)
// "mbuf->" + m.name s
{
  return "mbuf->" + m.name;
}

string as_string_field_accessor(const Field& m, const string& test = "")
/*
        Examples:
        
        int& x() { return mbuf->x; }         // for direct read/write manipulation
        Span<char> s() { return mbuf->s; }  // a Strings is presented to users as a Span
        auto values() { return Span_ref<Pair, Pair_direct>{mbuf->values.begin(), mbuf->values.end(), allo}; }   // flat accessors return accessors
*/
{
  if (m.status == Status::deleting)
    return "";
  if (m.status == Status::deleted)
    return "";

  Type& t = *m.typ;
  //      std::cerr << "as_string_field_accessor: " << m.name << " " << int(t.id) <<'\n';

  switch (t.id)
  {
    case Type_id::flat:
      return "   auto " + m.name + "() { " + test + " return " + t.name +
        "_direct{&mbuf->" + m.name +
        as_string_allo(m.typ->fl, "", ", allo", "") + "}; } // flat\n";
    case Type_id::variant:
      if (needs_allocator(&t))
        return "   auto " + m.name + "() { " + test + " return " + as_string(t) +
          "_direct{&mbuf->" + m.name + ",allo}; } // variant field\n";
      return "   " + as_string_accessor(t) + m.name + "() { " + test +
        " return mbuf->" + m.name + "; } // variant field\n";
    case Type_id::vector: // auto values() { return Span_ref<Pair, Pair_direct>{mbuf->values.begin(), mbuf->values.end(), allo}; }
    case Type_id::array:
    case Type_id::varray:
      if (t.t->id == Type_id::flat)
        return "   auto " + m.name + "() { " + test + " return " + "Span_ref<" +
          as_string(*t.t) + ", " + as_string(*t.t) + "_direct>{mbuf->" +
          m.name + ".begin(), mbuf->" + m.name + ".end(), allo}; }\n";
      break;
    case Type_id::optional:
      //    std::cerr <<"XXX " << int(t.t->id) << " " << int(Type_id::flat) << '\n';
      if (t.t->id == Type_id::flat)
        return "   auto " + m.name + "() { " + test + " return Optional_" +
          as_string(*t.t) + "_ref {&mbuf->" + m.name + ",allo}; }\n";
      break;
    default:
      break;
  }

  return "   " + as_string_accessor(t) + m.name + "() { " + test +
    " return mbuf->" + m.name + "; }\n";
}

static void print_field_accessor(
  const Flat& /*flt*/, const Field& m, std::ostream& out, const string& test = "")
/*
        Examples:

        int& x() { return mbuf->x; }         // for direct read/write manipulation
        Span<char> s() { return mbuf->s; }  // a Strings is presented to users as a Span
        auto values() { return Span_ref<Pair, Pair_direct>{mbuf->values.begin(), mbuf->values.end(), allo}; }   // flat accessors return accessors
*/
{
  out << as_string_field_accessor(m, test);
}

static void print_optional_accessor(const Flat& flt, const Field& m, std::ostream& out)
{
  print_field_accessor(
    flt, m, out,
    "expect([&] { return is_present(); }, Error_code::optional_not_present);");
}

string as_string_icheck(int i)
{
  if (!initialize_check)
    return "";
  return "icheck[" + as_string(i) + "]=1; ";
}

string as_string_variant_direct_field_accessor(const Field& m)
{
  Type& t = *m.typ;

  switch (t.id)
  {
    case Type_id::flat:
      return "   auto " + m.name + "() { return var->" + m.name +
        as_string_allo(&t, "(", "allo", "); } // flat\n");
    case Type_id::variant:
      if (needs_allocator(&t))
        return "   auto " + m.name + "() { return var->" + m.name +
          "(allo); } // variant\n";
      return "   " + as_string_accessor(t) + m.name + "() { return var->" +
        m.name + "; } // variant\n";
    case Type_id::optional:
      error("not implemented (and probably not necessary): optional as variant field");
    default:
      return "   " + as_string_accessor(t) + m.name + "() { return var->" +
        m.name + "(); }\n";
  }
}

static void print_variant_direct_field_accessor(const Field& m, std::ostream& out)
{
  out << as_string_variant_direct_field_accessor(m);
}

string as_string_string_constructor(const Field& m, const Field& v) // v!=m for variant initializers
{
  return "   void " + v.name + "(" + as_string_initializer_type(*m.typ) +
    " arg) { " + as_string_icheck(v.index) + "new(&mbuf->" + v.name + ") " +
    as_string(*v.typ) + "(allo,arg); }\n"; // variants always need allocators
}

string as_string_string_constructor(const Field& m)
{
  return "   void " + m.name + "(" + as_string_initializer_type(*m.typ) +
    " arg) { " + as_string_icheck(m.index) + "new(&mbuf->" + m.name + ") " +
    as_string(*m.typ) + as_string_allo(m.typ, "(", "allo,", "arg); }\n");
}

string as_string_extent_constructor(const Field& m)
{
  return "   void " + m.name + "(Extent arg) { " + as_string_icheck(m.index) +
    "new(&mbuf->" + m.name + ") " + as_string(*m.typ) +
    as_string_allo(m.typ, "(", "allo,", "arg); }\n");
}
string as_string_extend_constructor(const Field& m)
{
  //    return "   void " + m.name + "(Extend arg) { "
  //       + "new(&mbuf->" + m.name + ") " + as_string(*m.typ) + as_string_allo(m.typ, "(", "allo,", "arg); }\n");
  return "   void " + m.name + "(Push) { mbuf->" + m.name + ".push(); }\n";
}

string as_string_push_constructor(const Field& m)
{
  //    return "   void " + m.name + "(Push<" + as_string(*m.typ->t) + "> arg) { "
  //       + "new(&mbuf->" + m.name + ") " + as_string(*m.typ) + as_string_allo(m.typ, "(", "allo,", "arg); }\n");
  return "   void " + m.name + "(Push, " + as_string(*m.typ->t) +
    " arg) { mbuf->" + m.name + ".push(arg); }\n";
}

string as_string_cstring_push_constructor(const Field& m)
{
  return "   void " + m.name + "(Push, const char* arg) { mbuf->" + m.name +
    ".push(allo,arg); }\n";
}

string as_string_cstring_constructor(const Field&, const Field& v) // v!=m for variant initializers
{
  return "   void " + v.name + "(const char* arg) { " +
    as_string_icheck(v.index) + "new(&mbuf->" + v.name + ") " +
    as_string(*v.typ) + "(allo,arg); }\n"; // variants always need allocators
}

string as_string_optional_constructor(const Field& m, const Field& opt)
// e.g., void name(const std::string&  arg) { new(&mbuf->op) Optional<Pair>(allo, arg); }
{
  return "   void " + m.name + "(" + as_string_initializer_type(*m.typ) +
    " arg) { " + as_string_icheck(m.index) + "new(&mbuf->" + opt.name + ") " +
    as_string(*m.typ) + as_string_allo(m.typ, "(", "allo,", "arg); }\n");
}

string as_string_cstring_constructor(const Field& m)
{
  return "   void " + m.name + "(const char* arg) { " +
    as_string_icheck(m.index) + "new(&mbuf->" + m.name + ") " +
    as_string(*m.typ) + as_string_allo(m.typ, "(", "allo,", "arg); }\n");
}
string as_string_varray_constructor(const Field& m)
// default, extent, list, push
{
  string s = as_string_extent_constructor(m);
  s += as_string_extend_constructor(m);
  if (m.typ->t->id == Type_id::string)
    s += as_string_cstring_push_constructor(m);
  else
    s += as_string_push_constructor(m);
  if (m.typ->t->id == Type_id::char8) // add additional C-style string initializer
    s += as_string_cstring_constructor(m);
  if (m.typ->t->id != Type_id::string)
    s += as_string_string_constructor(m);
  return s;
}

string as_string_field_constructor(const Field& m)
/*
    Example:

    ???
*/
{
  if (m.status == Status::deleting)
    return "";
  if (m.status == Status::deleted)
    return "";

  Type& t = *m.typ;

  switch (t.id)
  {
    case Type_id::flat:
      // initializer for flats should be either a heterognous list of an unraveled member list:  x({x,y,z}) or X(x,y,z)
      // not yet, maybe not ever: could be very long and useless for large flats; the initializer types for field types would have to be determined
      return "";
    case Type_id::optional:
      if (t.t->id == Type_id::flat)
        return ""; // initializer for flats should be either a heterognous list of an unraveled member list:  x({x,y,z}) or X(x,y,z)
      break;
    case Type_id::string: // add additional C-style string initializer
      return as_string_cstring_constructor(m) + as_string_string_constructor(m);
    case Type_id::array:
      if (t.t->id == Type_id::char8) // add additional C-style string initializer
        return as_string_cstring_constructor(m) + as_string_string_constructor(m);
      if (t.t->id == Type_id::flat)
        return "";
      break;
    case Type_id::vector:
      if (t.t->id == Type_id::flat)
        return "";
      break;
    case Type_id::varray:
      // default, extent, array, push
      return as_string_varray_constructor(m);
    case Type_id::variant:
    {
      string s;
      for (auto& mm : m.typ->fl->fields)
        if (mm.typ->id != Type_id::flat && mm.typ->id != Type_id::variant)
        { // initialization with variant should be allowed? no: use a specific accessor
          s += as_string_string_constructor(mm, m);
          if (mm.typ->id == Type_id::string)
            s += as_string_cstring_constructor(mm, m);
        }
      return s;
    }
    default:
      break;
  }
  return as_string_string_constructor(m);
}

static void print_field_constructor(const Field& m, std::ostream& out)
{
  out << as_string_field_constructor(m);
}

string as_string_optional_field_constructor(const Field& m)
{
  if (m.status == Status::deleting)
    return "";
  if (m.status == Status::deleted)
    return "";

  Type& t = *m.typ;

  switch (t.id)
  {
    case Type_id::flat:
      // initializer for flats should be either a heterognous list of an unraveled member list:  x({x,y,z}) or X(x,y,z)
      // not yet, maybe not ever: could be very long and useless for large flats; the initializer types for field types would have to be determined
      return "";
    case Type_id::array:
      //        if (t.t->id == Type_id::char8) // add additional C-style string initializer
      //            return as_string_cstring_constructor(m) + as_string_string_constructor(m);
      break;
    default:
      break;
  }
  return as_string_optional_constructor(m, m);
}

void print_optional_field_constructor(const Field& m, std::ostream& out)
{
  out << as_string_optional_field_constructor(m);
}

string as_string_variant_direct_field_constructor(const Field& m, const Flat& flt)
/*
    for variants only
*/
{
  Type& t = *m.typ;

  switch (t.id)
  {
    default:
      break;
    case Type_id::flat:
    case Type_id::variant: // can't handle Flats as initializers
      return "";
    case Type_id::optional:
      if (t.t->id == Type_id::flat)
        return "";
  };

  string s = "   void " + m.name + "(" + as_string_initializer_type(t) +
    " arg) { " + as_string_icheck(m.index) + " new(&reinterpret_cast<" +
    flt.name + "::U*>(reinterpret_cast<Byte*>(var) + var->pos)->" + m.name +
    ") " + as_string(t) + as_string_allo(m.typ, "(", "allo,", "arg);") + "}\n";

  if (t.id == Type_id::string)
  { // add const char* constructor
    s += "   void " + m.name + "(const char* arg) { " +
      as_string_icheck(m.index) + " new(&reinterpret_cast<" + flt.name +
      "::U*>(reinterpret_cast<Byte*>(var) + var->pos)->" + m.name + ") " +
      as_string(t) + as_string_allo(m.typ, "(", "allo,", "arg);") + " }\n";
  }
  return s;
}

static void
  print_variant_direct_field_constructor(const Field& m, const Flat& flt, std::ostream& out)
// for variants only
{
  out << as_string_variant_direct_field_constructor(m, flt);
}

string as_string_field_empty_constructor(const Field& m)
//    for Optionals only: void o2(Empty) { new(&mbuf->o2) Optional<String>{mess, s}; }
{
  if (m.status == Status::deleting)
    return "";
  if (m.status == Status::deleted)
    return "";

  Type& t = *m.typ;
  if (t.id != Type_id::optional)
    return "";

  return "   void " + m.name + "(Empty) { new(&mbuf->" + m.name + ") " +
    as_string(t, Language::cpp) +
    as_string_allo(m.typ, "(", "allo,", "Empty{});") + " }\n";
}

string as_string_field_default_constructor(const Field& m)
// for Optionals only: void o2(Empty) { new(&mbuf->o2) Optional<String>{mess, s}; }
{
  if (m.status == Status::deleting)
    return "";
  if (m.status == Status::deleted)
    return "";

  Type& t = *m.typ;
  if (t.id != Type_id::optional)
    return "";

  return "   void " + m.name + "(Default) { new(&mbuf->" + m.name + ") " +
    as_string(t, Language::cpp) +
    as_string_allo(m.typ, "(", "allo,", "Default{});") + " }\n";
}

void print_field_empty_constructor(const Field& m, std::ostream& out)
// for Optionals only: void o2(Empty) { new(&mbuf->o2) Optional<String>{mess, s}; }
{
  out << as_string_field_empty_constructor(m);
}

void print_field_default_constructor(const Field& m, std::ostream& out)
// for Optionals only: void o2(Empty) { new(&mbuf->o2) Optional<String>{mess, s}; }
{
  out << as_string_field_default_constructor(m);
}

string as_string_field_size_constructor(const Field& m)
// for Vectors only:  void v1(Extent x) { new(&mbuf->v1) Vector<int32_t>{ allo, x }; }
{
  if (m.status == Status::deleting)
    return "";
  if (m.status == Status::deleted)
    return "";

  Type& t = *m.typ;

  switch (t.id)
  {
    case Type_id::vector:
    case Type_id::string:
      return "   void " + m.name + "(Extent arg) { new(&mbuf->" + m.name +
        ") " + as_string(t, Language::cpp) + "(allo,arg); }\n" + "   void " +
        m.name + "(Push) { mbuf->" + m.name + ".push(allo); }\n" +
        "   template<class Arg> void " + m.name + "(Push, Arg arg) { mbuf->" +
        m.name + ".push(allo, arg); }\n";
    default:
      return "";
  }
}

void print_field_size_constructor(const Field& m, std::ostream& out)
// for Vectors only:  void v1(Extent x) { new(&mbuf->v1) Vector<int32_t>{ allo, x }; }
{
  out << as_string_field_size_constructor(m);
}

void print_message(const Flat& mess, std::ostream& out) // generate a Message to hold a Flat
{
  Flat& flt = *mess.t->fl;
  bool allo = needs_allocator(flt);

  std::string mn = mess.name; // + "_message";
  out << "struct " << mn << " {\n";
  out << "   using Flat = " << flt.name << ";\n";
  out << "   Version v = { " << flt.fields.size() << "}; // version is generated\n";
  if (allo)
  {
    out << "   Allocator alloc;\n";

    out << "   " << mn << "(int buffer_size, int tail_size)\n";
    out << "      :alloc{ size_of<Flat>(),size_of<Flat>() + tail_size }\n";
    out << "      { expect([&] {return static_cast<int>(sizeof(*this)) + alloc.max <=buffer_size; }, Error_code::small_buffer);\n";
    if (default_init)
    {
      out << "        Byte* pp = reinterpret_cast<Byte*>(flat());\n";
      out << "        for (int i = 0; i<size_of<Flat>(); ++i) pp[i]=Byte{0};\n"; // is the compiler smart enough to optimize?
      out << "        Byte* p = tail();\n";
      out << "        for (int i = 0; i<tail_size; ++i) p[i]=Byte{0};\n"; // is the compiler smart enough to optimize?
    }
    out << "      }\n";

    out << "   " << mn << "(Reader, int buffer_size)\n";
    out << "      { expect([&] {return static_cast<int>(sizeof(*this)) + alloc.max <=buffer_size; }, Error_code::small_buffer); }\n";

    out << "   Byte* tail() { return reinterpret_cast<Byte*>(flat()) + sizeof(Flat); }\n";
    out << "   int current_size() const { return sizeof(*this) + alloc.next; }\n";
    out << "   int current_capacity() const { return alloc.max - alloc.next; }\n";
    out << "   " << flt.name << "_direct direct() { return { flat(), &alloc }; }\n";
  }
  else
  {
    out << "   " << mn << "(int buffer_size, int)\n";
    out << "      { expect([&] {return static_cast<int>(sizeof(*this)) < buffer_size; }, Error_code::small_buffer); }\n";

    out << "   " << mn << "(Reader, int buffer_size)\n";
    out << "      { expect([&] {return static_cast<int>(sizeof(*this)) < buffer_size; }, Error_code::small_buffer); }\n";

    out << "   int current_size() const { return sizeof(*this)+sizeof(Flat); }\n";
    out << "   int current_capacity() const { return 0; }\n";
    out << "   " << flt.name << "_direct direct() { return { flat() }; }\n";
  }
  out << "   " << flt.name << "* flat() { return reinterpret_cast<" << flt.name
      << "*>(reinterpret_cast<Byte*>(this) + sizeof(*this)); }\n";
  out << "   int version() const { return v.v; }\n";
  out << "   int size() const { return current_size()+current_capacity(); }\n";

  out << "   " << mn << "* clone(Byte* p) const {\n"; // returning a reference is accident prone with auto
  out << "      auto pt = reinterpret_cast<const Byte*>(this);\n";
  out << "      for (int i = 0; i<size(); ++i) p[i]=pt[i];\n";
  out << "      return reinterpret_cast<" << mn << "*>(p);\n";
  out << "   }\n";

  out << "      " << mn << "(const " << mn
      << "& arg)\n"; /// sneaky copy constructor; use for placement only
  out << "   {\n";
  out << "      auto p = reinterpret_cast<Byte*>(this);\n";
  out << "      auto pt = reinterpret_cast<const Byte*>(&arg);\n";
  out << "      for (int i = 0; i<size(); ++i) p[i]=pt[i];\n";
  out << "   }\n";

  out << "};\n\n";

  // placement helper functions:
  out << "inline " << mess.name << "* place_" << mess.name
      << "(Byte* buf, int size_of_buffer, int size_of_tail)";
  out << "   { return new(buf) " << mess.name
      << " { size_of_buffer,size_of_tail }; }\n\n";

  out << "inline " << mess.name << "* place_" << mess.name
      << "_reader(Byte* buf, int size_of_buffer, int )";
  out << "   { return new(buf) " << mess.name << " { Reader{}, size_of_buffer}; }\n\n";

  out << "inline " << mess.name << "* place_" << mess.name
      << "_writer(Byte* buf, int size_of_buffer, int size_of_tail)";
  out << "   { return new(buf) " << mess.name
      << " { size_of_buffer,size_of_tail }; }\n\n";
}

void print_variant_direct(const Flat& flt, std::ostream& out)
{
  out << "struct " << flt.name << "_direct {\n";
  out << "   " << flt.name << "* var;\n";
  out << "   Allocator* allo;\n";
  out << "   " << flt.name << "_direct(" << flt.name
      << "* v,Allocator* a) :var{v}, allo{a} {}\n";
  out << "   auto tag() { return var->utag; }\n";
  out << "   bool is_present() { return var->utag; }\n"; // for consistent use

  for (auto m : flt.fields)
  {
    print_variant_direct_field_constructor(m, flt, out);
    print_variant_direct_field_accessor(m, out);
  }
  out << "};\n";
}

void print_optional_ref(const Flat& flt, std::ostream& out)
{
  out << "struct Optional_" << flt.name << "_ref {\n";
  out << "   Optional<" << flt.name << ">* val;\n";
  out << "   " << flt.name << "* mbuf;\n";
  out << "   Allocator* allo;\n";

  out << "   bool is_present() const { return val->filled; }\n";
  out << "   bool is_empty() const { return !is_present(); }	// pretend to be a container\n\n";

  out << "   Optional_" << flt.name << "_ref(Optional<" << flt.name
      << ">* v,Allocator* a) :val{ v }, mbuf{ &v->val }, allo{ a } {}\n\n";

  for (auto m : flt.fields)
  {
    print_optional_accessor(flt, m, out);
    print_optional_field_constructor(m, out);
    //    print_field_empty_constructor(m, out);  // for Optionals only
    //    print_field_size_constructor(m, out);   // for Vectors only
    out << '\n';
  }

  out << "};\n\n";
}

void print_direct(const Flat& flt, std::ostream& out, bool packed = false)
/*
    struct Message_direct {
        Message* mbuf;
        Allocator* allo;
        Message_direct(Message* pp, Allocator* a) : mbuf{ pp }, allo{ a } {}
        // ... field accessors ...
    };
*/
{
  switch (flt.id)
  {
    case Type_id::variant:
      print_variant(flt, out, packed);
      if (needs_allocator(flt.t))
        print_variant_direct(flt, out);
      return;
    case Type_id::enumeration:
      return;
    case Type_id::message:
      print_message(flt, out);
      return;
    default:
      break;
  }

  // print "ordinary flat direct accessor:

  const auto& n = flt.name;

  out << "\n\n// Flat direct accessors:\n";
  out << "// options: initializer check==" << initialize_check
      << " default initialization==" << default_init << "\n\n";

  out << "   struct " << n << "_message;\n"; // forward declaration
  out << "struct " << n << "_direct {\n";
  out << "   " << n << "* mbuf;\n";
  out << "   constexpr static bool flat_tag = true;\n"; // to be able to select with is_flat<> concept
  if (needs_allocator(flt))
  {
    out << "   Allocator* allo;\n";
    out << "   " << flt.name << "_direct(" << flt.name
        << "* pp, Allocator* a) :mbuf{pp}, allo{a} ";
  }
  else
    out << "   " << flt.name << "_direct(" << flt.name << "* pp) :mbuf{pp} ";
  out << "{}\n";
  if (initialize_check)
    out << "   char icheck[" << flt.fields.size() << "] = {0};\n";

  for (auto m : flt.fields)
  {
    print_field_accessor(flt, m, out);
    print_field_constructor(m, out);
    if (m.typ->id == Type_id::optional)
    {
      print_field_empty_constructor(m, out);
      print_field_default_constructor(m, out);
    }
    print_field_size_constructor(m, out); // for Vectors only ??? Fixed_vector ???
    out << '\n';
  }

  out << "};\n\n";

  if (flt.used_as_optional)
    print_optional_ref(flt, out);
}