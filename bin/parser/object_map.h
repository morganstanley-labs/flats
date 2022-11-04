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
#include "flat.h"

struct Field_entry
{
  int index; // ordinal
  int offset;
  int size;
  Type_id type_id;
  int count; // elements of an array
  int no_of_type_names;
  std::string name;
  std::string type_name;
};

struct Flat_header
{
  std::string name;
  int number_of_fields; // version-deleted
  int version;
  // int size;	???
};

struct Object_map
{
  Flat_header head;
  std::vector<Field_entry> fields;
};

Object_map make_object_map(Flat& flt, bool packed = false);

void print(Object_map& m, std::ostream&); // print as text