#include <termios.h>

#include "command.h"
#include "file/config.h"
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

enum ArrowMode { ARROW_SLICEVOL, ARROW_COLOUR, ARROW_CROSSHAIR, N_ARROW_MODES };


// commmand-line description and syntax:
// (used to produce the help page and verify validity of arguments at runtime)
void usage ()
{
  // lifted from cmd/mrcolour.cpp:
  const ColourMap::Entry* entry = ColourMap::maps;
  do {
    colourmap_choices_std.push_back (lowercase (entry->name));
    ++entry;
  } while (entry->name && !entry->special && !entry->is_colour);
  colourmap_choices_cstr.reserve (colourmap_choices_std.size() + 1);
  for (const auto& s : colourmap_choices_std)
    colourmap_choices_cstr.push_back (s.c_str());
  colourmap_choices_cstr.push_back (nullptr);



  AUTHOR = "Jianxiao Wu (vesaveronica@gmail.com) & "
    "Max Pietsch (maximilian.pietsch@kcl.ac.uk) & "
    "Daan Christiaens (daan.christiaens@kcl.ac.uk) & "
    "J-Donald Tournier (jdtournier@gmail.com)";

  SYNOPSIS = "preview images on the terminal (requires terminal with sixel support)";

  DESCRIPTION
    + "This requires a terminal capable of displaying sixel graphics (e.g. iTerm2 on macOS, "
    "minTTY on Windows, mlTerm on Linux). Displays the image specified within the terminal, "
    "and allows interacting with the image. Press the ? key while running for runtime usage "
    "instructions.";

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

  + Option ("orthoview",
            "display three orthogonal or a single plane. Default is true.")
  +   Argument ("yesno").type_bool()

  + Option ("intensity_range",
            "specify intensity range of the data. The image intensity will be scaled "
            "between the specified minimum and maximum intensity values. "
            "By default, percentile scaling is used. ")
  +   Argument ("min").type_float()
  +   Argument ("max").type_float()

  + Option ("percentile_range",
            "specify intensity range of the data. The image intensity will be scaled "
            "between the specified minimum and maximum percentile values. "
            "Defaults are: " + str(DEFAULT_PMIN, 3) + " - " + str(DEFAULT_PMAX, 3))
  +   Argument ("min").type_float()
  +   Argument ("max").type_float()

  + Option ("colourmap",
            "the colourmap to apply; choices are: " + join(colourmap_choices_std, ",") +
            ". Default is " + colourmap_choices_std[0] + ".")
  +   Argument ("name").type_choice (colourmap_choices_cstr.data())

  + Option ("crosshairs",
            "draw crosshairs at specified position. Set to negative position to hide.")
  +   Argument ("x").type_integer()
  +   Argument ("y").type_integer()

  + Option ("levels",
            "number of intensity levels in the colourmap. Default is 64.")
  +   Argument ("number").type_integer (2)

  + Option ("scale_image",
            "scale the image size by the supplied factor")
    + Argument ("factor").type_float()

  + Option ("interactive",
            "interactive mode. Default is true.")
  +   Argument ("yesno").type_bool();
}


using value_type = float;

