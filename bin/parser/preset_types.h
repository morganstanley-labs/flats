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
	mappings of Flat's type names to language-specific type names
	currently C++ and Java

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
*/

// clang-format off
// name		cpp_native		java_native	java_flat	id					size				align
{ "int8",	"std::int8_t",	"byte",		"Int8",		int(Type_id::int8), sizeof(std::int8_t),	alignof(std::int8_t) },
{ "char",	"char",			"byte",		"Char8",	int(Type_id::char8), 1, 					alignof(char) },
{ "int16",	"std::int16_t",	"short",	"Int16",	int(Type_id::int16), sizeof(std::int16_t), 	alignof(std::int16_t) },
{ "int24",	"Int24",		"int",		"Int24",	int(Type_id::int24), 4, alignof(std::int32_t) },	// pretty weird type and size
{ "int32",	"std::int32_t",	"int", 		"Int32",	int(Type_id::int32), sizeof(std::int32_t),	alignof(std::int32_t) },
{ "int64",	"int64_t",		"long",		"Int64",	int(Type_id::int64), sizeof(int64_t),		alignof(int64_t) },
{ "uint8",	"unsigned char", "int", 	"Uint8",	int(Type_id::uint8), sizeof(signed char),	alignof(signed char) },
{ "uint16",	"std::uint16_t", "short",	"Uint16",	int(Type_id::uint16), sizeof(std::uint16_t), alignof(std::uint16_t) },
{ "uint32",	"std::uint32_t", "int",		"Uint32",	int(Type_id::int32), sizeof(std::uint32_t), alignof(std::uint32_t) },
{ "uint64",	"uint64_t", 	"long",		"Uint64",	int(Type_id::int64), sizeof(uint64_t),		alignof(uint64_t) },
{ "float32","float",		"float",	"Float32",	int(Type_id::float32), sizeof(float),		alignof(float) },
{ "float64","double",		"double",	"Float64",	int(Type_id::float64), sizeof(double),		alignof(double) },

{ "string",	"string",		"String",	"String",	int(Type_id::string), sizeof(string),		alignof(string) },
{ "TimeStamp","TimeStamp","TimeStamp",	"TimeStamp",	int(Type_id::Preset)+1 /*int(Type_id::TimeStamp)*/, 16 /*sizeof(TimeStamp)*/, 8 /*alignof(TimeStamp)*/ },
{ "time_point","time_point","TP",	"TP",	int(Type_id::Preset)+2, sizeof(time_point), sizeof(time_point) },	// ???
{ "ukey_t", "ukey_t", "UP", "UP", int(Type_id::Preset)+3, sizeof(ukey_t), alignof(ukey_t)  }, // ???
{ "exchange_id", "exchange_id", "EI", "EI", int(Type_id::Preset)+4, sizeof(exchange_id), alignof(exchange_id)  }, // ???
{ "option_price_t", "option_price_t", "OP", "OP", int(Type_id::Preset)+5, sizeof(option_price_t), alignof(option_price_t)  }, // ???
{ "option_trade_side_values", "option_trade_side_values", "SV", "SV", int(Type_id::Preset)+6, sizeof(option_trade_side_values), alignof(option_trade_side_values)  }, // ???
{ "instrument_status", "instrument_status", "IS", "IS", int(Type_id::Preset)+5, sizeof(instrument_status), alignof(instrument_status)  },
{ "option_book_flags", "option_book_flags", "OB", "OB", int(Type_id::Preset)+5, sizeof(option_book_flags), alignof(option_book_flags)  },
{ "option_book_flags1", "option_book_flags1", "OBF1", "OBF1", int(Type_id::Preset)+5, sizeof(option_book_flags1), alignof(option_book_flags1)  }
 // clang-format on