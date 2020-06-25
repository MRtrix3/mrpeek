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

  + Option ("plot",
            "specify plot dimension: "
            "0: L/R (sagittal); 1: A/P (coronal); 2 I/S (axial); 3 volumes... ")
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
            "number of intensity levels in the colourmap. Default is 32.")
  +   Argument ("number").type_integer (2)

  + Option ("zoom",
            "scale the image size by the supplied factor")
    + Argument ("factor").type_float()

  + Option ("interactive",
            "interactive mode. Default is true.")
  +   Argument ("yesno").type_bool();
}


using value_type = float;
using ImageType = Image<value_type>;
using Reslicer = Adapter::Reslice<Interp::Nearest, ImageType>;


#define CROSSHAIR_COLOUR 1
#define STANDARD_COLOUR 2
#define HIGHLIGHT_COLOUR 3
#define STATIC_CMAP { {0,0,0}, { 100,100,0 }, {50,50,50}, {100,100,100} }



// Global variables to hold slide dislay parameters:
// These will need to be moved into a struct/class eventually...
int levels = 32;
int x_axis, y_axis, slice_axis = 2, plot_axis = slice_axis, vol_axis = -1;
value_type pmin = DEFAULT_PMIN, pmax = DEFAULT_PMAX, zoom = 1.0;
bool crosshair = true, colorbar = true, orthoview = true, interactive = true;
bool do_plot = false, show_image = true;
vector<int> focus (3, 0);  // relative to original image grid
ArrowMode x_arrow_mode = ARROW_SLICEVOL, arrow_mode = x_arrow_mode;
Sixel::ColourMaps colourmaps;
Sixel::ColourMaps plot_cmaps;




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




inline void show_focus (ImageType& image)
{
  image.index(0) = focus[0];
  image.index(1) = focus[1];
  image.index(2) = focus[2];
  std::cout << VT::ClearLine;

  std::cout << "index: [ ";
  for (int d = 0; d < 3; d++) {
    if (d == x_axis) {if (arrow_mode == ARROW_CROSSHAIR) std::cout << "\u2194" << VT::TextForegroundYellow; std::cout << VT::TextUnderscore; }
    else if (d == y_axis) {if (arrow_mode == ARROW_CROSSHAIR) std::cout << "\u2195" << VT::TextForegroundYellow; std::cout << VT::TextUnderscore; }
    else {if (arrow_mode == ARROW_SLICEVOL) std::cout << "\u2195" << VT::TextForegroundYellow; }
    std::cout << focus[d];
    std::cout << VT::TextReset;
    std::cout << " ";
  }
  for (size_t n = 3; n < image.ndim(); ++n) {
    if (n == vol_axis && arrow_mode == ARROW_SLICEVOL) std::cout << "\u2194" << VT::TextForegroundYellow;
    std::cout << image.index(n);
    std::cout << VT::TextReset << " ";
  }
  std::cout << "] ";

  std::cout << "| value: " << image.value();
}




inline Reslicer get_regridder (ImageType& image, int with_slice_axis)
{
  Header header_target (image);
  default_type original_extent;
  for (int d = 0; d < 3; ++d) {
    const float new_voxel_size = (d == with_slice_axis) ? image.spacing(d) : 1.0f/zoom;

    original_extent = image.size(d) * image.spacing(d);

    header_target.size(d) = std::round (image.size(d) * image.spacing(d) / new_voxel_size - 0.0001); // round down at .5
    for (size_t i = 0; i < 3; ++i)
      header_target.transform()(i,3) += 0.5 * (
          (new_voxel_size - header_target.spacing(d)) +
          (original_extent - (header_target.size(d) * new_voxel_size))
          ) * header_target.transform()(i,d);
    header_target.spacing(d) = new_voxel_size;
  }

  return { image, header_target };
}





void autoscale (ImageType& image, Sixel::CMap& cmap)
{
  auto image_regrid = get_regridder (image, slice_axis);
  const int x_dim = image_regrid.size(x_axis);
  const int y_dim = image_regrid.size(y_axis);
  image_regrid.index(slice_axis) = focus[slice_axis];

  std::vector<value_type> currentslice (x_dim*y_dim);
  size_t k = 0;
  for (auto l = Loop ({ size_t(x_axis), size_t(y_axis) })(image_regrid); l; ++l)
    currentslice[k++] = image_regrid.value();

  value_type vmin = percentile(currentslice, pmin);
  value_type vmax = percentile(currentslice, pmax);
  cmap.set_scaling_min_max (vmin, vmax);
  INFO("reset intensity range to " + str(vmin) + " - " +str(vmax));
}




