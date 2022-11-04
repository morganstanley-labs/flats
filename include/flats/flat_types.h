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
	Hand-written types for use in flats and messages

	Why not use the standard ones?
	Because std::vector, etc. aren't guaranteed to have "flat" implementations; in fact, they don't.

	Why initializer_list rather than variadic templates?
	Expediency; there may be a significant improvement to be had.

	Why no Variant?
	There were too little shared code among all Variants for it to be worthwhile; so they are generated.
*/

#include <string>
#include <iostream>
#include <cstddef>
#include <exception>

// hack to use either std concepts or TS concepts
#define CBOOL bool

template <class T>
concept CBOOL is_vector = requires(T a)
{
  a.vector_tag;
};

template <class T>
concept CBOOL is_array = requires(T a)
{
  a.array_tag;
};

template <class T>
concept CBOOL is_container = is_vector<T> || is_array<T>;

template <class T>
concept CBOOL is_flat = requires(T a)
{
  a.flat_tag;
};

template<class T> 
concept CBOOL is_optional = requires(T a)
{
  a.optional_tag; 
};

template <class T>
concept CBOOL is_concrete = !is_container<T>;

template <bool a = false>
constexpr bool False = a;

namespace Flats
{

using Byte = std::byte; //  unsigned char;
using Offset = short; // relative position measured in Bytes in a flat or message
using Size = short; // the number of Bytes of something in a mesage or flat
using int64_t = long long;
using uint64_t = unsigned long long;

class Reader
{
}; // for overloading message constructors
class Writer
{
};
class Reader_writer
{
};

template <class T>
constexpr Size size_of()
{
  return static_cast<Size>(sizeof(T));
}
static Size size_of(const std::string& s)
{
  return static_cast<Size>(s.size());
}
template <class T>
Size size_of(const std::initializer_list<T> s)
{
  return static_cast<Size>(s.size());
}

struct Empty
// indicate an empty Optional; necessary in initializer lists.
{
};

struct Default
// indicate a defaulted Optional; necessary for flats.
{
};

enum class Error_handling
{
  ignoring,
  throwing,
  terminating,
  logging,
  testing
};

enum class Error_code
{
  bad_int,
  bad_array_init,
  tail_too_big,
  bad_span_index,
  array_initializer,
  small_buffer,
  optional_not_present,
  cstring_overflow,
  truncation,
  narrowing,
  variant_tag,
  fixed_array_overflow
};

const std::string error_code_name[] = {
  "bad int",
  "bad array init",
  "tail too big",
  "bad span index",
  "array initializer",
  "buffer too small",
  "optional not present",
  "C-style string too long",
  "C-style string truncation",
  "narrowing",
  "bad variant tag",
  "fixed array overflow"};

constexpr Error_handling default_error_action = Error_handling::testing;
constexpr Error_handling check_cstring = Error_handling::testing;
constexpr Error_handling check_truncation = Error_handling::testing;
constexpr Error_handling check_narrowing = Error_handling::testing;

template <Error_handling action = default_error_action, class C>
constexpr void expect(C cond, Error_code x) // C++17; a bit like assert()
{
  if constexpr (action == Error_handling::ignoring)
    return;
  else if constexpr (action == Error_handling::logging)
  {
    if (!cond())
      std::cerr << "Flats error: " << int(x) << ' ' << error_code_name[int(x)] << '\n';
    return;
  }
  else if constexpr (action == Error_handling::testing)
  {
    if (!cond())
    {
      std::cerr << "Flats error: " << int(x) << ' ' << error_code_name[int(x)] << '\n';
      throw x;
    }
    return;
  }
  else if constexpr (action == Error_handling::throwing)
  {
    if (!cond())
      throw x;
    return;
  }
  else if constexpr (action == Error_handling::terminating)
  {
    if (!cond())
      std::terminate();
    return;
  }
  else
    static_assert(False<>, "bad error handling option");
}

inline Offset narrow(std::size_t x)
{
  Offset xx = static_cast<Offset>(x);
  expect<check_narrowing>(
    [x, xx] { return x == static_cast<std::size_t>(xx); }, Error_code::narrowing);
  return xx;
}

struct Extent
// the number of elements of a given type in an array or vector
{
  Size sz;
  explicit Extent(int x) : sz{narrow(x)}
  {
  }
}; 

// Allocator and Version are not part of a Flat because they are shared by all Flats in a message
struct Version
{
  int v;
};
struct Tail_ref
{
  Offset pos;
  Size sz;
}; // offset retative to start of message

inline Size cstring_copy(char* to, const char* from, int max)
// copy at most max characters
{
  const char* first = to;
  while (*from)
  {
    expect<check_cstring>([max] { return 0 < max; }, Error_code::cstring_overflow);
    --max;
    *to++ = *from++;
  }
  return to - first;
}

struct Allocator
{
  Offset next, max; // retative to the start of message

