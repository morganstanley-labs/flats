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

template <class T, int s>
struct scaled_decimal
{ // fake, for now ???
  T a;
};

using option_price_t = scaled_decimal<uint32_t, 4>;

struct time_point
{
  int64_t value; // nanoseconds since epoch
};

using ukey_t = uint32_t;

enum class exchange_id : uint16_t
{
  none,
  // other values...
};

enum class option_trade_side_values : char
{
  buy = 'B',
  sell = 'S'
};

//** option_trade_side_values : enum { buy = 'B' sell = 'S' }

enum class instrument_status : uint8_t
{
  // values...
};
//** instrument_status : enum { /* values */ }

enum class option_book_flags : uint8_t
{
  // values
};

// option_book_flags : enum { /* values */ }
using option_book_flags1 = option_book_flags; //  enum_bitset<option_book_flags, 16>;
static std::size_t const k_num_levels = 5;

//using small_vector = Flats::Fixed_vector;
//using legs_t = small_vector<leg, 6>;
// small vector is a std::vector with small buffer optimization for N elements
//** I don't have such a type (unless you mean Fixed_vector). Can a small_vector have more than 6 elements? ???
