#include "command.h"
#include "image.h"
#include "algo/loop.h"
#include "sixel.h"

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
  +   Argument ("y").type_integer(0);

}



using value_type = float;





void run ()
{
  auto image_in = Image<value_type>::open (argument[0]);

  int axis = get_option_value ("axis", 2);
  int slice = get_option_value ("slice", image_in.size(axis)/2);
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

  const int x_dim = image_in.size(x_axis);
  const int y_dim = image_in.size(y_axis);

  image_in.index(axis) = slice;

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

}