  Allocator(int n, int m)
    : next{static_cast<Offset>(n)}
    , max{static_cast<Offset>(m)}
  {
  }
  Allocator()
  {
  } // don't touch, used for reading

  Offset allocate(int sz)
  { // allocate sz bytes from tail
    int nx = next;
    expect([&]{ return nx + sz <= max; }, Error_code::tail_too_big);
    next += sz;
    return nx;
  };

  Tail_ref place(const char* str)
  {
    Offset pos = next;
    char* p = reinterpret_cast<char*>(flat()) + pos;
    Size sz = cstring_copy(p, str, max - next);
    next += sz;
    return {pos, sz};
  }

  Size capacity() const
  {
    return max - next;
  }
  Byte* flat()
  {
    return reinterpret_cast<Byte*>(this) + sizeof(*this);
  } // the allocator is always just before the flat; sneaky
};

template <typename T>
struct Span
// a pure acessor read/write access to an array of chars
// not std::span because of error handling: expect()
{
  Span(T* p, T* q): first{p}, last{q}
  {
  }

  T* first;
  T* last;

  T* begin()
  {
    return first;
  }
  T* end()
  {
    return last;
  }

  const T* begin() const
  {
    return first;
  }
  const T* end() const
  {
    return last;
  }

  Size size() const
  {
    return last - first;
  }

  bool is_present() const
  {
    return size(); // pretend to be optional
  }

  bool is_empty() const
  {
    return size() == 0; // pretend to be a container
  } 

  T& operator[](int i) requires is_concrete<T>
  {
    expect([&] { return 0 <= i && i < size(); }, Error_code::bad_span_index);
    return first[i];
  }

  const T& operator[](int i) const requires is_concrete<T>
  {
    expect([&] { return 0 <= i && i < size(); }, Error_code::bad_span_index);
    return first[i];
  }

  auto operator[](int i) requires is_vector<T> || is_array<T>
  {
    expect([&] { return 0 <= i && i < size(); }, Error_code::bad_span_index);
    return Span<typename T::value_type>{first[i].begin(), first[i].end()};
  }

  auto operator[](int i) const requires is_vector<T> || is_array<T>
  {
    expect([&] { return 0 <= i && i < size(); }, Error_code::bad_span_index);
    return Span<typename T::value_type>{first[i].begin(), first[i].end()};
  }

  void operator=(const char* p)
  {
    for (T& t : *this)
    {
      t = *p++;
      if (t == 0)
        return;
    }
    expect<check_truncation>([p] { return *p == 0; }, Error_code::truncation);
  }

  void operator=(const std::string& s)
  {
    expect<check_truncation>([&] { return s.size() <= size(); }, Error_code::truncation);
    int max = (s.size() < size()) ? s.size() : size(); // expect might not terminate
    for (int i = 0; i < max; ++i)
    {
      first[i] = s[i];
    }
    if (s.size() < size())
      first[s.size()] = 0; // sneaky terminator
  }

  void operator=(const std::initializer_list<T> s) 
  // without this span::operator=(string) might get picked up (and surprisingly work)
  {
    int max = size();
    expect<check_truncation>([&] { return s.size() == (unsigned)max; }, Error_code::truncation);
    for (int i = 0; i < max; ++i)
      first[i] = *(s.begin() + i);
  }

  const std::string to_string() const
  {
    std::string res;
    for (char x : *this)
      if (x)
        res += x;
      else
        break;
    return res;
  }
};

template <class T, class TD>
struct Span_ref
// Span over an array of flats T
// when returning an element of type T, it constructs an accessor TD for the element value
{
  T* first;
  T* last;
  Allocator* allo;

  Span_ref(T* p, T* q, Allocator* a) : first{p}, last{q}, allo{a}
  {
  }

  struct Ptr_ref
  {
    Ptr_ref(T* pp, Allocator* a) : p{pp}, allo{a}
    {
    }
    T* p;
    Allocator* allo;
    Ptr_ref& operator++()
    {
      ++p;
      return *this;
    }
    bool operator==(const Ptr_ref& pp) const
    {
      return pp.p == p;
    }
    bool operator!=(const Ptr_ref& pp) const
    {
      return pp.p != p;
    }
    TD operator*()
    {
      return {p, allo};
    }
  };

  Ptr_ref begin()
  {
    return {first, allo};
  }
  Ptr_ref end()
  {
    return {last, allo};
  }

  const Ptr_ref begin() const
  {
    return {first, allo};
  }

  const Ptr_ref end() const
  {
    return {last, allo};
  }

  bool is_present() const
  {
    return size();
  } // pretend to be optional

  bool is_empty() const
  {
    return size() == 0;
  } // pretend to be a container
  
  int size() const
  {
    return last - first;
  }

  TD operator[](int i)
  {
    expect([&] { return i < size(); }, Error_code::bad_span_index);
    return TD{first + i, allo};
  }
};

template <class T>
struct Optional_init
{
  bool filled;
  T val;
  Optional_init(const T& x) : filled{true}, val(x)
  {
  }
  Optional_init(const char* x) : filled{true}, val(x)
  // C-style strings are very special
  {
  } 
  Optional_init(Empty) : filled{false}
  {
  }
};

template <class T, class X>
void place_one_alloc(Allocator* a, T* t, const X& x)
{
  new (t) T(a, x);
}

template <class T, class X>
void place_one(T* t, const X& x)
{
  new (t) T(x);
}

template <class T, class X>
void place(T* t, std::initializer_list<X> lst)
// initialize T[i] from lst[i]
{
  int n = 0;
  for (auto& x : lst)
  {
    place_one(&t[n], x);
    ++n;
  };
}

template <class T, class X>
void place([[maybe_unused]] Allocator* a, T* t, std::initializer_list<Optional_init<X>> lst)
// initialize T[] from lst
{
  int n = 0;
  for (auto& x : lst)
  {
    if (x.filled)
    {
      if constexpr (std::is_constructible<T, Allocator*, X>::value)
        place_one_alloc(a, &t[n], x.val);
      else if constexpr (std::is_constructible<T, X>::value)
        place_one(&t[n], x.val);
      else
        static_assert(False<>, "Cannot construct X from Optional<X>");
    }
    else if constexpr (std::is_constructible<T, Empty>::value)
      place_one(&t[n], Empty{}); // e.g., Optional<Vector<int>>
    else
      static_assert(False<>, "Cannot construct X from Empty{}");
    ++n;
  }
}

template <class T, class X>
void place(Allocator* a, T* t, std::initializer_list<X> lst)
// initialize T[i] from lst[i]
{
  int n = 0;
  for (auto& s : lst)
  {
    if constexpr (std::is_constructible<T, Allocator*, X>::value)
      place_one_alloc(a, &t[n], s);
    else if constexpr (std::is_constructible<T, X>::value)
    {
      place_one(&t[n], s);
      ++a; // suppress warning
    }
    else
      static_assert(False<>, "Cannot construct T from initializer_list element");
    ++n;
  };
}

template <class T, class X>
void place(T* t, std::initializer_list<Optional_init<X>> lst)
// initialize T[] from lst
{
  int n = 0;
  for (auto& x : lst)
  {
    if (x.filled)
    {
      if constexpr (std::is_constructible<T, X>::value)
        place_one(&t[n], x.val);
      else
        static_assert(False<>, "Cannot construct T from Optional<X>");
    }
    else if constexpr (std::is_constructible<T, Empty>::value)
      place_one(&t[n], Empty{}); // e.g., Optional<Vector<int>>
    else
      static_assert(False<>, "Cannot construct T from Empty{}");
    ++n;
  }
}

template <class T>
struct Optional
{
  using value_type = T;
  bool filled;
  T val;

