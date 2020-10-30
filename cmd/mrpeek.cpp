#include "command.h"
#include "file/config.h"
#include "image.h"
#include "algo/loop.h"
#include "interp/nearest.h"
#include "interp/linear.h"
#include "interp/cubic.h"
#include "filter/reslice.h"

#include "sixel.h"

using namespace MR;
using namespace App;
using namespace VT;

#define DEFAULT_PMIN 0.2
#define DEFAULT_PMAX 99.8

#define CROSSHAIR_COLOUR 1
#define STANDARD_COLOUR 2
#define HIGHLIGHT_COLOUR 3
#define STATIC_CMAP { {0,0,0}, { 50,50,0 }, {50,50,50}, {100,100,100} }

#define COLOURBAR_WIDTH 10

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
#ifdef MRTRIX_WINDOWS
    + "NOTE: interactive mode is not currently supported on Windows."
#endif
    + "This requires a terminal capable of displaying sixel graphics (e.g. iTerm2 on macOS, "
    "minTTY on Windows, mlTerm on Linux). Displays the image specified within the terminal, "
    "and allows interacting with the image. Press the ? key while running for runtime usage "
    "instructions.";

  ARGUMENTS
  + Argument ("in", "the input image.").type_image_in ();

  OPTIONS
#ifndef MRTRIX_WINDOWS
  + Option ("batch",
            "disables interactive mode")
#endif
  + Option ("sagittal",
            "view sagittal projection only. Default: orthoview")

  + Option ("coronal",
            "view coronal projection only. Default: orthoview")

  + Option ("axial",
            "view axial projection only. Default: orthoview")

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

  + Option ("focus",
            "set focus (crosshairs) at specified position, as a comma-separated "
            "list of integer-valued voxel indices. Use empty entries to leave as default (e.g. '-focus ,,100' "
            "to place the focus on slice 100 along the z-axis, or '-focus ,,,4' to "
            "select volume 4).")
  +   Argument ("pos").type_sequence_int()

  + Option ("levels",
            "number of intensity levels in the colourmap. Default is 32.")
  +   Argument ("number").type_integer (2)

  + Option ("zoom",
            "scale the image size by the supplied factor")
    + Argument ("factor").type_float()

  + Option ("notext",
            "omit text output to show only the sixel image")

  + Option ("nocrosshairs",
            "do not render crosshairs at the focus")

  + Option ("noimage",
            "do not render the main image");
}


using value_type = float;
using ImageType = Image<value_type>;
using Reslicer = Adapter::Reslice<Interp::Nearest, ImageType>;
using LinearReslicer = Adapter::Reslice<Interp::Linear, ImageType>;
using CubicReslicer = Adapter::Reslice<Interp::Cubic, ImageType>;




// Global variables to hold slide dislay parameters:
// These will need to be moved into a struct/class eventually...
int levels = 32;
int x_axis, y_axis, slice_axis = 2, plot_axis = slice_axis, vol_axis = -1;
value_type pmin = DEFAULT_PMIN, pmax = DEFAULT_PMAX, zoom = 1.0;
bool crosshair = true, colorbar = true, orthoview = true, interactive = true;
bool do_plot = false, show_image = true, interpolate = false, show_text = true;
vector<int> focus (3, 0);  // relative to original image grid
ArrowMode x_arrow_mode = ARROW_SLICEVOL, arrow_mode = x_arrow_mode;
Sixel::ColourMaps colourmaps;
Sixel::ColourMaps plot_cmaps;


inline std::string move_down (int n) {
  if (interactive)
    return move_cursor (Down, n);
  std::string out;
  for (int i = 0; i < n; ++i)
    out += "\n";
  return out;
}


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




