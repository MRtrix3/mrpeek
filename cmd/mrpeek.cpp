#include <termios.h>

#include "command.h"
#include "image.h"
#include "algo/loop.h"
#include "interp/nearest.h"
#include "filter/resize.h"

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
  +   Argument ("number").type_integer (2)

  + Option   ("image_scale",
            "scale the image size by the supplied factor")
    + Argument ("factor").type_float();
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
  auto header_in = Header::open (argument[0]);
  Image<value_type> image_in = header_in.get_image<value_type>();

  int axis = get_option_value ("axis", 2);
  int slice = get_option_value ("slice", image_in.size(axis)/2);
  if (slice >= image_in.size(axis))
    throw Exception("slice " + str(slice) + " exceeds image size (" + str(image_in.size(axis)) + ") in axis " + str(axis));

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

  Header header_target (header_in);
  default_type image_scale = 1.0;
  default_type original_extent;
  auto opt = get_options ("image_scale");
  if (opt.size()) {
    image_scale = opt[0][0];
    vector<default_type> new_voxel_size (3, 1.0);
    for (int d = 0; d < 3; ++d) {
      if (d != axis)
        new_voxel_size[d] = (header_in.size(d) * header_in.spacing(d)) / std::ceil (header_in.size(d) * image_scale);
      else
        new_voxel_size[d] = header_in.spacing(d);
      original_extent = header_in.size(d) * header_in.spacing(d);

      header_target.size(d) = std::round (header_in.size(d) * header_in.spacing(d) / new_voxel_size[d] - 0.0001); // round down at .5
      for (size_t i = 0; i < 3; ++i)
        header_target.transform()(i,3) += 0.5 * ((new_voxel_size[d] - header_target.spacing(d)) + (original_extent - (header_target.size(d) * new_voxel_size[d]))) * header_target.transform()(i,d);
      header_target.spacing(d) = new_voxel_size[d];
    }
  }

  Adapter::Reslice<Interp::Nearest, Image<value_type>> image_regrid(image_in, header_target, Adapter::NoTransform, Adapter::AutoOverSample); // out_of_bounds_value

  const int x_dim = image_regrid.size(x_axis);
  const int y_dim = image_regrid.size(y_axis);

  image_regrid.index(axis) = slice;

  value_type vmin, vmax;
  opt = get_options ("intensity_range");
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
      image_regrid.index(y_axis) = y_forward ? y : y_dim-1-y;
      for (int x = 0; x < x_dim; ++x, ++k) {
        image_regrid.index(x_axis) = x_forward ? x : x_dim-1-x;
        currentslice[k] = image_regrid.value();
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

  int crosshairs_x, crosshairs_y;  // relative to original image grid
  opt = get_options ("crosshairs");
  if (opt.size()){
    const int x = opt[0][0], y = opt[0][1];
    crosshairs_x = x_forward ? x : image_in.size(x_axis)-1-x;
    crosshairs_y = y_forward ? y : image_in.size(y_axis)-1-y;
  }


  struct termios old_termios;
  struct termios new_termios;
  tcgetattr(STDIN_FILENO, &old_termios);
  (void) memcpy(&new_termios, &old_termios, sizeof(old_termios));
  new_termios.c_iflag &= ~(ICRNL | IXON);
  new_termios.c_oflag &= ~(OPOST);
  new_termios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  // start loop
  std::cout.write("\033[2J", 4);
  int slice_curr = slice;
  while (true) {
    image_regrid.index(axis) = slice_curr;
    std::cout.write("\033[H", 3);

    for (int y = 0; y < y_dim; ++y) {
      image_regrid.index(y_axis) = y_forward ? y : y_dim-1-y;
      for (int x = 0; x < x_dim; ++x) {
        image_regrid.index(x_axis) = x_forward ? x : x_dim-1-x;
        encoder(x, y, image_regrid.value());
      }
    }

    if (opt.size()) {
      encoder.draw_crosshairs (std::round(image_scale * (crosshairs_x + 0.5)), std::round(image_scale * (crosshairs_y + 0.5)));
    }

    // encode buffer and print out:
    encoder.write();

    // enter cbreak mode
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios);

    // read input
    char c;
    std::cin.get(c);
    if (c == 27) { // escape
      std::cin.get(c);
      if (c == 91) {
        std::cin.get(c);
        switch (c) {
          case 53:
            std::cin.get(c);
            if (c == 126){
              slice_curr++; //page up
              break;
            }
            break;
          case 54:
            std::cin.get(c);
            if (c == 126){
              slice_curr--; //page down
              break;
            }
            break;
          case 65: crosshairs_y--; break; // up
          case 66: crosshairs_y++; break; // down
          case 67: crosshairs_x++; break; // right
          case 68: crosshairs_x--; break; // left
        }
      }
    }
    else if (c == 'q' || c == 'Q')
      break;

    // back to normal mode
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);
  }
  // make sure we end in non-cbreak mode
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);

}