  Optional& operator=(const Optional&) = delete;
  Optional(const Optional&) = delete;

  Optional(Empty) : filled{false}
  {
  } // for optionals as list elements

  Optional(Default) : filled{true}, val{}
  {
  } // for optionals as list elements

  Optional() : filled{false}
  {
  }

  Optional(Allocator*, Empty) : filled{false}
  {
  }

  Optional(Allocator*, Default) : filled{true}, val{}
  {
  }
  
  Optional(const T& x) : filled{true}, val(x)
  {
  }

  Optional(Allocator* a, const std::string& s) : filled{true}, val(a, s)
  {
  }

  template <class X>
  Optional(std::initializer_list<X> lst) : filled{true}, val(lst)
  {
  }

  template <class X>
  Optional(Allocator* a, std::initializer_list<X> lst)
    : filled{true}, val(a, lst)
  {
  }

  bool is_present() const
  {
    return filled;
  }
  bool is_empty() const
  {
    return filled == false;
  } // pretend to be a container

  void operator=(const T& x)
  {
    val = x;
    filled = true;
  }

  operator T&()
  {
    return access();
  } // what about Flats?

  T& access()
  {
    expect([&] { return is_present(); }, Error_code::optional_not_present);
    return val;
  }

  const T& access() const
  {
    expect([&] { return is_present(); }, Error_code::optional_not_present);
    return val;
  }

