#include "command.h"
#include "image.h"
#include "algo/loop.h"
#include "sixel.h"
#include <ostream>

using namespace MR;
using namespace App;

// commmand-line description and syntax:
// (used to produce the help page and verify validity of arguments at runtime)
void usage ()
{
  AUTHOR = "Joe Bloggs (joe.bloggs@acme.org)";

  SYNOPSIS = "raise each voxel intensity to the given power (default: 2)";

  ARGUMENTS
  + Argument ("in", "the input image.").type_image_in();

  OPTIONS
  + Option ("axis", 
            "specify projection of slice, as an integer representing the slice normal: "
            "0: L/R (sagittal); 1: A/P (coronal); 2 I/S (axial). Default is 2 (axial). ")
    + Argument ("value").type_integer()

  + Option ("slice", 
            "select slice to display. Default is the middle slice.")
    + Argument ("value").type_integer()

  + Option ("scaling",
            "specity intensity scaling of data. The image intensity will be scaled as "
            "output = offset + scale*input. Default is [0 1].")
    + Argument ("offset").type_float()
    + Argument ("scale").type_float();
    
}

int write_function (char *data, int size, void *priv)
{
  std::cout.write(data, size);
  return size;
}


// It is a good idea to use typedef's to help with flexibility if types need to
// be changed later on.
using value_type = float;


// This is where execution proper starts - the equivalent of main().
// The difference is that this is invoked after all command-line parsing has
// been done.
void run ()
{

  // Image to access the input data:
  auto in = Image<value_type>::open (argument[0]);

  // options
  auto axis = get_option_value("axis", 0);
  auto slice = get_option_value("slice", in.size(axis)/2);
  float offset = 0.0f;
  float scale = 1.0f;
  auto opt = get_options ("scaling");
  if (opt.size()){
    offset = opt[0][0];
    scale = opt[0][1];
  }

  // declarations
  int gscale, width, height;
  vector<size_t> order;
  sixel_dither *dither;
  sixel_output *output;

  // set up image view
  in.index(axis) = slice;
  switch(axis){
    case 0: order = {1, 2}; break;
    case 1: order = {0, 2}; break;
    case 2: order = {0, 1}; break;
    default: throw Exception {"Invalid axis option."};
  }

  // set up image parameters
  width = in.size(order[0]);
  height = in.size(order[1]);
  
  std::cout.write("\033[2J", 4);

  for (int new_slice = slice; new_slice<(slice+5); new_slice++){
    in.index(axis) = new_slice;
    //std::cout.write("\033[H", 3);
    //std::cout.write("\033[2J", 4);

    // loop
    unsigned char val[width*height*4];
    auto loop = Loop (order);
    for (auto l = loop (in); l; ++l){
      gscale = in.value() * scale + offset;
      if (gscale > 255){
        gscale = 255;
      }
      val[(in.get_index(order[0])+(height-1-in.get_index(order[1]))*width)*4+0] = (unsigned char) gscale ; //red
      val[(in.get_index(order[0])+(height-1-in.get_index(order[1]))*width)*4+1] = (unsigned char) gscale ; //green
      val[(in.get_index(order[0])+(height-1-in.get_index(order[1]))*width)*4+2] = (unsigned char) gscale ; //blue
      val[(in.get_index(order[0])+(height-1-in.get_index(order[1]))*width)*4+3] = (unsigned char) gscale >0; //alpha
    }

    // dither object
    sixel_dither_new(&dither, 256, NULL);
    sixel_output_new(&output, &write_function, NULL, NULL);
    sixel_dither_initialize(dither, val, width, height, SIXEL_PIXELFORMAT_RGBA8888, SIXEL_LARGE_AUTO, SIXEL_REP_AUTO, SIXEL_QUALITY_AUTO);
    sixel_encode(val, width, height, 1, dither, output);


  }

  //write_function ("\033[?47l", 6, NULL);
}