inline std::string show_focus (ImageType& image)
{
  image.index(0) = focus[0];
  image.index(1) = focus[1];
  image.index(2) = focus[2];
  std::string out = ClearLine;
  out += "index: [ ";

  for (int d = 0; d < 3; d++) {
    if (d == x_axis) {
      if (arrow_mode == ARROW_CROSSHAIR)
        out += std::string(LeftRightArrow) + TextForegroundYellow;
      out += TextUnderscore;
    }
    else if (d == y_axis) {
      if (arrow_mode == ARROW_CROSSHAIR)
        out += std::string(UpDownArrow) + TextForegroundYellow;
      out += TextUnderscore;
    }
    else {
      if (arrow_mode == ARROW_SLICEVOL)
        out += std::string(LeftRightArrow) + TextForegroundYellow;
    }
    out += str(focus[d]) + TextReset + " ";
  }
  for (int n = 3; n < int(image.ndim()); ++n) {
    if (n == vol_axis && arrow_mode == ARROW_SLICEVOL)
      out += std::string(UpDownArrow) + TextForegroundYellow;
    out += str(image.index(n)) + TextReset + " ";
  }
  out += "] ";

  out += "| value: " + str(image.value());

  return out;
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

  vector<value_type> currentslice (x_dim*y_dim);
  size_t k = 0;
  std::cerr.flush();
  for (auto l = Loop (vector<size_t>({ size_t(x_axis), size_t(y_axis) }))(image_regrid); l; ++l) {
    //std::cerr << "[" << image_regrid.index(0) << " " << image_regrid.index(1) << " " << image_regrid.index(2) << "] ";
    currentslice[k++] = image_regrid.value();
  }

  value_type vmin = percentile(currentslice, pmin);
  value_type vmax = percentile(currentslice, pmax);
  cmap.set_scaling_min_max (vmin, vmax);
  INFO("reset intensity range to " + str(vmin) + " - " +str(vmax));
}






// add crosshairs at the specified position,
// using colour index specified:
void draw_frame (const Sixel::ViewPort& view, int index)
{
  for (int x = 0; x < view.xdim(); ++x)
    view(x,0) = view(x,view.ydim()-1) = index;
  for (int y = 0; y < view.ydim(); ++y)
    view(0,y) = view(view.xdim()-1,y) = index;
}






// add crosshairs at the specified position,
// using colour index specified:
void draw_crosshairs (const Sixel::ViewPort& view, int x0, int y0, int index) {
  for (int x = 0; x < view.xdim(); ++x)
    view(x,y0) = index;
  for (int y = 0; y < view.ydim(); ++y)
    view(x0,y) = index;
}




template <class InterpType>
void render_slice (InterpType& regrid, const Sixel::ViewPort& view, const Sixel::CMap& cmap)
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



void display_slice (ImageType& image, Reslicer& regrid, const Sixel::ViewPort& view, const Sixel::CMap& cmap)
{
  if (interpolate) {
    LinearReslicer reslicer (image, regrid);
    render_slice (reslicer, view, cmap);
  }
  else
    render_slice (regrid, view, cmap);
}



void draw_colourbar (const Sixel::ViewPort& view, const Sixel::CMap& cmap)
{
  for (int y = 0; y < view.ydim(); ++y) {
    int colour = cmap.index + std::round (cmap.levels() * (1.0f-float (y)/view.ydim()));
    for (int x = 0; x < view.xdim(); ++x)
      view (x,y) = colour;
  }
}






