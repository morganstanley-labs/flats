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
 view accessors
*/

#include "flat.h"
using namespace std;

static void
  print_view_field_accessor(const Field& m, const Flat& flt, std::ostream& out, int count)
/*
	int& x() { return reinterpret_cast<Mess*>(buff+m[m.index]); }
*/
{
  if (m.status == Status::deleting)
    return;
  if (m.status == Status::deleted)
    return;
  if (m.typ->count > 1)
  {
    out << "using XXX" << count << " = ";
    print(*m.typ, Language::cpp, out);
    out << ";\n  XXX" << count;
  }
  else
    print(*m.typ, Language::cpp, out);
  out << "& ";
  out << m.name;
  out << "() { return *reinterpret_cast<" << flt.name << "*>(buff+m[" << m.index
      << "]); }\n";
}

void print_view(const Flat& flt, std::ostream& out)
/*
	struct Mess_view_accessor {
		const Offsets m;	// an offset is an array of ints
		Byte* p;
		Mess_view_accessor(const Offsets& mm, Byte* pp) :m{ mm }, p{ pp } {};

		int& x() { *reinterpret_cast<int*>(p + m[0]); }
		string& s() { *reinterpret_cast<String*>(p + m[1]); }
	};
*/
{
  if (flt.id == Type_id::variant)
    return;
  if (flt.id == Type_id::enumeration)
    return;
  out << "\n\n// view accessors:\n";
  out << " struct " << flt.name << "_view {\n";
  out << "   const Offsets mm;\n";
  out << "   Byte* buff;\n";
  out << flt.name << "_view(const Offsets& mm, const Byte* pp) :m{mm}, buff{ pp } {}\n";
  int count = 0;
  for (auto m : flt.fields)
    print_view_field_accessor(m, flt, out, count++);
  out << "};\n\n";
}