void display_slice (Reslicer& regrid, const Sixel::ViewPort& view, const Sixel::CMap& cmap)
{
  const int x_dim = regrid.size(x_axis);
  const int y_dim = regrid.size(y_axis);

  regrid.index(slice_axis) = focus[slice_axis];
  for (int y = 0; y < y_dim; ++y) {
    regrid.index(y_axis) = y_dim-1-y;
    for (int x = 0; x < x_dim; ++x) {
      regrid.index(x_axis) = x_dim-1-x;
      view(x,y) = cmap (regrid.value());
    }
  }
}







void plot (ImageType& image, int plot_axis)
{
  set_axes();

  const int radius = std::max<int>(1, std::round(zoom));
  const int pad = std::max(radius, std::max<int>(2, std::round(2*zoom)));
  const int x_dim = std::max(100.0, 2.0f * std::max(std::max (image.size(0)*image.spacing(0), image.size(1)*image.spacing(1)), image.size(2)*image.spacing(2)) * zoom) + 2 * pad;
  const int y_dim = std::round((float) x_dim / 1.618033) + 2 * pad;

  for (int n = 0; n < 3; ++n) {
    if (focus[n] < 0) focus[n] = 0;
    if (focus[n] >= image.size(n)) focus[n] = image.size(n)-1;
  }

  for (int n = 0; n < 3; ++n)
    image.index(n) = focus[n];
  for (size_t n = 3; n < image.ndim(); ++n)
    image.index(n) = image.index(n);
  image.index(plot_axis) = 0;

  std::vector<value_type> plotslice (image.size(plot_axis));
  std::vector<value_type> plotslice_finite (image.size(plot_axis));
  size_t k = 0;
  for (auto l = Loop ({ size_t(plot_axis) })(image); l; ++l) {
    plotslice[k] = image.value();
    plotslice_finite[k] = plotslice[k];
    ++k;
  }
  value_type vmin = percentile(plotslice_finite, 0); // non-finite values removed
  value_type vmax = percentile(plotslice_finite, 100);
  if (vmax == vmin) {
    vmin -= 1e-3;
    vmax += 1e-3;
  }

  if (!plot_cmaps.size())
    plot_cmaps.add ({ { 0,0,0 }, { 50,50,50 }, {100,100,100} });

  Sixel::Encoder encoder (x_dim, y_dim, plot_cmaps);
  auto canvas = encoder.viewport();

  int last_x, last_y, delta_x;
  bool connect_dots = false;
  const int x_offset = pad, y_offset = y_dim-1-pad;
  // coordinate axes
  for (int x = 0; x < x_dim; ++x)
    canvas(x, y_offset) = STANDARD_COLOUR;
  for (int y = 0; y < y_dim; ++y)
    canvas(x_offset, y) = STANDARD_COLOUR;
  for (int index = 0; index < plotslice.size(); ++index) {
    int x = std::round(float(index) / (plotslice.size() - 1) * (x_dim - 2 * pad));
    assert(x < x_dim);
    assert(x >= 0);
    int r0 = (index % 10) == 0 ? -pad : -std::max(1, pad/2);
    for (int y = r0; y < 0; ++y)
      canvas(x_offset + x, y_offset - y) = STANDARD_COLOUR;
  }

  for (int index = 0; index < plotslice.size(); ++index) {
    // ignore non-finite point, don't connect neighbouring data
    if (!std::isfinite(plotslice[index])) {
      connect_dots = false;
      continue;
    }
    int x = std::round(float(index) / (plotslice.size() - 1) * (x_dim - 2 * pad));
    int y = std::round(float(plotslice[index] - vmin) / (vmax - vmin) * (y_dim - 2 * pad));
    assert(x < x_dim);
    assert(x >= 0);
    assert(y < y_dim);
    assert(y >= 0);

    if ((plot_axis < 3 && index == focus[plot_axis]) || (plot_axis > 2 && index == image.index(plot_axis))) {
      // focus position: draw []
      for (int r1 = -radius; r1 <= radius; ++r1)
        for (int r2 = -radius; r2 <= radius; ++r2)
          canvas(x_offset + (x+r1), y_offset - (y+r2)) = STANDARD_COLOUR;
    }

    // plot line segment
    if (connect_dots) {
      assert(x > last_x);
      delta_x = x - last_x;
      for (int dx = 0; dx <= delta_x; ++dx)
        canvas(x_offset + (last_x + dx), y_offset - std::round((float(y * dx) + float(last_y * (delta_x - dx))) / delta_x)) = 1;
    }

    // data: draw +
    for (int r = -radius; r <= radius; ++r)
      canvas(x_offset + x, y_offset - (y+r)) = HIGHLIGHT_COLOUR;
    for (int r = -radius; r <= radius; ++r)
      canvas(x_offset + (x + r), y_offset - y) = HIGHLIGHT_COLOUR;

    connect_dots = true;
    last_x = x; last_y = y;
  }

  // encode buffer and print out:
  std::cout << VT::move_cursor (VT::Down, 2) << VT::CarriageReturn << vmax
    << VT::move_cursor (VT::Down, 1) << VT::CarriageReturn;
  encoder.write();
  std::cout << VT::ClearLine << vmin
    << VT::move_cursor (VT::Down, 1) << VT::CarriageReturn << VT::ClearLine
    << "plot axis: " << plot_axis << " | x range: [ 0 " << plotslice.size() - 1 << " ]";

  std::cout.flush();
}