  operator T&() requires is_concrete<T>
  {
    return access();
  }

  operator const T&() const requires is_concrete<T>
  {
    return access();
  }

  // do we really need operator*() ???

  T& operator*() requires is_concrete<T>
  {
    return access();
  }

  const T& operator*() const requires is_concrete<T>
  {
    return access();
  }

  auto operator*() requires is_container<T>
  {
    auto& v = access();
    return Span<typename T::value_type>{v.begin(), v.end()};
  }

  auto operator*() const requires is_container<T>
  {
    auto& v = access();
    return Span<const typename T::value_type>{v.begin(), v.end()};
  }

  bool operator==(const T& x) const
  {
    return access() == x;
  }
};

/*
template <class T, class TD>
struct Optional_ref
{ // accessor to optional<T>
  Optional<T>* val;
  Allocator* allo;

  bool is_present() const
  {
    return val->filled;
  }
  bool is_empty() const
  {
    return !is_present();
  } // pretend to be a container

  Optional_ref(Allocator* a, Optional<T>* v) : val{v}, allo{a}
  {
  }

  void operator=(const T& x)
  {
    val->val = x;
    val->filled = true;
  }

  operator TD()
  {
    return access();
  }

  auto access()
  {
    expect([&] { return is_present(); }, Error_code::optional_not_present);
    return TD{&val->val, allo};
  }
};
*/
template <typename T>
struct Vector
{ // Refers to "the variable part" of a message starting at sizeof(this message)";
  using value_type = T;
  constexpr static bool vector_tag = true;
  Size sz = 0; // number of Ts
  Offset pos = 0; // pos is relative to the position of this, so char* p = (char*)this+pos; Is 16 bits sufficent for every object? ???

  Size nbytes() const
  {
    return sz * sizeof(T);
  } // number of bytes of elements
  bool is_empty() const
  {
    return sz == 0;
  }
  bool is_present() const
  {
    return sz;
  } // pretend to be optional

  Vector& access() // pretend to be optional
  {
    expect([&] { return is_present(); }, Error_code::optional_not_present);
    return *this;
  }

  Offset alloc(Allocator* a) // allocate length bytes in the tail and return the relative position of the first of those bytes
  {
    pos = a->allocate(sz * sizeof(T)); // position in Flat
    pos -= reinterpret_cast<Byte*>(this) - a->flat(); // position relative to this
    return pos;
  }

  Vector() = default; // : sz{ 0 }, pos{ 0 } {}
  Vector& operator=(const Vector&) = delete; // no copying
  Vector(const Vector&) = delete;

