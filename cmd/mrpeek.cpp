#include "command.h"
#include "image.h"
#include "algo/loop.h"
#include "adapter/extract.h"

using namespace MR;
using namespace App;

// commmand-line description and syntax:
// (used to produce the help page and verify validity of arguments at runtime)
void usage ()
{
  AUTHOR = "Joe Bloggs (joe.bloggs@acme.org)";

  SYNOPSIS = "raise each voxel intensity to the given power (default: 2)";

  ARGUMENTS
  + Argument ("in", "the input image.").type_image_in ();

  OPTIONS
  + Option ("coord",
            "retain data from the input image only at the coordinates "
            "specified in the selection along the specified axis. The selection "
            "argument expects a number sequence, which can also include the "
            "'end' keyword.").allow_multiple()
    + Argument ("axis").type_integer (0)
    + Argument ("selection").type_sequence_int();
}



using value_type = float;

void run ()
{
  auto image_in = Image<value_type>::open (argument[0]);

  auto opt = get_options ("coord");
  vector<vector<int>> pos;
  if (opt.size()) {
    pos.assign (image_in.ndim(), vector<int>());
    for (size_t n = 0; n < opt.size(); n++) {
      int axis = opt[n][0];
      if (axis >= (int)image_in.ndim())
        throw Exception ("axis " + str(axis) + " provided with -coord option is out of range of input image");
      if (pos[axis].size())
        throw Exception ("\"coord\" option specified twice for axis " + str (axis));
      pos[axis] = parse_ints (opt[n][1], image_in.size(axis)-1);

      auto minval = std::min_element(std::begin(pos[axis]), std::end(pos[axis]));
      if (*minval < 0)
        throw Exception ("coordinate position " + str(*minval) + " for axis " + str(axis) + " provided with -coord option is negative");
      auto maxval = std::max_element(std::begin(pos[axis]), std::end(pos[axis]));
      if (*maxval >= image_in.size(axis))
        throw Exception ("coordinate position " + str(*maxval) + " for axis " + str(axis) + " provided with -coord option is out of range of input image");
    }

    for (size_t n = 0; n < image_in.ndim(); ++n) {
      if (pos[n].empty()) {
        pos[n].resize (image_in.size (n));
        for (size_t i = 0; i < pos[n].size(); i++)
          pos[n][i] = i;
      }
    }
    size_t ndims = 0;
    for (size_t n = 0; n < image_in.ndim(); ++n) {
      if (pos[n].size() > 1) {
        INFO("input " + str(n) + ": positions " + str(pos[n]));
        ++ndims;
      }
    }

    if (ndims != 2)
      throw Exception ("2D slice required but coordinate selection is " + str(ndims)+"D");

    auto extract = Adapter::make<Adapter::Extract> (image_in, pos);
    for (size_t n = 0; n < extract.ndim(); ++n)
      INFO("extracted " + str(n) + ": " + str(extract.size(n)));

    std::string s;
    std::stringstream ss;

    ss << static_cast<char>(27);
    ss << "Pq";
    ss << "#0;2;0;0;0#1;2;100;100;0#2;2;0;100;0";
    ss << "#1~~@@vv@@~~@@~~$";
    ss << "#2?" "?}}GG}}?" "?}}?" "?-";
    ss << "#1!14@";
    ss << static_cast<char>(27);
    ss << "\\";

    std::cout << ss.str() << std::endl;
  }
}
