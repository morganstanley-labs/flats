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
 
//---------------------------------------
// object map generator:

#include "object_map.h"
using namespace std;

string get_name(const Type& t)
{
  switch (t.id)
  {
    case Type_id::int32:
      return "int32";
    case Type_id::float32:
      return "float";
    case Type_id::string:
      return "string";
    case Type_id::flat:
      return t.name;
    case Type_id::optional:
    case Type_id::vector:
      return t.t->name;
    //case Type_id::variant:	return t.t->name;
    default:
      error("type not implemented (no name)", t.name);
  }
}

string make_type_rep(Type& tp)
{
  string s;
  switch (tp.id)
  {
    default: // fundamental types, variants, and flats: T
      s = tp.name;
      break;
    case Type_id::vector: // <T>
      s += "vector<" + make_type_rep(*tp.t) + ">";
      break;
    case Type_id::optional: // <T>
      s += "optional<" + make_type_rep(*tp.t) + ">";
      break;
  }
  if (1 < tp.count)
    s += "[" + to_string(tp.count) + "]";
  return s;
}

Object_map make_object_map(Flat& flt, bool packed)
{
  int count = 0; // number of object_map entries
  int index = 0; // field index
  int position = 0; // byte count

  Object_map m;
  m.head.name = flt.name;
  m.head.version = flt.no_of_fields();

  for (Field& fld : flt.fields)
  {
    switch (fld.status)
    {
      case Status::deleting:
      case Status::deprecating:
      case Status::deleted:
        break;
      default:
      {
        Type* tp = fld.typ;
        //cerr << "field: " << fld.name << (packed?" packed ":" aligned ") << "pos =" << position << " sz=" << tp->size << " al=" << tp->align << ' ' << position % tp->align << '\n';
        //cerr<< "type: " << tp->name << '\n';
        fld.size = tp->size;
        fld.offset = position;
        m.fields.push_back(Field_entry{
          index, position, tp->size, tp->id, tp->count, 0, fld.name,
          make_type_rep(*tp)});
        ++count;
        if (!packed && (position % tp->align))
          position += (tp->align - position % tp->align);
        if (flt.id != Type_id::variant)
        {
          //cerr << "field: " << fld.name << " pos=" << position << '\n';
          position += tp->size;
        }
      }
    }
    ++index;
  }
  m.head.number_of_fields = count;

  if (!packed)
    position += (alignof(Flat) - position % alignof(Flat));
  flt.t->size = position;
  flt.var = {position, position};
  return m;
}
