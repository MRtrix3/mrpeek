#ifndef __SIXEL_H__
#define __SIXEL_H__

#include "colourmap.h"
#include "vt_control.h"


namespace MR {
  namespace Sixel {

    constexpr float BrightnessIncrement = 0.01f;
    constexpr float ContrastIncrement = 0.03f;

    class ColourMap {
      public:
        ColourMap (const ::MR::ColourMap::Entry& colourmapper, int number_colours) :
          num_colours (number_colours),
          _offset(NaN),
          _scale (NaN) {
            const auto& map_fn = colourmapper.basic_mapping;
            for (int n = 0; n <= num_colours; ++n) {
              const Eigen::Array3f colour = 100.0*map_fn (float(n)/num_colours);
              auto ns = str(std::round ((100.0*n)/num_colours));
              specifier += "#"+str(n)+";2;"+
                str(std::round(colour[0]))+";"+
                str(std::round(colour[1]))+";"+
                str(std::round(colour[2]));
            }
            specifier += "#"+str(num_colours+1)+";2;100;100;0";    // crosshair
            specifier += "#"+str(num_colours+2)+";2;30;30;30";     // boundingbox
            specifier += "#"+str(num_colours+3)+";2;55;55;40$\n";  // boundingbox highlight
          }

        const std::string& spec () const { return specifier; }
        const int maximum () const { return num_colours+3; }
        const int range () const { return num_colours; }
        const int crosshairs() const { return num_colours+1; }
        const int boundingbox(bool highlight = false) const { return num_colours+2+int(highlight); }


        // apply rescaling from floating-point value to clamped rescaled
        // integer:
        int rescale (float value) const {
          int val = std::round (_offset + _scale * value);
          return std::min (std::max (val,0), num_colours);
        }

        // set offset * scale parameters to adjust brightness / contrast:
        bool scaling_set () const { return std::isfinite (_offset) && std::isfinite (_scale); }
        void invalidate_scaling () { _offset = _scale = NaN; }
        void set_scaling (float offset, float scale) { _offset = offset*num_colours; _scale = scale*num_colours; }
        void set_scaling_min_max (float vmin, float vmax) { float dv = vmax - vmin; set_scaling (-vmin/dv, 1.0f/dv ); }
        void update_scaling (int x, int y) {
          float mid = (num_colours*(0.5f - BrightnessIncrement*x) - _offset) / _scale;
          _scale = std::exp (std::log(_scale) - ContrastIncrement * y);
          _offset = 0.5f*num_colours - _scale*mid;
        }
        const float offset () const { return _offset/num_colours; }
        const float scale () const { return _scale/num_colours; }
        const float min () const { return -offset() / scale(); }
        const float max () const { return (1.f - offset()) / scale(); }

      private:
        int num_colours;
        float _offset, _scale;
        std::string specifier;
    };





    // template parameters set horizontal and vertical grid size
    template <int gx = 1, int gy = 1>
    class Encoder {
      public:
        Encoder (int x_dim, int y_dim, const ColourMap& colourmap) :
          x_dim (x_dim),
          y_dim (y_dim),
          gi (0), gj (0),
          // make sure data buffer is a multiple of 6 to avoid overflow:
          data (gx*x_dim*6*std::ceil(gy*y_dim/6.0), 0),
          colourmap (colourmap),
          current (255),
          repeats (0) { }

        void set_panel (int k) {
          assert (k < gx*gy);
          gi = k % gx;
          gj = k / gx;
        }

        // set value at (x,y), rescaling as per colourmap parameters:
        void operator() (int x, int y, float value) {
          int val = colourmap.rescale (value);
          data[mapxy(x,y)] = val;
        }

        // add yellow crosshairs at the specified position:
        void draw_crosshairs (int x0, int y0) {
          for (int x = 0; x < x_dim; ++x)
            data[mapxy(x,y0)] = colourmap.crosshairs();
          for (int y = 0; y < y_dim; ++y)
            data[mapxy(x0,y)] = colourmap.crosshairs();
        }

