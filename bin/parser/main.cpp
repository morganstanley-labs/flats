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
    Flats parser driver
*/

#include "flat.h"
#include "object_map.h"
#include <fstream>

void print_direct(const Flat& flt, std::ostream& out, bool packed = false);
void print_view(const Flat& flt, std::ostream& out);

using namespace std;

static ostream* osp = &cout;
static istream* isp = &cin;

ostream& os()
{
  return *osp;
}

istream& is()
{
  return *isp;
}

string get_arg(const string& prompt, int index, int argc, char* argv[])
{
  string val;
  if (argc == 4)
  {
    val = argv[index];
  }
  else
  {
    cout << prompt;
    cin >> val;
  }
  return val;
}

ostream* get_output(const string& name)
{
  if (name == "")
    return &cout;
  if (auto p = new ofstream(name))
    return p; // leaks file stream unless deleted ???
  error("can't open output file", name);
}

istream* get_input(const string& name)
{
  if (name == "")
    return &cin;
  if (auto p = new ifstream(name))
    return p; // leaks file stream unless deleted ???
  error("can't open input file", name);
}

void print_debug(Flat* flt)
{
  print(*flt);
  Object_map m = make_object_map(*flt);
  print(m, os());
  print_direct(*flt, os());
  print_view(*flt, os());
}

enum class Act
{
  unknown,
  debug,
  cpp_direct,
  cpp_packed,
  cpp_view,
  packed_view,
  obj_map
};

map<string, Act> actions = {
  {"", Act::unknown},          {"debug", Act::debug},
  {"direct", Act::cpp_direct}, {"packed", Act::cpp_packed},
  {"view", Act::cpp_view},     {"packed_view", Act::packed_view}};

Act select_action(const string& name)
{
  return actions[name];
}

int main(int argc, char* argv[])
/*
    zero arguments: command from cin to cout
    N arguments: command input-file output-file+
*/
try
{
  vector<string> argument(argv, argv + argc);
  if (argument.size() == 1)
    error("no arguments to parser");
  int i = 0;
  for (auto& x : argument)
    cout << i++ << ": " << x << "\n";
  const string& command = argument[1];
  string ifile;
  string ofile; // output file for C++
  string odir; // output directory for Java (one file per class)
  if (1 < argument.size())
    ifile = argument[2];
  if (2 < argument.size())
    ofile = argument[3];
  if (3 < argument.size())
    odir = argument[4];
  if (5 < argument.size())
    error("too many output files");

  isp = get_input(ifile); // default: cin
  osp = get_output(ofile); // default: cout

  auto act = select_action(command);
  if (act == Act::unknown)
    error("parser: unknown action");
  // cerr << "action: " << static_cast<int>(act) << '\n';

  auto flats = parse();
  //cerr << "\nparse complete: " << flats.size() << " types\n";

  switch (act)
  { // prefixes
    case Act::cpp_direct:
      //    case Act::cpp_packed:
    case Act::cpp_view:
      //    case Act::packed_view:
      os() << "#include<cstdint>\n";
    default:
      break;
  };

  for (auto& flt : flats)
  {
    if (flt->id == Type_id::enumeration)
      continue;
    bool packed = [act] { // overly clever ?
      switch (act)
      {
          //    case Act::cpp_packed:
          //    case Act::packed_view:
          //        return true;
        default:
          return false;
      };
    }();

    Object_map m = make_object_map(*flt, packed); // does all size and position calculations
    flt->omap = &m;
    flt->packed = packed;

    switch (act)
    {
      case Act::unknown:
        error("sorry, unknown action");
        break;
      case Act::debug:
        print_debug(flt);
        break;
      case Act::cpp_direct:
      case Act::cpp_packed:
        os() << "namespace Flats {\n";
        print_struct(*flt, os(), packed);
        print_direct(*flt, os());
        os() << "} // namespace Flats\n\n";
        break;
      case Act::cpp_view:
      case Act::packed_view:
        os() << "namespace Flats {\n";
        // assumes that the struct has been output
        print_view(*flt, os());
        os() << "} // namespace Flats\n";
        break;
      case Act::obj_map:
        print(m, os());
        break;
      default:
        error("unknown request", static_cast<int>(act));
    }
  }

  if (isp != &cin)
    delete isp; // smells
  if (osp != &cout)
    delete osp;
}
catch (...)
{
  cerr << "parser abnormal termination\n";
  cerr << "press '~' to terminate\n";
  char ch;
  while (cin >> ch && ch != '~')
    cout << "?\n"; // for simplest Window: don't close immediately
}