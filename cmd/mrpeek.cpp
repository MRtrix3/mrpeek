#include "command.h"
#include "image.h"
#include "algo/loop.h"
#include "sixel.h"
#include <ostream>
#include "dither.h" // symbolic link in mrtrix3/src
#include "output.h" // symbolic link in mrtrix3/src

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
  + Option ("view", "projection view (0: coronal / 1: sagittal / 2: axial)")
  + Option ("slice", "slice level for the chosen view. For example, the slice level of the axial view "
                     "is detemined by the Z coordinate");
    
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
  auto view = get_option_value("view", 0);
  auto level = get_option_value("slice", 50);

  // set-up
  int width = 91;
  int height = 109;
  unsigned char val[width*height*4];
  int gscale;
  vector<size_t> order;
  sixel_dither *dither;
  sixel_output *output;

  // view
  switch(view){
    case 0:
      order = {1, 2};
      break;
    case 1:
      order = {0, 2};
      break;
    case 2:
      order = {0, 1};
  }

  // loop
  LoopInOrder loop (in, order);
  in.index(view) = level;
  for (auto l = loop.run (in); l; ++l){
    gscale = in.value() * 255 / 384;
    val[(in.get_index(0)+(height-1-in.get_index(1))*width)*4+0] = (unsigned char) in.value(); //red
    val[(in.get_index(0)+(height-1-in.get_index(1))*width)*4+1] = (unsigned char) in.value(); //green
    val[(in.get_index(0)+(height-1-in.get_index(1))*width)*4+2] = (unsigned char) in.value(); //blue
    val[(in.get_index(0)+(height-1-in.get_index(1))*width)*4+3] = (unsigned char) in.value()>0; //alpha
  }

  // dither object
  sixel_dither_new(&dither, 256, NULL);
  sixel_output_new(&output, &write_function, NULL, NULL);
  sixel_dither_initialize(dither, val, width, height, SIXEL_PIXELFORMAT_RGBA8888, SIXEL_LARGE_AUTO, SIXEL_REP_AUTO, SIXEL_QUALITY_AUTO);
  sixel_encode(val, width, height, 1, dither, output);

  // save sixel file for checks
}
