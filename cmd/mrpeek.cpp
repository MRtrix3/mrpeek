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




// Global variables to hold slide dislay parameters:
// These will need to be moved into a struct/class eventually...
int colourmap_ID = 0;
int levels = 100;
value_type image_scale = 1.0;
int x_axis, y_axis, slice_axis = 2;
value_type pmin = DEFAULT_PMIN, pmax = DEFAULT_PMAX;
bool crosshair = true;
vector<int> focus (3, 0);  // relative to original image grid



inline void set_axes ()
{
  switch (slice_axis) {
    case 0: x_axis = 1; y_axis = 2; break;
    case 1: x_axis = 0; y_axis = 2; break;
    case 2: x_axis = 0; y_axis = 1; break;
    default: throw Exception ("invalid axis specifier");
  }
}



void display (Image<value_type>& image, Sixel::ColourMap& colourmap)
{
  set_axes();

  Header header_target (image);
  float new_voxel_size = 1.0;
  float scale = std::min (std::min (image.spacing(0), image.spacing(1)), image.spacing(2)) / image_scale;

  default_type original_extent;
  for (int d = 0; d < 3; ++d) {
    if (d == slice_axis)
      new_voxel_size = image.spacing(d);
    else
      new_voxel_size = scale;

    original_extent = image.size(d) * image.spacing(d);

    header_target.size(d) = std::round (image.size(d) * image.spacing(d) / new_voxel_size - 0.0001); // round down at .5
    for (size_t i = 0; i < 3; ++i)
      header_target.transform()(i,3) += 0.5 * ((new_voxel_size - header_target.spacing(d)) + (original_extent - (header_target.size(d) * new_voxel_size))) * header_target.transform()(i,d);
    header_target.spacing(d) = new_voxel_size;
  }

  Adapter::Reslice<Interp::Nearest, Image<value_type>> image_regrid(image, header_target, Adapter::NoTransform, Adapter::AutoOverSample); // out_of_bounds_value

  const int x_dim = image_regrid.size(x_axis);
  const int y_dim = image_regrid.size(y_axis);

  for (int n = 0; n < 3; ++n) {
    if (focus[n] < 0) focus[n] = 0;
    if (focus[n] >= image.size(n)) focus[n] = image.size(n)-1;
  }


  image_regrid.index(slice_axis) = focus[slice_axis];


  if (!colourmap.scaling_set()) {
    // reset scaling:

    std::vector<value_type> currentslice (x_dim*y_dim);
    size_t k = 0;
    for (auto l = Loop ({0,1})(image_regrid); l; ++l)
      currentslice[k++] = image_regrid.value();

    value_type vmin = percentile(currentslice, pmin);
    value_type vmax = percentile(currentslice, pmax);
    colourmap.set_scaling_min_max (vmin, vmax);
    INFO("reset intensity range to " + str(vmin) + " - " +str(vmax));
  }

  Sixel::Encoder encoder (x_dim, y_dim, colourmap);

  for (int y = 0; y < y_dim; ++y) {
    image_regrid.index(y_axis) = y_dim-1-y;
    for (int x = 0; x < x_dim; ++x) {
      image_regrid.index(x_axis) = x_dim-1-x;
      encoder(x, y, image_regrid.value());
    }
  }

   if (crosshair)
    encoder.draw_crosshairs (std::round(x_dim - image_scale * (focus[x_axis] - 0.5)), std::round(y_dim - image_scale * (focus[y_axis] - 0.5)));


  // encode buffer and print out:
  encoder.write();

  image.index(0) = focus[0];
  image.index(1) = focus[1];
  image.index(2) = focus[2];
  std::cout << VT::CarriageReturn << VT::ClearLine << "[ " << focus[0] << " " << focus[1] << " " << focus[2] << " ";
  for (size_t n = 3; n < image.ndim(); ++n)
    std::cout << image.index(n) << " ";
  std::cout << "]: " << image.value();

  std::cout.flush();
}





void run ()
{
  auto image = Image<value_type>::open (argument[0]);

  slice_axis = get_option_value ("axis", slice_axis);
  focus[slice_axis] = get_option_value ("slice", image.size(slice_axis)/2);
  set_axes();
  focus[x_axis] = std::round (image.size(x_axis)/2);
  focus[y_axis] = std::round (image.size(y_axis)/2);


  if (focus[slice_axis] >= image.size(slice_axis))
    throw Exception("slice " + str(focus[slice_axis]) + " exceeds image size (" + str(image.size(slice_axis)) + ") in axis " + str(slice_axis));

  colourmap_ID = get_option_value ("colourmap", colourmap_ID);
  levels = get_option_value ("levels", levels);

  Sixel::ColourMap colourmap (ColourMap::maps[colourmap_ID], levels);

  auto opt = get_options ("intensity_range");
  if (opt.size()) {
    colourmap.set_scaling_min_max (opt[0][0], opt[0][1]);
  }

  opt = get_options ("percentile_range");
  if (opt.size()) {
    pmin = opt[0][0];
    pmax = opt[0][1];
  }

  opt = get_options ("crosshairs");
  if (opt.size()) {
    focus[x_axis] = opt[0][0];
    focus[y_axis] = opt[0][1];
  }

  image_scale = get_option_value ("image_scale", image_scale);


  // start loop
  VT::enter_raw_mode();
  try {
    std::cout << VT::ClearScreen;

    int event = 0;
    int x, y, xp = 0, yp = 0;
    bool need_update = true;

    do {

      while ((event = VT::read_user_input(x, y)) == 0) {
        if (need_update) {
          std::cout << VT::CursorHome;
          display (image, colourmap);
          need_update = false;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds(10));
      }

      need_update = true;

      switch (event) {
        case VT::Up:
        case VT::MouseWheelUp: ++focus[slice_axis]; break;
        case VT::Down:
        case VT::MouseWheelDown: --focus[slice_axis]; break;
        case VT::Left: if (image.ndim() > 3) --image.index(3); if (image.index(3) < 0) image.index(3) = image.size(3)-1; break;
        case VT::Right: if (image.ndim() > 3) ++image.index(3); if (image.index(3) >= image.size(3)) image.index(3) = 0; break;
        case 'f': crosshair = !crosshair; std::cout << VT::ClearScreen; break;
        case 'a': slice_axis = 2; std::cout << VT::ClearScreen; break;
        case 's': slice_axis = 0; std::cout << VT::ClearScreen; break;
        case 'c': slice_axis = 1; std::cout << VT::ClearScreen; break;
        case 'r': focus[x_axis] = std::round (image.size(x_axis)/2); focus[x_axis] = std::round (image.size(x_axis)/2);
                  focus[slice_axis] = std::round (image.size(slice_axis)/2); std::cout << VT::ClearScreen; break;
        case '+': image_scale *= 1.1; std::cout << VT::ClearScreen; break;
        case '-': image_scale /= 1.1; std::cout << VT::ClearScreen; break;
        case VT::MouseMoveLeft: focus[x_axis] += xp-x; focus[y_axis] += yp-y; break;
        case VT::Escape: colourmap.update_scaling (x, y); break;
        case VT::MouseMoveRight: colourmap.update_scaling (x-xp, y-yp); break;
        case VT::Home: colourmap.invalidate_scaling(); break;

        default: need_update = false; break;
      }
      xp = x;
      yp = y;

    } while (!(event == 'q' || event == 'Q' || event == VT::Ctrl('c') || event == VT::Ctrl('q')));
    VT::exit_raw_mode();

  }
  catch (Exception&) {
    VT::exit_raw_mode();
    throw;
  }

}