  Vector(Allocator* a, Extent sz) // sz uninitialized elements
    : sz{sz.sz}, pos{alloc(a)}
  {
  }

  template <class X>
  Vector(Allocator* a, std::initializer_list<X> lst)
    : sz{narrow(lst.size())}, pos{alloc(a)}
  {
    place(a, begin(), lst);
  }

  Vector(Allocator* a, const std::string& s)
    : sz{static_cast<Size>(s.size())}, pos{alloc(a)}
  {
    std::copy(s.data(), s.data() + s.size(), begin());
  }

  Vector(Allocator* a, const char* s) // allocate the C-style string s in the tail
  {
    auto r = a->place(s);
    pos = r.pos - (reinterpret_cast<Byte*>(this) - a->flat());
    sz = r.sz;
  }

  operator Span<T>()
  {
    return {begin(), end()};
  } // Span is range checked by default
  T* begin()
  {
    auto p = reinterpret_cast<char*>(this) + pos;
    return reinterpret_cast<T*>(p);
  }
  T* end()
  {
    return begin() + size();
  }

  operator Span<const T>() const
  {
    return {begin(), end()};
  }
  const T* begin() const
  {
    auto p = reinterpret_cast<const char*>(this) + pos;
    return reinterpret_cast<const T*>(p);
  }
  const T* end() const
  {
    return begin() + size();
  }

  Size size() const
  {
    return sz;
  }

  // you can push if this vector is the last allocated and there is free space

  void push(Allocator* a)
  {
    expect([a, this] { return this->can_push(a); }, Error_code::fixed_array_overflow);
    a->allocate(sizeof(T));
    ++sz;
  }

  void push(Allocator* a, T v)
  {
    push(a);
    begin()[sz - 1] = v;
  }

  int can_push(Allocator* a) // return number o f spare slots
  {
    auto pos = a->next + a->flat();
    if (pos != reinterpret_cast<Byte*>(this->end()))
      return 0;
    pos = a->max - sizeof(T) + a->flat();
    //	std::cout << "push vec " << (Byte*)(this->end()) << " " << pos << " " << sizeof(T) << '\n';
    //	std::cout << (pos-reinterpret_cast<Byte*>(this->end()))/sizeof(T) << '\n';
    return (pos - reinterpret_cast<Byte*>(this->end())) / sizeof(T);
  }

  void push(Allocator* a, const char* v)
  {
    push(a);
    place_one_alloc(a, &begin()[sz - 1], v);
  }
  // subscripting, range checking: use Span
};

inline bool operator==(Span<char> sp, const char* p)
{
  for (char ch : sp)
  {
    if (*p == 0 && ch == 0)
      return true;
    if (*p++ != ch)
      return false;
  }
  return true;
}

inline bool operator==(const char* p, Span<char> sp)
{
  return sp == p;
}

inline bool operator!=(Span<char> sp, const char* p)
{
  return !(sp == p);
}

inline bool operator!=(const char* p, Span<char> sp)
{
  return !(sp == p);
}

inline bool operator==(Span<char> sp, const std::string& s)
{
  if (sp.size() != size_of(s))
    return false;
  auto p = &*s.cbegin();
  for (char ch : sp)
    if (*p++ != ch)
      return false;
  return true;
}

inline bool operator==(const std::string& s, Span<char> sp)
{
  return sp == s;
}

inline bool operator!=(const std::string& s, Span<char> sp)
{
  return !(sp == s);
}

inline bool operator!=(Span<char> sp, const std::string& s)
{
  return !(sp == s);
}

using String = Vector<char>;

template <class T, int N>
struct Array
{ // like Span, a pure accessor; N consecutive elements of type T
  using value_type = T;
  constexpr static bool array_tag = true;
  T val[N];

  operator Span<T>()
  {
    return {begin(), end()};
  }

  T* begin()
  {
    return &val[0];
  }
  T* end()
  {
    return &val[N];
  }
  const T* begin() const
  {
    return &val[0];
  }
  const T* end() const
  {
    return &val[N];
  }

  Size size() const
  {
    return N;
  }
  Size Max_size() const
  {
    return N;
  }

  Array()
  {
  }

