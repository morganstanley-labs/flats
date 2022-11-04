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

// Simple print function for debugging

#include "flat.h"
#include <sstream>
using namespace std;

string as_string(int x)
{
  stringstream ss;
  ss << x;
  return ss.str();
}

string as_string_cpp(const Type& t)
{
  switch (t.id)
  {
    default:
      return t.cpp_native_name;
    case Type_id::string:
      return "String";
    case Type_id::flat:
    case Type_id::variant:
      return t.name;
    case Type_id::optional:
      return "Optional<" + as_string_cpp(*t.t) + ">";
    case Type_id::vector:
      return "Vector<" + as_string_cpp(*t.t) + ">";
    case Type_id::array:
      return "Array<" + as_string_cpp(*t.t) + " , " + as_string(t.count) + ">";
    case Type_id::varray:
      return "Fixed_vector<" + as_string_cpp(*t.t) + " , " + as_string(t.count) + ">";
    case Type_id::undefined: // impossible
      error(t.name, " not defined after end of parse");
  }
}

void cpp_print(const Type& t, ostream& out)
{
  switch (t.id)
  {
    case Type_id::string:
      out << "String";
      break;
    case Type_id::flat:
    case Type_id::variant:
      out << t.name;
      break;
    case Type_id::optional:
      out << "Optional< ";
      print(*t.t, Language::cpp, out);
      out << " >";
      break;
    case Type_id::vector:
      out << "Vector< ";
      print(*t.t, Language::cpp, out);
      out << " >";
      break;
    case Type_id::array:
      out << "Array< ";
      print(*t.t, Language::cpp, out);
      out << " , " << t.count << " >";
      break;
    case Type_id::varray:
      out << "Fixed_vector< ";
      print(*t.t, Language::cpp, out);
      out << " , " << t.count << " >";
      break;
    case Type_id::undefined: // impossible
      error(t.name, " not defined after end of parse");
    default:
      out << t.cpp_native_name;
  }
}

string as_string_java_flat(const Type& t)
{
  std::string n; // = as_string(int(t.id));
  switch (t.id)
  {
    default:
      n += t.java_flat_name;
      break;
    case Type_id::flat:
      n += t.name;
      break;
    case Type_id::variant:
    case Type_id::undefined: // impossible;
      n += "UNIMPLEMENTED";
      break;
    case Type_id::vector:
      if (t.t->id == Type_id::flat)
        n += t.t->name;
      else
        n += t.t->java_flat_name;
      n += "Vector";
      break;
    case Type_id::array:
      if (t.t->id == Type_id::flat)
        n += t.t->name;
      else
        n += t.t->java_flat_name;
      n += "Array_";
      break;
    case Type_id::varray:
      if (t.t->id == Type_id::flat)
        n += t.t->name;
      else
        n += t.t->java_flat_name;
      n += "Fixed_vector_";
      break;
  }
  if (t.count != 1)
    n += as_string(t.count);
  return n;
}

string as_string_java(const Type& t)
{
  string s;
  switch (t.id)
  {
    default:
      s = t.java_native_name;
      break;
    case Type_id::string:
      s = "String";
      break;
    case Type_id::flat:
    case Type_id::variant:
      s = t.name;
      break;
    case Type_id::undefined: // impossible
      error(t.name, "not defined after end of parse");
  }
  if (t.count != 1)
    s += "[" + as_string(t.count) + "]";
  return s;
}

void java_print(const Type& t, ostream& out)
{
  out << as_string_java(t);
  /*
	switch (t.id) {
	case Type_id::string:
		out << "String";
		break;
	case Type_id::flat:
	case Type_id::variant:
		out << t.name;
		break;
	case Type_id::undefined:	// impossible
		error(t.name, "not defined after end of parse");
	default:
		out << t.java_native_name;
	}
	if (t.count != 1) out << '[' << t.count << ']';
	*/
}

string as_string(const Type& t, Language lang)
{
  switch (lang)
  {
    case debug:
    case cpp:
      return as_string_cpp(t);
    case java:
      return as_string_java(t);
    default:
      error("internal error: bad output language");
  }
}

void print(const Type& t, Language lang, ostream& out)
{
  switch (lang)
  {
    case debug:
    case cpp:
      cpp_print(t, out);
      break;
    case java:
      java_print(t, out);
      break;
  }
}

string as_string(const Field& m) // ugly
{
  if (m.status == Status::deleting)
    return "{ delete " + m.name + " }\n"; // {} ???

  if (m.status == Status::deprecating)
    return "{ deprecate " + m.name + " }\n";

  string s = "{ ";
  if (m.status == Status::deleted)
    s += "deleted ";
  else if (m.status == Status::deprecated)
    s += "deprecated ";
  return s + m.name + " : " + as_string(*m.typ) + "}\n";
}

void print(const Field& m)
{
  cout << as_string(m);
}
/*
{
	cout << "{";
	if (m.status == Status::deleting)
		cout << "delete " << m.name;
	else
		if (m.status == Status::deprecating)
			cout << "deprecate " << m.name;
		else {
			if (m.status == Status::deleted)
				cout << "deleted ";
			else if (m.status == Status::deprecated)
				cout << "deprecated ";
			cout << m.name << " : ";
			print(*m.typ);
		}
		cout << "}\n";
}
*/
void print_enumeration(const Flat& flt)
{
  cout << "enum {";
  for (auto m : flt.fields)
    cout << m.name << ":" << m.value << " ";
  cout << "}\n";
}

void print_flat(const Flat& flt, Type_id id)
{
  cout << ((id == Type_id::flat) ? "flat" : "variant") << " {\n";
  for (auto m : flt.fields)
    print(m);
  cout << "}\n";
}

void print(const Flat& flt)
{
  cout << flt.name << " : ";
  switch (flt.id)
  {
    case Type_id::flat:
    case Type_id::variant:
      print_flat(flt, flt.id);
      break;
    case Type_id::enumeration:
      print_enumeration(flt);
      break;
    default:
      error("not a flat, variant, or enum", flt.name);
  }
}