// Show the main image,
// run repeatedly to update display.
void display (ImageType& image, Sixel::ColourMaps& colourmaps)
{
  auto& cmap = colourmaps[1];

  if (show_image) {
    set_axes();
    for (int n = 0; n < 3; ++n) {
      if (focus[n] < 0) focus[n] = 0;
      if (focus[n] >= image.size(n)) focus[n] = image.size(n)-1;
    }

    if (!cmap.scaling_set())
      autoscale (image, cmap);

    if (orthoview) {
      const int backup_slice_axis = slice_axis;

      Reslicer regrid[3] = {
        get_regridder (image, 0),
        get_regridder (image, 1),
        get_regridder (image, 2)
      };

      // calculate panel dimensions
      int panel_x_dim = std::max(regrid[2].size(0), regrid[0].size(0));
      int panel_y_dim = std::max(regrid[1].size(1), regrid[2].size(1));

      Sixel::Encoder encoder (3*panel_x_dim, panel_y_dim, colourmaps);

      for (slice_axis = 0; slice_axis < 3; ++slice_axis) {
        set_axes();
        const int x_dim = regrid[slice_axis].size (x_axis);
        const int y_dim = regrid[slice_axis].size (y_axis);
        // recentring
        const int dx = (panel_x_dim - x_dim) / 2;
        const int dy = (panel_y_dim - y_dim) / 2;
        auto view = encoder.viewport (slice_axis*panel_x_dim, 0, panel_x_dim, panel_y_dim);
        display_slice (regrid[slice_axis], view.viewport (dx, dy), cmap);

        if (crosshair) {
          int x = std::round(x_dim - image.spacing(x_axis) * (focus[x_axis] + 0.5) * zoom);
          int y = std::round(y_dim - image.spacing(y_axis) * (focus[y_axis] + 0.5) * zoom);
          x = std::max (std::min (x, x_dim-1), 0);
          y = std::max (std::min (y, y_dim-1), 0);
          view.draw_crosshairs(x+dx, y+dy, CROSSHAIR_COLOUR);
        }

        //if (slice_axis == 2) encoder.draw_colourbar ();

        //view.frame ((interactive && slice_axis == backup_slice_axis) ? HIGHLIGHT_COLOUR : STANDARD_COLOUR);
      }
      slice_axis = backup_slice_axis;
      set_axes();

      // encode buffer and print out:
      encoder.write();
    }
    else {
      auto regrid = get_regridder (image, slice_axis);
      const int x_dim = regrid.size (x_axis);
      const int y_dim = regrid.size (y_axis);

      Sixel::Encoder encoder (x_dim, y_dim, colourmaps);
      auto view = encoder.viewport();
      display_slice (regrid, view, cmap);

      if (crosshair) {
        int x = std::round(x_dim - image.spacing(x_axis) * (focus[x_axis] + 0.5) * zoom);
        int y = std::round(y_dim - image.spacing(y_axis) * (focus[y_axis] + 0.5) * zoom);
        x = std::max (std::min (x, x_dim-1), 0);
        y = std::max (std::min (y, y_dim-1), 0);
        view.draw_crosshairs (x, y, CROSSHAIR_COLOUR);
      }

      //view.draw_colourbar ();

      // encode buffer and print out:
      encoder.write();
    }
  }


  show_focus(image);
  std::cout << " [ ";
  if (arrow_mode == ARROW_COLOUR) std::cout << VT::TextForegroundYellow;
  std::cout << cmap.min() << " " << cmap.max() << VT::TextReset;
  std::cout << " ] ";

  if (orthoview) {
    std::cout << "| active: ";
    switch (slice_axis) {
      case (0): std::cout << VT::TextUnderscore << "s" << VT::TextReset << "agittal "; break;
      case (1): std::cout << VT::TextUnderscore << "c" << VT::TextReset << "oronal "; break;
      case (2): std::cout << VT::TextUnderscore << "a" << VT::TextReset << "xial "; break;
      default: break;
    };
  }

  if (interactive)
    std::cout << "| help: " << VT::TextUnderscore << "?" << VT::TextReset;

  if (do_plot)
    plot (image, plot_axis);

  std::cout.flush();
}