// calculate percentile of a list of numbers
// implementation based on `mrthreshold` - can be merged with Math::median in due course
template <class Container>
value_type percentile (Container& data, default_type percentile)
{
  // ignore nan
  auto isnotfinite = [](typename Container::value_type val) { return !std::isfinite(val); };
  data.erase (std::remove_if (data.begin(), data.end(), isnotfinite), data.end());
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
int levels = 64;
int x_axis, y_axis, slice_axis = 2;
value_type pmin = DEFAULT_PMIN, pmax = DEFAULT_PMAX, scale_image = 1.0;
bool crosshair = true, colorbar = true, orthoview = true, interactive = true;
vector<int> focus (3, 0);  // relative to original image grid
ArrowMode x_arrow_mode = ARROW_SLICEVOL, arrow_mode = x_arrow_mode;


// Supporting functions for display
//
inline void set_axes ()
{
  switch (slice_axis) {
    case 0: x_axis = 1; y_axis = 2; break;
    case 1: x_axis = 0; y_axis = 2; break;
    case 2: x_axis = 0; y_axis = 1; break;
    default: throw Exception ("invalid axis specifier");
  }
}

template <class ImageType>
Header regrid_header (ImageType& image, float scale)
{
  Header header_target (image);
  float new_voxel_size = 1.0;
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

  return header_target;
}

template <class ImageType>
using Reslicer = Adapter::Reslice<Interp::Nearest, ImageType>;



// Show the main image,
// run repeatedly to update display.
void display (Image<value_type>& image, Sixel::ColourMap& colourmap)
{
  set_axes();
  float scale = std::min (std::min (image.spacing(0), image.spacing(1)), image.spacing(2)) / scale_image;

  auto image_regrid = Adapter::make<Reslicer> (image, regrid_header(image, scale));
  int x_dim = image_regrid.size(x_axis);
  int y_dim = image_regrid.size(y_axis);

  for (int n = 0; n < 3; ++n) {
    if (focus[n] < 0) focus[n] = 0;
    if (focus[n] >= image.size(n)) focus[n] = image.size(n)-1;
  }

  image_regrid.index(slice_axis) = focus[slice_axis];

  if (!colourmap.scaling_set()) {
    // reset scaling:
    std::vector<value_type> currentslice (x_dim*y_dim);
    size_t k = 0;
    for (auto l = Loop ({ size_t(x_axis), size_t(y_axis) })(image_regrid); l; ++l)
      currentslice[k++] = image_regrid.value();

    value_type vmin = percentile(currentslice, pmin);
    value_type vmax = percentile(currentslice, pmax);
    colourmap.set_scaling_min_max (vmin, vmax);
    INFO("reset intensity range to " + str(vmin) + " - " +str(vmax));
  }

  if (orthoview) {
    int backup_slice_axis = slice_axis;

    // calculate panel dimensions
    slice_axis = 2; set_axes();
    auto tmp_regrid1 = regrid_header(image, scale);
    int panel_x_dim = std::max(tmp_regrid1.size(0), tmp_regrid1.size(1));
    slice_axis = 0; set_axes();
    auto tmp_regrid2 = regrid_header(image, scale);
    int panel_y_dim = std::max(tmp_regrid2.size(0), tmp_regrid2.size(1));

    Sixel::Encoder<3> encoder (panel_x_dim, panel_y_dim, colourmap);

    for (slice_axis = 0; slice_axis < 3; ++slice_axis)
    {
      set_axes();
      auto image_regrid1 = Adapter::make<Reslicer> (image, regrid_header(image, scale));
      x_dim = image_regrid1.size(x_axis);
      y_dim = image_regrid1.size(y_axis);
      // recentring
      int dx = (panel_x_dim - x_dim) / 2;
      int dy = (panel_y_dim - y_dim) / 2;

      encoder.set_panel(slice_axis);

      image_regrid1.index(slice_axis) = focus[slice_axis];
      for (int y = 0; y < y_dim; ++y) {
        image_regrid1.index(y_axis) = y_dim-1-y;
        for (int x = 0; x < x_dim; ++x) {
          image_regrid1.index(x_axis) = x_dim-1-x;
          encoder(x+dx, y+dy, image_regrid1.value());
        }
      }

      if (crosshair) {
        int x = std::round(x_dim - image.spacing(x_axis) * (focus[x_axis] + 0.5) / scale);
        int y = std::round(y_dim - image.spacing(y_axis) * (focus[y_axis] + 0.5) / scale);
        x = std::max (std::min (x, x_dim-1), 0);
        y = std::max (std::min (y, y_dim-1), 0);
        encoder.draw_crosshairs (x+dx, y+dy);
      }

      if (slice_axis == 2) encoder.draw_colourbar ();

      encoder.draw_boundingbox (interactive && slice_axis == backup_slice_axis);
    }
    slice_axis = backup_slice_axis;

    // encode buffer and print out:
    encoder.write();
  }
  else {
    Sixel::Encoder<> encoder (x_dim, y_dim, colourmap);

    for (int y = 0; y < y_dim; ++y) {
      image_regrid.index(y_axis) = y_dim-1-y;
      for (int x = 0; x < x_dim; ++x) {
        image_regrid.index(x_axis) = x_dim-1-x;
        encoder(x, y, image_regrid.value());
      }
    }

    if (crosshair) {
      int x = std::round(x_dim - image.spacing(x_axis) * (focus[x_axis] + 0.5) / scale);
      int y = std::round(y_dim - image.spacing(y_axis) * (focus[y_axis] + 0.5) / scale);
      x = std::max (std::min (x, x_dim-1), 0);
      y = std::max (std::min (y, y_dim-1), 0);
      encoder.draw_crosshairs (x,y);
    }

    encoder.draw_colourbar ();

    encoder.draw_boundingbox (interactive);

    // encode buffer and print out:
    encoder.write();
  }

  // print focus and pixel value
  image.index(0) = focus[0];
  image.index(1) = focus[1];
  image.index(2) = focus[2];
  std::cout << std::endl << VT::CarriageReturn << VT::ClearLine << "[ ";
  for (int d = 0; d < 3; d++) {
    if (d == x_axis || d == y_axis) {
        if (arrow_mode == ARROW_CROSSHAIR) std::cout << VT::TextForegroundYellow;
        std::cout << VT::TextUnderscore;
    }
    else if (arrow_mode == ARROW_SLICEVOL) std::cout << VT::TextForegroundYellow;
    std::cout << focus[d];
    std::cout << VT::TextReset;
    std::cout << " ";
  }
  for (size_t n = 3; n < image.ndim(); ++n) {
    if (n == 3 && arrow_mode == ARROW_SLICEVOL) std::cout << VT::TextForegroundYellow;
    std::cout << image.index(n);
    std::cout << VT::TextReset << " ";
  }
  std::cout << "]: ";
  std::cout << image.value();

  std::cout << "               [ ";
  if (arrow_mode == ARROW_COLOUR) std::cout << VT::TextForegroundYellow;
  std::cout << colourmap.min() << " " << colourmap.max() << VT::TextReset;
  std::cout << " ]";

  std::cout.flush();
}



void show_help ()
{
  std::cout << VT::ClearScreen;
  int row = 2;
  std::cout << VT::position_cursor_at (row++, 2) << "mrpeek key bindings:";
  row++;
  std::cout << VT::position_cursor_at (row++, 4) << "up/down               previous/next slice";
  std::cout << VT::position_cursor_at (row++, 4) << "left/right            previous/next volume";
  std::cout << VT::position_cursor_at (row++, 4) << "a / s / c             axial / sagittal / coronal projection";
  std::cout << VT::position_cursor_at (row++, 4) << "o                     toggle orthoview";
  std::cout << VT::position_cursor_at (row++, 4) << "- / +                 zoom out / in";
  std::cout << VT::position_cursor_at (row++, 4) << "x / <space>           toggle arrow key crosshairs control";
  std::cout << VT::position_cursor_at (row++, 4) << "b                     toggle arrow key brightness control";
  std::cout << VT::position_cursor_at (row++, 4) << "f                     show / hide crosshairs";
  std::cout << VT::position_cursor_at (row++, 4) << "r                     reset focus";
  std::cout << VT::position_cursor_at (row++, 4) << "left mouse & drag     move focus";
  std::cout << VT::position_cursor_at (row++, 4) << "right mouse & drag    adjust brightness / contrast";
  std::cout << VT::position_cursor_at (row++, 4) << "Esc                   reset brightness / contrast";
  std::cout << VT::position_cursor_at (row++, 4) << "1-9                   select colourmap";
  std::cout << VT::position_cursor_at (row++, 4) << "l                     select number of colourmap levels";
  row++;
  std::cout << VT::position_cursor_at (row++, 4) << "q / Q / Crtl-C        exit mrpeek";
  row++;
  std::cout << VT::position_cursor_at (row++, 4) << "press any key to exit help page";


  std::cout.flush();

  int event, x, y;
  while ((event = VT::read_user_input(x, y)) == 0)
    std::this_thread::sleep_for (std::chrono::milliseconds(10));

  std::cout << VT::ClearScreen;
  std::cout.flush();
}


bool query_int (const std::string& prompt,
    int& value,
    int vmin = std::numeric_limits<int>::min(),
    int vmax = std::numeric_limits<int>::max())
{
  std::cout << VT::CarriageReturn << VT::ClearLine << prompt;
  std::cout.flush();

  int event, x, y;
  std::string response;
  while ((event = VT::read_user_input(x, y)) != '\r') {
    std::this_thread::sleep_for (std::chrono::milliseconds(10));
    if (event >= '0' && event <= '9') {
      response += char(event);
      std::cout << char(event);
      std::cout.flush();
    }
    else if (event == VT::Backspace) {
      if (response.size()) {
        response.pop_back();
        std::cout << VT::move_cursor_left(1) << VT::ClearLineFromCursorRight;
        std::cout.flush();
      }
    }
  }

  if (response.size()) {
    value = to<int> (response);
    return (value >= vmin && value <= vmax);
  }
  return false;
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
    int x = opt[0][0];
    int y = opt[0][1];
    if (x<0 || y<0) {
      crosshair = false;
    } else {
      focus[x_axis] = opt[0][0];
      focus[y_axis] = opt[0][1];
    }
  }

  //CONF option: MRPeekOrthoView
  orthoview = get_option_value ("orthoview", MR::File::Config::get_bool ("MRPeekOrthoView", orthoview));

  //CONF option: MRPeekScaleImage
  scale_image = get_option_value ("scale_image", MR::File::Config::get_float ("MRPeekScaleImage", scale_image));
  if (scale_image <= 0)
    throw Exception ("scale_image value needs to be positive");
  INFO("scale_image: " + str(scale_image));

  //CONF option: MRPeekInteractive
  if (!interactive or !get_option_value ("interactive", MR::File::Config::get_bool ("MRPeekInteractive", true))) {
    interactive = false;
    display (image, colourmap);
    std::cout << "\n";
    return;
  }


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

      if (x-xp > 127) xp += 256;
      if (xp-x > 127) xp -= 256;
      if (y-yp > 127) yp += 256;
      if (yp-y > 127) yp -= 256;

      need_update = true;

      switch (event) {
        case VT::Up:
          switch(arrow_mode) {
            case ARROW_SLICEVOL:  ++focus[slice_axis];   break;
            case ARROW_CROSSHAIR: ++focus[y_axis]; break;
            case ARROW_COLOUR:    colourmap.update_scaling (0, -1); break;
            default: break;
          } break;
        case VT::MouseWheelUp: ++focus[slice_axis]; break;
        case VT::Down:
          switch(arrow_mode) {
            case ARROW_SLICEVOL:  --focus[slice_axis];   break;
            case ARROW_CROSSHAIR: --focus[y_axis]; break;
            case ARROW_COLOUR:    colourmap.update_scaling (0, 1); break;
            default: break;
          } break;
        case VT::MouseWheelDown: --focus[slice_axis]; break;
        case VT::Left:
          switch(arrow_mode) {
            case ARROW_SLICEVOL:  if (image.ndim() > 3) {--image.index(3);
                                                         if (image.index(3) < 0) image.index(3) = image.size(3)-1;}
                                  break;
            case ARROW_CROSSHAIR: ++focus[x_axis]; break;
            case ARROW_COLOUR:    colourmap.update_scaling (-1, 0); break;
            default: break;
          } break;
        case VT::Right:
          switch(arrow_mode) {
            case ARROW_SLICEVOL:  if (image.ndim() > 3) {++image.index(3);
                                                         if (image.index(3) >= image.size(3)) image.index(3) = 0;}
                                  break;
            case ARROW_CROSSHAIR: --focus[x_axis]; break;
            case ARROW_COLOUR:    colourmap.update_scaling (1, 0); break;
            default: break;
          } break;
        case 'f': crosshair = !crosshair; std::cout << VT::ClearScreen; break;
        case 'a': slice_axis = 2; std::cout << VT::ClearScreen; break;
        case 's': slice_axis = 0; std::cout << VT::ClearScreen; break;
        case 'c': slice_axis = 1; std::cout << VT::ClearScreen; break;
        case 'o': orthoview = !orthoview; std::cout << VT::ClearScreen; break;
        case 'r': focus[x_axis] = std::round (image.size(x_axis)/2); focus[x_axis] = std::round (image.size(x_axis)/2);
                  focus[slice_axis] = std::round (image.size(slice_axis)/2); std::cout << VT::ClearScreen; break;
        case '+': scale_image *= 1.1; std::cout << VT::ClearScreen; break;
        case '-': scale_image /= 1.1; std::cout << VT::ClearScreen; break;
        case ' ':
        case 'x': arrow_mode = x_arrow_mode = (x_arrow_mode == ARROW_SLICEVOL) ? ARROW_CROSSHAIR : ARROW_SLICEVOL; break;
        case 'b': arrow_mode = (arrow_mode == ARROW_COLOUR) ? x_arrow_mode : ARROW_COLOUR; std::cout << VT::ClearScreen; break;
        case VT::MouseMoveLeft: focus[x_axis] += xp-x; focus[y_axis] += yp-y; break;
        case VT::Escape: colourmap.invalidate_scaling(); break;
        case VT::MouseMoveRight: colourmap.update_scaling (x-xp, y-yp); break;
        case 'l': {
                    int n;
                    if (query_int ("select number of levels: ", n, 1, 254)) {
                      levels = n;
                      float offset = colourmap.offset();
                      float scale = colourmap.scale();
                      colourmap = Sixel::ColourMap (ColourMap::maps[colourmap_ID], levels);
                      colourmap.set_scaling (offset, scale);
                    }
                  } break;
        case '?': show_help(); break;

        default:
                  if (event >= '1' && event <= '9') {
                    int idx = event - '1';
                    if (idx < colourmap_choices_std.size()) {
                      float offset = colourmap.offset();
                      float scale = colourmap.scale();
                      colourmap_ID = idx;
                      colourmap = Sixel::ColourMap (ColourMap::maps[colourmap_ID], levels);
                      colourmap.set_scaling (offset, scale);
                      break;
                    }
                  }
                  need_update = false; break;
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