  template <class X>
  Array(std::initializer_list<X> lst)
  {
    //		std::cerr << "construct array: " << int(this) << ' ' << N << '\n';
    expect([&] { return N == lst.size(); }, Error_code::array_initializer); // insist on all elements initialized ???
    place(&val[0], lst);
  }

  Array(const char* str)
  {
    for (char& x : val)
      x = *str++;
    expect<check_truncation>([&] { return *str == 0; }, Error_code::truncation);
  }

  template <class X>
  Array(Allocator* a, std::initializer_list<X> lst)
  {
    expect([&] { return N == lst.size(); }, Error_code::array_initializer); // insist on all elements initialized ???
    place(a, &val[0], lst); // would require copy constructor for T
  }
};

struct Push
{
}; // Used in user interface code to indicate a wish to push()

#include <cassert>
template <class T, int N>
struct Fixed_vector
{ // An array that keeps track of how many elements are used
  // could also be seen as a vector with a fixed size
  // Ideally Fixed_vector<T,N> would have an Array<T,N> as a member, but implicit conversions would be messed up
  // Also, deriving Fixed_vector<T,N> from Array<T,N> would place the count at the end
  using value_type = T;
  constexpr static bool array_tag = true;
  Size used = 0;
  T val[N];

  operator Span<T>()
  {
    return {begin(), end()};
  }

  T* begin()
  {
    return &val[0];
  }
  T* end()
  {
    return &val[used];
  }
  const T* begin() const
  {
    return &val[0];
  }
  const T* end() const
  {
    return &val[used];
  }

  Size size() const
  {
    return used;
  }
  Size Max_size() const
  {
    return N;
  }

  Fixed_vector()
  {
  }

  template <class X>
  Fixed_vector(std::initializer_list<X> lst)
  {
    expect([&] { return lst.size() <= N; }, Error_code::array_initializer);
    place(&val[0], lst);
    used = lst.size();
  }

  Fixed_vector(const char* str)
  {
    for (char& x : val)
    {
      x = *str++;
      if (x == 0)
        break;
      ++used;
    }
    expect<check_truncation>([&] { return *str; }, Error_code::truncation);
  }

  template <class X>
  Fixed_vector(Allocator* a, std::initializer_list<X> lst) : used{lst.size()}
  {
    expect([&] { return lst.size() <= N; }, Error_code::array_initializer); // insist on all elements initialized ???
    place(a, &val[0], lst); // would require copy constructor for T
  }

  Fixed_vector(Extent ex) // sz uninitialized elements
  {
    auto n = ex.sz;
    expect([n] { return 0 <= n && n <= N; }, Error_code::fixed_array_overflow);
    used = n;
  }

  void push()
  {
    expect([this] { return this->used < N; }, Error_code::fixed_array_overflow);
    ++used;
  }

  void push(T v)
  {
    expect([this] { return this->used < N; }, Error_code::fixed_array_overflow);
    val[used++] = v;
  }

  int can_push()
  {
    return N - used;
  }

  void push(Allocator* a, const char* v)
  {
    expect([this] { return this->used < N; }, Error_code::fixed_array_overflow);
    place_one_alloc(a, &val[used++], v);
  }
};

inline std::ostream& operator<<(std::ostream& out, Span<char> s)
{
  for (char x : s)
    out << x;
  return out;
}

template <class T>
inline std::ostream& operator<<(std::ostream& out, Span<T> s)
{
  out << '{';
  for (const T& x : s)
    out << x << ", ";
  out << '}';
  return out;
}

template <class T>
std::ostream& operator<<(std::ostream& os, Optional<T>& opt)
{
  return os << opt.access();
}
/*
	template<class T>
	std::ostream& operator << (std::ostream& os, Vector<T>& v)
	{
		return os << Span<T>(v);
	}

	template<class T, int N>
	std::ostream& operator << (std::ostream& os, Array<T, N>& v)
	{
		return os << Span<T>(v);
	}

	template<class T, int N>
	std::ostream& operator << (std::ostream& os, Fixed_vector<T, N>& v)
	{
		return os << Span<T>(v);
	}
*/
template <is_container T>
std::ostream& operator<<(std::ostream& os, T& v)
{
  return os << Span<T>(v);
}

} // namespace Flats