void show_help ()
{
  using namespace VT;
  auto key = [](const char* left, const char* right) {
    return move_cursor(Down,1) + position_cursor_at_col (3) + left
      + position_cursor_at_col (26) + right;
  };

  std::string out = ClearScreen;
  out += CursorHome
    + key ("mrpeek key bindings:", "")
    + move_cursor(Down,1)
    + key ("up/down", "previous/next slice")
    + key ("left/right", "previous/next volume")
    + key ("a / s / c", "axial / sagittal / coronal projection")
    + key ("o", "toggle orthoview")
    + key ("m", "toggle image display")
    + key ("v", "choose volume dimension")
    + key ("- / +", "zoom out / in")
    + key ("x / <space>", "toggle arrow key crosshairs control")
    + key ("b", "toggle arrow key brightness control")
    + key ("f", "show / hide crosshairs")
    + key ("r", "reset focus")
    + key ("left mouse & drag", "move focus")
    + key ("right mouse & drag", "adjust brightness / contrast")
    + key ("Esc", "reset brightness / contrast")
    + key ("1-9", "select colourmap")
    + key ("l", "select number of colourmap levels")
    + key ("p", "intensity plot along specified axis")
    + move_cursor(Down,1)
    + key ("q / Q / Crtl-C", "exit mrpeek")
    + move_cursor(Down,1)
    + key ("press any key to exit help page", "");

  std::cout << out;
  std::cout.flush();

  int event, x, y;
  while ((event = read_user_input(x, y)) == 0)
    std::this_thread::sleep_for (std::chrono::milliseconds(10));

  std::cout << ClearScreen;
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
        std::cout << VT::move_cursor (VT::Left, 1) << VT::ClearLineFromCursorRight;
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
  vol_axis = image.ndim() > 3 ? 3 : -1;
  focus[slice_axis] = get_option_value ("slice", image.size(slice_axis)/2);
  set_axes();
  focus[x_axis] = std::round (image.size(x_axis)/2);
  focus[y_axis] = std::round (image.size(y_axis)/2);

  if (focus[slice_axis] >= image.size(slice_axis))
    throw Exception("slice " + str(focus[slice_axis]) + " exceeds image size (" + str(image.size(slice_axis)) + ") in axis " + str(slice_axis));

  int colourmap_ID = get_option_value ("colourmap", 0);

  do_plot = get_options ("plot").size();
  plot_axis = get_option_value ("plot", plot_axis);
  if (plot_axis >= image.ndim())
    throw Exception("plot axis larger than image dimension, needs to be in [0..." + str(image.ndim()-1) + "].");

  //CONF option: MRPeekColourmapLevels
  //CONF default: 32
  //CONF set the default number of colourmap levels to use within mrpeek
  levels = get_option_value ("levels", File::Config::get_int ("MRPeekColourmapLevels", levels));

  colourmaps.add (STATIC_CMAP);
  colourmaps.add (colourmap_ID, levels);

  auto opt = get_options ("intensity_range");
  if (opt.size()) {
    colourmaps[1].set_scaling_min_max (opt[0][0], opt[0][1]);
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
  orthoview = get_option_value ("orthoview", File::Config::get_bool ("MRPeekOrthoView", orthoview));

  //CONF option: MRPeekScaleImage
  zoom = get_option_value ("zoom", MR::File::Config::get_float ("MRPeekZoom", zoom));
  if (zoom <= 0)
    throw Exception ("zoom value needs to be positive");
  INFO("zoom: " + str(zoom));
  zoom /= std::min (std::min (image.spacing(0), image.spacing(1)), image.spacing(2));

  //CONF option: MRPeekInteractive
  if (!interactive or !get_option_value ("interactive", File::Config::get_bool ("MRPeekInteractive", true))) {
    interactive = false;
    display (image, colourmaps);
    std::cout << "\n";
    return;
  }


  // start loop
  VT::enter_raw_mode();
  Sixel::init();

  try {

    int event = 0;
    int x, y, xp = 0, yp = 0;
    bool need_update = true;

    do {

      while ((event = VT::read_user_input(x, y)) == 0) {
        if (need_update) {
          std::cout << VT::ClearScreen << VT::CursorHome;
          display (image, colourmaps);
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
            case ARROW_COLOUR:    colourmaps[1].update_scaling (0, -1); break;
            default: break;
          } break;
        case VT::MouseWheelUp: ++focus[slice_axis]; break;
        case VT::Down:
          switch(arrow_mode) {
            case ARROW_SLICEVOL:  --focus[slice_axis];   break;
            case ARROW_CROSSHAIR: --focus[y_axis]; break;
            case ARROW_COLOUR:    colourmaps[1].update_scaling (0, 1); break;
            default: break;
          } break;
        case VT::MouseWheelDown: --focus[slice_axis]; break;
        case VT::Left:
          switch(arrow_mode) {
            case ARROW_SLICEVOL:  if (vol_axis >= 0) {
                                    --image.index(vol_axis);
                                    if (image.index(vol_axis) < 0) image.index(vol_axis) = image.size(vol_axis) - 1; }
                                  break;
            case ARROW_CROSSHAIR: ++focus[x_axis]; break;
            case ARROW_COLOUR:    colourmaps[1].update_scaling (-1, 0); break;
            default: break;
          } break;
        case VT::Right:
          switch(arrow_mode) {
            case ARROW_SLICEVOL:  if (vol_axis >= 0) {
                                    ++image.index(vol_axis);
                                    if (image.index(vol_axis) >= image.size(vol_axis)) image.index(vol_axis) = 0; }
                                  break;
            case ARROW_CROSSHAIR: --focus[x_axis]; break;
            case ARROW_COLOUR:    colourmaps[1].update_scaling (1, 0); break;
            default: break;
          } break;
        case 'f': crosshair = !crosshair; break;
        case 'v': if (image.ndim() > 3) {vol_axis = (vol_axis - 2) % (image.ndim() - 3) + 3; } break;
        case 'a': slice_axis = 2; break;
        case 's': slice_axis = 0; break;
        case 'c': slice_axis = 1; break;
        case 'o': orthoview = !orthoview; break;
        case 'm': show_image = !show_image; break;
        case 'r': focus[x_axis] = std::round (image.size(x_axis)/2); focus[x_axis] = std::round (image.size(x_axis)/2);
                  focus[slice_axis] = std::round (image.size(slice_axis)/2); break;
        case '+': zoom *= 1.1; break;
        case '-': zoom /= 1.1; break;
        case ' ':
        case 'x': arrow_mode = x_arrow_mode = (x_arrow_mode == ARROW_SLICEVOL) ? ARROW_CROSSHAIR : ARROW_SLICEVOL; break;
        case 'b': arrow_mode = (arrow_mode == ARROW_COLOUR) ? x_arrow_mode : ARROW_COLOUR; break;
        case VT::MouseMoveLeft: focus[x_axis] += xp-x; focus[y_axis] += yp-y; break;
        case VT::Escape: colourmaps[1].invalidate_scaling(); break;
        case VT::MouseMoveRight: colourmaps[1].update_scaling (x-xp, y-yp); break;
        case 'l': {
                    int n;
                    if (query_int ("select number of levels: ", n, 1, 254)) {
                      levels = n;
                      colourmaps[1].set_levels (levels);
                    }
                  } break;
        case 'p': do_plot = query_int ("select plot axis [0 ... "+str(image.ndim()-1)+"]: ",
                      plot_axis, 0, image.ndim()-1);
                  break;
        case '?': show_help(); break;

        default:
                  if (event >= '1' && event <= '9') {
                    int idx = event - '1';
                    if (idx < colourmap_choices_std.size()) {
                      colourmaps[1].ID = idx;
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
