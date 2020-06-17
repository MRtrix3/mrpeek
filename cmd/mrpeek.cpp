#include "command.h"
#include "image.h"
#include "algo/loop.h"
#include "sixel.h"

using namespace MR;
using namespace App;


#define DEFAULT_PMIN 0.2
#define DEFAULT_PMAX 99.8

vector<std::string> colourmap_choices_std;
vector<const char*> colourmap_choices_cstr;


// commmand-line description and syntax:
// (used to produce the help page and verify validity of arguments at runtime)
void usage ()
{
  // lifted from cmd/mrcolour.cpp:
  const ColourMap::Entry* entry = ColourMap::maps;
  do {
    if (strcmp(entry->name, "Complex"))
      colourmap_choices_std.push_back (lowercase (entry->name));
    ++entry;
  } while (entry->name);
  colourmap_choices_cstr.reserve (colourmap_choices_std.size() + 1);
  for (const auto& s : colourmap_choices_std)
    colourmap_choices_cstr.push_back (s.c_str());
  colourmap_choices_cstr.push_back (nullptr);



  AUTHOR = "Joe Bloggs (joe.bloggs@acme.org)";

  SYNOPSIS = "preview images on the terminal (requires terminal with sixel support)";

  ARGUMENTS
  + Argument ("in", "the input image.").type_image_in ();

  OPTIONS
  + Option ("axis",
            "specify projection of slice, as an integer representing the slice normal: "
            "0: L/R (sagittal); 1: A/P (coronal); 2 I/S (axial). Default is 2 (axial). ")
  +   Argument ("index").type_integer (0)

  + Option ("slice",
            "select slice to display")
  +   Argument ("number").type_integer(0)

  + Option ("intensity_range",
            "specify intensity range of the data. The image intensity will be scaled "
            "between the specified minimum and maximum intensity values. "
            "By default, percentile scaling is used. ")
  +   Argument ("min").type_float()
  +   Argument ("max").type_float()

  + Option ("percentile_range",
            "specify intensity range of the data. The image intensity will be scaled "
            "between the specified minimum and maximum percentile values. "
            "Defaults are: " + str(DEFAULT_PMIN) + " - " + str(DEFAULT_PMAX))
  +   Argument ("min").type_float()
  +   Argument ("max").type_float()

  + Option ("colourmap",
            "the colourmap to apply; choices are: " + join(colourmap_choices_std, ",") +
            ". Default is " + colourmap_choices_std[0] + ".")
  +   Argument ("name").type_choice (colourmap_choices_cstr.data())

  + Option ("crosshairs",
            "draw crosshairs at specified position")
  +   Argument ("x").type_integer(0)
  +   Argument ("y").type_integer(0)

  + Option ("levels",
            "number of intensity levels in the colourmap. Default is 100.")
  +   Argument ("number").type_integer (2);

}



using value_type = float;


// calculate percentile of a list of numbers
// implementation based on `mrthreshold` - can be merged with Math::median in due course
template <class Container>
value_type percentile (Container& data, default_type percentile)
{
  if (percentile == 100.0) {
    return default_type(*std::max_element (data.begin(), data.end()));
  } else if (percentile == 0.0) {
    return default_type(*std::min_element (data.begin(), data.end()));
  } else {
    const default_type interp_index = 0.01 * percentile * (data.size()-1);
    const size_t lower_index = size_t(std::floor (interp_index));
    const default_type mu = interp_index - default_type(lower_index);
    std::nth_element (data.begin(), data.begin() + lower_index, data.end());
    const default_type lower_value = default_type(data[lower_index]);
    std::nth_element (data.begin(), data.begin() + lower_index + 1, data.end());
    const default_type upper_value = default_type(data[lower_index + 1]);
    return (1.0-mu)*lower_value + mu*upper_value;
  }
}



void run ()
{
  auto image_in = Image<value_type>::open (argument[0]);

  int axis = get_option_value ("axis", 2);
  int slice = get_option_value ("slice", image_in.size(axis)/2);

  int colourmap_ID = get_option_value ("colourmap", 0);
  const auto colourmapper = ColourMap::maps[colourmap_ID];
  int levels = get_option_value ("levels", 100);

  int x_axis, y_axis;
  bool x_forward, y_forward;
  switch (axis) {
    case 0: x_axis = 1; y_axis = 2; x_forward = false; y_forward = false; break;
    case 1: x_axis = 0; y_axis = 2; x_forward = false; y_forward = false; break;
    case 2: x_axis = 0; y_axis = 1; x_forward = false; y_forward = false; break;
    default: throw Exception ("invalid axis specifier");
  }

  const int x_dim = image_in.size(x_axis);
  const int y_dim = image_in.size(y_axis);

  image_in.index(axis) = slice;

  value_type vmin, vmax;
  auto opt = get_options ("intensity_range");
  if (opt.size()) {
    vmin = opt[0][0]; vmax = opt[0][1];
  } else {
    float pmin = DEFAULT_PMIN, pmax = DEFAULT_PMAX;
    opt = get_options ("percentile_range");
    if (opt.size()) {
      pmin = opt[0][0]; pmax = opt[0][1];
    }
    std::vector<value_type> currentslice (x_dim*y_dim);
    size_t k = 0;
    for (int y = 0; y < y_dim; ++y) {
      image_in.index(y_axis) = y_forward ? y : y_dim-1-y;
      for (int x = 0; x < x_dim; ++x, ++k) {
        image_in.index(x_axis) = x_forward ? x : x_dim-1-x;
        currentslice[k] = image_in.value();
      }
    }
    vmin = percentile(currentslice, pmin);
    vmax = percentile(currentslice, pmax);
    INFO("Selected intensity range: " + str(vmin) + " - " +str(vmax));
  }

  value_type scale = 1.0 / (vmax - vmin);
  value_type offset = -scale*vmin;
  Sixel::ColourMap colourmap (colourmapper, levels);
  colourmap.set_scaling (offset, scale);

  Sixel::Encoder encoder (x_dim, y_dim, colourmap);

  for (int y = 0; y < y_dim; ++y) {
    image_in.index(y_axis) = y_forward ? y : y_dim-1-y;
    for (int x = 0; x < x_dim; ++x) {
      image_in.index(x_axis) = x_forward ? x : x_dim-1-x;
      encoder(x, y, image_in.value());
    }
  }

  opt = get_options ("crosshairs");
  if (opt.size())
    encoder.draw_crosshairs (opt[0][0], opt[0][1]);

  // encode buffer and print out:
  encoder.write();


  VT::enter_raw_mode();

  while (int c = VT::read_user_input()) {
    if (c == 'q')
      break;
  }

  VT::exit_raw_mode();

}