        // add yellow crosshairs at the specified position:
        void draw_boundingbox (bool highlight = false) {
          for (int x = 0; x < x_dim; ++x) {
            data[mapxy(x,0)] = colourmap.boundingbox(highlight);
            data[mapxy(x,y_dim-1)] = colourmap.boundingbox(highlight);
          }
          for (int y = 0; y < y_dim; ++y) {
            data[mapxy(0,y)] = colourmap.boundingbox(highlight);
            data[mapxy(x_dim-1,y)] = colourmap.boundingbox(highlight);
          }
        }

        void draw_colourbar () {
          const int h = 90, w = 14;
          if (x_dim < 2*w || y_dim < 2*h)
            return;
          int y1 = y_dim-4, y0 = y1-h-1;
          int x1 = x_dim-4, x0 = x1-w-1;

          for (int y = y0; y <= y1; y++) {
            int val = (h-(y-y0)) * colourmap.range() / h;
            for (int x = x0; x <= x1; x++) {
              data[mapxy(x,y)] = (y==y0 || y==y1 || x==x0 || x==x1) ? colourmap.crosshairs() : val;
            }
          }
        }

        // once slice is fully specified, encode and write to stdout:
        void write () {
          std::string out = VT::SixelStart + colourmap.spec();

          for (int y = 0; y < gy*y_dim; y += 6)
            out += encode (y);

          out += VT::SixelStop;
          std::cout << out;
        }

      private:

        int x_dim, y_dim;
        int gi, gj;
        std::vector<uint8_t> data;
        const ColourMap& colourmap;
        std::string buffer;
        uint8_t current;
        int repeats;

        inline size_t mapxy (int x, int y) const {
          assert (x < x_dim);
          assert (y < y_dim);
          return x + x_dim*(gi + gx*(y + y_dim*gj));
        }

        std::string encode (int y0) {
          std::string out;

          for (int intensity = 0; intensity <= colourmap.maximum(); ++intensity) {
            for (int i = y0*gx*x_dim; i < (y0+6)*gx*x_dim; ++i) {
              // if any voxel in buffer has this intensity, then need to encode the
              // whole row of sixels:
              if (data[i] == intensity) {
                out += encode (y0, intensity);
                break;
              }
            }
          }
          // replace last character from $ (carriage return) to '-' (newline):
          out.back() = '-';
          return out;
        }


        std::string encode (const int y0, const int intensity)
        {
          std::string out;
          clear();
          for (int x = 0; x < gx*x_dim; ++x) {
            int index = x + y0*gx*x_dim;
            uint8_t s = 0;
            if (data[index] == intensity) s |= 1U; index += gx*x_dim;
            if (data[index] == intensity) s |= 2U; index += gx*x_dim;
            if (data[index] == intensity) s |= 4U; index += gx*x_dim;
            if (data[index] == intensity) s |= 8U; index += gx*x_dim;
            if (data[index] == intensity) s |= 16U; index += gx*x_dim;
            if (data[index] == intensity) s |= 32U;
            add (s);
          }
          commit (true);
          out += "#" + str(intensity) + buffer + '$';
          return out;
        }

        void add (uint8_t c) {
          if (c == current)
            ++repeats;
          else {
            commit();
            current = c;
            repeats = 1;
          }
        }

        void clear () {
          buffer.clear();
          repeats = 0;
          current = 255;
        }

        void commit (bool is_last = false) {
          if (is_last && current == 0)
            return;
          switch (repeats) {
            case 0: break;
            case 3: buffer += char (63+current);
            case 2: buffer += char (63+current);
            case 1: buffer += char (63+current); break;
            default: buffer += '!'+str(repeats)+char(63+current);
          }
        }
    };


    bool test_need_newline_after_sixel()
    {
      int x, y;
      std::cout << VT::CursorHome << VT::SixelStart << "#0;2;0;0$#0?!200-" << VT::SixelStop;
      VT::get_cursor_position (x,y);
      return y > 1;
    }

  }
}

#endif


