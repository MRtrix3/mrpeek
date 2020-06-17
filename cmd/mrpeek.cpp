#include "command.h"
#include "image.h"
#include "algo/loop.h"
#include "interp/linear.h"
#include "filter/resize.h"
// #include "adapter/regrid.h"

#include "sixel.h"

const char* interp_choices[] = { "nearest", "linear", "cubic", "sinc", NULL };
using namespace MR;
using namespace App;

// commmand-line description and syntax:
// (used to produce the help page and verify validity of arguments at runtime)
void usage ()
{
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

  + Option ("scaling",
            "specify intensity scaling of the data. The image intensity will be scaled "
            "as output = offset + scale*input, with the visible range set to [0 1]. "
            "Default is [0 1].")
  +   Argument ("offset").type_float()
  +   Argument ("scale").type_float()

  + Option ("intensity_range",
            "specify intensity range of the data. The image intensity will be scaled "
            "between the specified minimum and maximum intensity values. ")
  +   Argument ("min").type_float()
  +   Argument ("max").type_float()

  + Option ("crosshairs",
            "draw crosshairs at specified position")
  +   Argument ("x").type_integer(0)
  +   Argument ("y").type_integer(0)

  + OptionGroup ("Regridding options")
    + Option   ("image_scale", "scale the image size by the supplied factor")
    + Argument ("factor").type_float()

    + Option ("interp", "set the interpolation method to use when reslicing (choices: nearest, linear, cubic, sinc. Default: cubic).")
    + Argument ("method").type_choice (interp_choices)

    + Option ("oversample",
        "set the amount of over-sampling (in the target space) to perform when regridding. This is particularly "
        "relevant when downsamping a high-resolution image to a low-resolution image, to avoid aliasing artefacts. "
        "This can consist of a single integer, or a comma-separated list of 3 integers if different oversampling "
        "factors are desired along the different axes. Default is determined from ratio of voxel dimensions (disabled "
        "for nearest-neighbour interpolation).")
    + Argument ("factor").type_sequence_int();
}



using value_type = float;





void run ()
{
  auto header_in = Header::open (argument[0]);
  Image<value_type> image_in = header_in.get_image<value_type>();

  int axis = get_option_value ("axis", 2);
  int slice = get_option_value ("slice", image_in.size(axis)/2);
  if (slice >= image_in.size(axis))
    throw Exception("slice " + str(slice) + " exceeds image size (" + str(image_in.size(axis)) + ") in axis " + str(axis));

  float offset = 0.0f;
  float scale = 1.0f;
  auto opt = get_options ("scaling");
  if (opt.size()) {
    offset = opt[0][0];
    scale = opt[0][1];
  }
  opt = get_options ("intensity_range");
  if (opt.size()) {
    float min = opt[0][0], max = opt[0][1];
    // 0 = off + scale*min
    // 1 = off + scale*max
    scale = 1.f / (max - min);
    offset = -scale*min;
  }

  int interp = 2;  // cubic
  opt = get_options ("interp");
  if (opt.size()) {
    interp = opt[0][0];
  }

  vector<int> oversample = Adapter::AutoOverSample;
  opt = get_options ("oversample");
  if (opt.size()) {
    oversample = opt[0][0];
  }

  int x_axis, y_axis;
  bool x_forward, y_forward;
  switch (axis) {
    case 0: x_axis = 1; y_axis = 2; x_forward = false; y_forward = false; break;
    case 1: x_axis = 0; y_axis = 2; x_forward = false; y_forward = false; break;
    case 2: x_axis = 0; y_axis = 1; x_forward = false; y_forward = false; break;
    default: throw Exception ("invalid axis specifier");
  }


  Sixel::ColourMap colourmap (100);
  colourmap.set_scaling (offset, scale);

  Header header_target (header_in);
  opt = get_options ("image_scale");
  if (opt.size()) {
    default_type image_scale (opt[0][0]);
    vector<default_type> new_voxel_size (1.0, 3);
    Eigen::Vector3 original_extent;
    for (int d = 0; d < 3; ++d) {
      if (d != axis)
        new_voxel_size[d] = (header_in.size(d) * header_in.spacing(d)) / std::ceil (header_in.size(d) * image_scale);
      else
        new_voxel_size[d] = header_in.spacing(d);
      original_extent[d] = header_in.size(d) * header_in.spacing(d);

      header_target.size(d) = std::round (header_in.size(d) * header_in.spacing(d) / new_voxel_size[d] - 0.0001); // round down at .5
      for (size_t i = 0; i < 3; ++i)
        header_target.transform()(i,3) += 0.5 * ((new_voxel_size[d] - header_target.spacing(d)) + (original_extent[d] - (header_target.size(d) * new_voxel_size[d]))) * header_target.transform()(i,d);
      header_target.spacing(d) = new_voxel_size[d];
    }
  }

  Adapter::Reslice<Interp::Linear, Image<value_type>> image_regrid(image_in, header_target);  // NoTransform, AutoOverSample, out_of_bounds_value

  const int x_dim = image_regrid.size(x_axis);
  const int y_dim = image_regrid.size(y_axis);

  image_regrid.index(axis) = slice;


  Sixel::Encoder encoder (x_dim, y_dim, colourmap);

  for (int y = 0; y < y_dim; ++y) {
    image_regrid.index(y_axis) = y_forward ? y : y_dim-1-y;
    for (int x = 0; x < x_dim; ++x) {
      image_regrid.index(x_axis) = x_forward ? x : x_dim-1-x;
      encoder(x, y, image_regrid.value());
    }
  }

  opt = get_options ("crosshairs");
  if (opt.size())
    encoder.draw_crosshairs (opt[0][0], opt[0][1]);

  // encode buffer and print out:
  encoder.write();

}