std::string plot (ImageType& image, int plot_axis)
{
  set_axes();

  const int radius = std::max<int>(1, std::round(zoom));
  const int pad = std::max(radius, std::max<int>(2, std::round(2*zoom)));
  const int x_dim = std::max(100.0, 2.0f * std::max(std::max (image.size(0)*image.spacing(0), image.size(1)*image.spacing(1)), image.size(2)*image.spacing(2)) * zoom) + 2 * pad;
  const int y_dim = std::round((float) x_dim / 1.618033) + 2 * pad;

  for (int n = 0; n < 3; ++n)
    image.index(n) = focus[n];

  ssize_t current_index = image.index (plot_axis);
  image.index(plot_axis) = 0;

  std::vector<value_type> plotslice (image.size(plot_axis));
  std::vector<value_type> plotslice_finite (image.size(plot_axis));
  size_t k = 0;
  for (auto l = Loop (plot_axis)(image); l; ++l) {
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
    plot_cmaps.add (STATIC_CMAP);

  Sixel::Encoder encoder (x_dim, y_dim, plot_cmaps);
  auto canvas = encoder.viewport();

  int last_x, last_y, delta_x;
  bool connect_dots = false;
  const int x_offset = pad, y_offset = y_dim-1-pad;
  // coordinate axes
  for (int x = 0; x < x_dim; ++x)
    canvas(x, y_offset) = HIGHLIGHT_COLOUR;
  for (int y = 0; y < y_dim; ++y)
    canvas(x_offset, y) = HIGHLIGHT_COLOUR;
  for (int index = 0; index < int(plotslice.size()); ++index) {
    int x = std::round(float(index) / (plotslice.size() - 1) * (x_dim - 2 * pad));
    assert(x < x_dim);
    assert(x >= 0);
    int r0 = (index % 10) == 0 ? -pad : -std::max(1, pad/2);
    for (int y = r0; y < 0; ++y)
      canvas(x_offset + x, y_offset - y) = HIGHLIGHT_COLOUR;
  }

  for (int index = 0; index < int(plotslice.size()); ++index) {
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

    if (crosshair && ((plot_axis < 3 && index == focus[plot_axis]) || (plot_axis > 2 && index == current_index))) {
      // focus position: draw line
      for (int r = 0; r < y_offset; ++r)
        canvas (x_offset+x, r) = CROSSHAIR_COLOUR;
    }

    // plot line segment
    if (connect_dots) {
      assert(x > last_x);
      delta_x = x - last_x;
      int yp = 0;
      const int ydiff = y-last_y;
      for (int dx = 0; dx <= delta_x; ++dx) {
        while (std::round (float(delta_x*yp)/float(ydiff)) == dx) {
          canvas (x_offset + (last_x + dx), y_offset-(last_y+yp)) = STANDARD_COLOUR;
          if (ydiff > 0) {
            if (++yp > ydiff)
              break;
          }
          else {
            if (--yp < ydiff)
              break;
          }
        }
      }
    }

    // data: draw +
    for (int r = -radius; r <= radius; ++r)
      canvas(x_offset + x, y_offset - (y+r)) = HIGHLIGHT_COLOUR;
    for (int r = -radius; r <= radius; ++r)
      canvas(x_offset + (x + r), y_offset - y) = HIGHLIGHT_COLOUR;

    connect_dots = true;
    last_x = x; last_y = y;
  }

  image.index (plot_axis) = current_index;

  // encode buffer and print out:
  std::string out = move_down (2);
  if (show_text) out += CarriageReturn + str(vmax) + move_down(1) + CarriageReturn;
  out += encoder.write();
  if (show_text) out += ClearLine + str(vmin)
    + move_down(1) + CarriageReturn + ClearLine
    + "plot axis: " + str(plot_axis) + " | x range: [ 0 " + str(plotslice.size() - 1) + " ]";

  return out;
}








std::string display_image (ImageType& image, const Sixel::CMap& cmap, int colourbar_offset)
{
  std::string out;
  if (orthoview) {
    const int backup_slice_axis = slice_axis;

    Reslicer regrid[3] = {
      get_regridder (image, 0),
      get_regridder (image, 1),
      get_regridder (image, 2)
    };

    // set up canvas:
    const int panel_y_dim = std::max(regrid[0].size(2), regrid[2].size(1));
    Sixel::Encoder encoder (colourbar_offset + regrid[0].size(1)+regrid[1].size(0)+regrid[2].size(0),
        panel_y_dim, colourmaps);

    if (colorbar) draw_colourbar (encoder.viewport (0, 0, COLOURBAR_WIDTH), cmap);


    int x_pos = colourbar_offset;
    for (slice_axis = 0; slice_axis < 3; ++slice_axis) {
      set_axes();
      const int x_dim = regrid[slice_axis].size (x_axis);
      const int y_dim = regrid[slice_axis].size (y_axis);
      // recentring
      const int dy = (panel_y_dim - y_dim) / 2;
      auto view = encoder.viewport (x_pos, 0, regrid[slice_axis].size (x_axis), panel_y_dim);
      display_slice (image, regrid[slice_axis], view.viewport (0, dy), cmap);

      if (crosshair) {
        int x = std::round(x_dim - image.spacing(x_axis) * (focus[x_axis] + 0.5) * zoom);
        int y = std::round(y_dim - image.spacing(y_axis) * (focus[y_axis] + 0.5) * zoom);
        x = std::max (std::min (x, x_dim-1), 0);
        y = std::max (std::min (y, y_dim-1), 0);
        draw_crosshairs (view, x, y+dy, CROSSHAIR_COLOUR);
      }

      if (interactive && slice_axis == backup_slice_axis)
        draw_frame (view, HIGHLIGHT_COLOUR);

      x_pos += regrid[slice_axis].size (x_axis);
    }
    slice_axis = backup_slice_axis;
    set_axes();

    // encode buffer and print out:
    out += encoder.write();
  }
  else {
    auto regrid = get_regridder (image, slice_axis);
    const int x_dim = regrid.size (x_axis);
    const int y_dim = regrid.size (y_axis);

    Sixel::Encoder encoder (colourbar_offset+x_dim, y_dim, colourmaps);
    if (colorbar) draw_colourbar (encoder.viewport (0, 0, COLOURBAR_WIDTH), cmap);

    auto view = encoder.viewport(colourbar_offset, 0);
    display_slice (image, regrid, view, cmap);

    if (crosshair) {
      int x = std::round(x_dim - image.spacing(x_axis) * (focus[x_axis] + 0.5) * zoom);
      int y = std::round(y_dim - image.spacing(y_axis) * (focus[y_axis] + 0.5) * zoom);
      x = std::max (std::min (x, x_dim-1), 0);
      y = std::max (std::min (y, y_dim-1), 0);
      draw_crosshairs (view, x, y, CROSSHAIR_COLOUR);
    }

    //view.draw_colourbar ();

    // encode buffer and print out:
    out += encoder.write();
  }
  return out;
}






// Show the main image,
// run repeatedly to update display.
std::string display (ImageType& image, Sixel::ColourMaps& colourmaps)
{
  std::string out;
  auto& cmap = colourmaps[1];

  if (show_image) {
    set_axes();
    for (int n = 0; n < 3; ++n) {
      if (focus[n] < 0) focus[n] = 0;
      if (focus[n] >= image.size(n)) focus[n] = image.size(n)-1;
    }

    if (!cmap.scaling_set())
      autoscale (image, cmap);
    if (show_text) {
      out += ClearLine;
      if (arrow_mode == ARROW_COLOUR)
        out += TextForegroundYellow;
      out += str(cmap.max(),4) + TextReset + move_down(1) + position_cursor_at_col (2);
    }

    out += display_image (image, cmap, 2*COLOURBAR_WIDTH) + CarriageReturn + ClearLine;

    if (show_text) {
      if (arrow_mode == ARROW_COLOUR)
        out += TextForegroundYellow;
      out += str(cmap.min(), 4) + TextReset + move_down(1) + CarriageReturn;
    }
  }


  if (show_text) out += show_focus(image);

  if (interactive && orthoview && show_text) {
    out += " | active: ";
    switch (slice_axis) {
      case (0): out += std::string (TextUnderscore) + "s" + TextReset + "agittal"; break;
      case (1): out += std::string (TextUnderscore) + "c" + TextReset + "oronal"; break;
      case (2): out += std::string (TextUnderscore) + "a" + TextReset + "xial"; break;
      default: break;
    };
  }

  if (interactive && show_text)
    out += std::string(" | help: ") + TextUnderscore + "?" + TextReset;
  if (do_plot)
    out += plot (image, plot_axis);

  return out;
}














void show_help ()
{
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
    + key ("t", "toggle text overlay")
    + key ("v", "choose volume dimension")
    + key ("- / +", "zoom out / in")
    + key ("x / <space>", "toggle arrow key crosshairs control")
    + key ("b", "toggle arrow key brightness control")
    + key ("f", "show / hide crosshairs")
    + key ("r", "reset focus")
    + key ("i", "toggle between nearest (default) and linear interpolation")
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

  struct CallBack : public EventLoop::CallBack
  {
    bool operator() (int event, const std::vector<int>& param) override { return event == 0; }
  } callback;
  EventLoop (callback).run();

  std::cout << ClearScreen;
}






bool query_int (const std::string& prompt,
    int& value,
    int vmin = std::numeric_limits<int>::min(),
    int vmax = std::numeric_limits<int>::max())
{
  std::cout << CarriageReturn << ClearLine << prompt;
  std::cout.flush();

  struct CallBack : public EventLoop::CallBack
  {
    CallBack (int vmin, int vmax) : vmin (vmin), vmax (vmax) { }
    bool operator() (int event, const std::vector<int>& param) override
    {
      if (event == CarriageReturn)
        return false;
      if (event >= '0' && event <= '9') {
        response += char(event);
        std::cout << char(event);
        std::cout.flush();
      }
      else if (event == Backspace) {
        if (response.size()) {
          response.pop_back();
          std::cout << move_cursor (Left, 1) << ClearLineFromCursorRight;
          std::cout.flush();
        }
      }
      return true;
    }

    bool get_value (int& value) {
      if (response.size()) {
        value = to<int> (response);
        return (value >= vmin && value <= vmax);
      }
      return false;
    }

    int vmin, vmax;
    std::string response;
  } callback (vmin, vmax);
  EventLoop (callback).run();

  return callback.get_value (value);
}






class CallBack : public EventLoop::CallBack
{
  public:
    CallBack (ImageType& image) : image (image), xp (0), yp (0), need_update (true) { }

    bool operator() (int event, const std::vector<int>& param) override
    {

      if (!event) {
        if (need_update) {
          need_update = false;
          std::cout << CursorHome << display (image, colourmaps);
          std::cout.flush();
        }
        return true;;
      }

      need_update = true;

      if (event == 'q')
        return false;

      if (event == MouseEvent) {
        auto button = mouse_button (param[0]);
        bool mod = mouse_modifier (param[0]);
        int x = param[1];
        int y = param[2];

        if (x-xp > 127) xp += 256;
        if (xp-x > 127) xp -= 256;
        if (y-yp > 127) yp += 256;
        if (yp-y > 127) yp -= 256;

        switch (button) {
          case MouseWheelUp:
            focus[slice_axis] += mod ? 10 : 1;
            break;
          case MouseWheelDown:
            focus[slice_axis] -= mod ? 10 : 1;
            break;
          case MouseMoveLeft:
            focus[x_axis] += xp-x;
            focus[y_axis] += yp-y;
            break;
          case MouseMoveRight:
            colourmaps[1].update_scaling (x-xp, y-yp);
            break;
          default: break;
        }

        xp = x;
        yp = y;
        return true;
      }

      switch (event) {
        case Up:
          switch(arrow_mode) {
            case ARROW_SLICEVOL:  ++focus[slice_axis];   break;
            case ARROW_CROSSHAIR: ++focus[y_axis]; break;
            case ARROW_COLOUR:    colourmaps[1].update_scaling (0, -1); break;
            default: break;
          } break;
        case Down:
          switch(arrow_mode) {
            case ARROW_SLICEVOL:  --focus[slice_axis];   break;
            case ARROW_CROSSHAIR: --focus[y_axis]; break;
            case ARROW_COLOUR:    colourmaps[1].update_scaling (0, 1); break;
            default: break;
          } break;
        case Left:
          switch(arrow_mode) {
            case ARROW_SLICEVOL:  if (vol_axis >= 0) {
                                    --image.index(vol_axis);
                                    if (image.index(vol_axis) < 0) image.index(vol_axis) = image.size(vol_axis) - 1; }
                                  break;
            case ARROW_CROSSHAIR: ++focus[x_axis]; break;
            case ARROW_COLOUR:    colourmaps[1].update_scaling (-1, 0); break;
            default: break;
          } break;
        case Right:
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
        case 'a': slice_axis = 2; if (!orthoview) std::cout << ClearScreen; break;
        case 's': slice_axis = 0; if (!orthoview) std::cout << ClearScreen; break;
        case 'c': slice_axis = 1; if (!orthoview) std::cout << ClearScreen; break;
        case 'o': orthoview = !orthoview; std::cout << ClearScreen; break;
        case 't': show_text = colorbar = !show_text; std::cout << ClearScreen; break;
        case 'm': show_image = !show_image; std::cout << ClearScreen; break;
        case 'r': focus[x_axis] = std::round (image.size(x_axis)/2); focus[x_axis] = std::round (image.size(x_axis)/2);
                  focus[slice_axis] = std::round (image.size(slice_axis)/2); break;
        case 'i': interpolate = !interpolate; break;
        case '+': zoom *= 1.1; std::cout << ClearScreen; break;
        case '-': zoom /= 1.1; std::cout << ClearScreen; break;
        case ' ':
        case 'x': arrow_mode = x_arrow_mode = (x_arrow_mode == ARROW_SLICEVOL) ? ARROW_CROSSHAIR : ARROW_SLICEVOL; break;
        case 'b': arrow_mode = (arrow_mode == ARROW_COLOUR) ? x_arrow_mode : ARROW_COLOUR; break;
        case Escape: colourmaps[1].invalidate_scaling(); break;
        case 'l': {
                    int n;
                    if (query_int ("select number of levels: ", n, 1, 254)) {
                      levels = n;
                      colourmaps[1].set_levels (levels);
                    }
                  } break;
        case 'p': do_plot = query_int ("select plot axis [0 ... "+str(image.ndim()-1)+"]: ",
                      plot_axis, 0, image.ndim()-1);
                  if (!do_plot) std::cout << ClearScreen;
                  break;
        case '?': show_help(); break;

        default:
                  if (event >= '1' && event <= '9') {
                    size_t idx = event - '1';
                    if (idx < colourmap_choices_std.size()) {
                      colourmaps[1].ID = idx;
                      break;
                    }
                  }
                  /* for debugging purposes:
                  std::cerr << std::hex << event << " ( ";
                  for (const auto x : param)
                    std::cerr << x << " ";
                  std::cerr << ") "; */
                  need_update = false; break;
      }


      return true;
    }


  private:
    ImageType& image;
    int xp, yp;
    bool need_update;
};







void run ()
{
  auto image = Image<value_type>::open (argument[0]);

  size_t projection_axes[3] = {get_options("sagittal").size(), get_options("coronal").size(), get_options("axial").size()};
  size_t psum = 0;
  for (int i = 0; i < 3; ++i) {
    if (projection_axes[i]) { ++psum; slice_axis = i; }
    if (psum > 1) throw Exception("Projection axes options are mutually exclusive.");
  }
  orthoview = psum == 0;
  vol_axis = image.ndim() > 3 ? 3 : -1;
  set_axes();
  for (int a = 0; a < 3; ++a)
    focus[a] = std::round (image.size(a)/2.0);

  int colourmap_ID = get_option_value ("colourmap", 0);

  do_plot = get_options ("plot").size();
  plot_axis = get_option_value ("plot", plot_axis);
  if (plot_axis >= int(image.ndim()))
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

  opt = get_options ("focus");
  if (opt.size()) {
    vector<int> p = opt[0][0];
    if (p.size() > image.ndim())
      throw Exception ("number of indices passed to -focus option exceeds image dimensions");
    for (unsigned int n = 0; n < p.size(); ++n) {
      if (std::isfinite (p[n])) {
        if (p[n] < 0 || p[n] > image.size(n)-1)
          throw Exception ("position passed to -focus option is out of bounds for axis "+str(n));
        if (n < 3)
          focus[n] = p[n];
        else
          image.index(n) = p[n];
      }
    }
  }

  if (get_options ("nocrosshairs").size())
    crosshair = false;

  //CONF option: MRPeekScaleImage
  zoom = get_option_value ("zoom", MR::File::Config::get_float ("MRPeekZoom", zoom));
  if (zoom <= 0)
    throw Exception ("zoom value needs to be positive");
  INFO("zoom: " + str(zoom));
  zoom /= std::min (std::min (image.spacing(0), image.spacing(1)), image.spacing(2));

  colorbar = show_text = !get_options ("notext").size();
  show_image = !get_options ("noimage").size();

#ifdef MRTRIX_WINDOWS
  interactive = false;
  std::cout << display (image, colourmaps) << "\n";
#else
  interactive = isatty (STDOUT_FILENO);
  if (get_options ("batch").size())
    interactive = false;

  if (!interactive) {
    std::cout << display (image, colourmaps) << "\n";
    return;
  }

  try {
    // start loop
    enter_raw_mode();
    Sixel::init();
    std::cout << ClearScreen;

    CallBack callback (image);
    EventLoop event_loop (callback);
    event_loop.run();
    exit_raw_mode();
  }
  catch (...) {
    exit_raw_mode();
    throw;
  }
#endif
}
