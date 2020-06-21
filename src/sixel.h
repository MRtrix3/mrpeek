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
            specifier += "#"+str(num_colours+1)+";2;100;100;0$\n";
          }

        const std::string& spec () const { return specifier; }
        const int maximum () const { return num_colours+1; }
        const int range () const { return num_colours; }
        const int crosshairs() const { return num_colours+1; }


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





    class Encoder {
      public:
        Encoder (int x_dim, int y_dim, const ColourMap& colourmap) :
          x_dim (x_dim),
          y_dim (y_dim),
          // make sure data buffer is a multiple of 6 to avoid overflow:
          data (x_dim*6*std::ceil(y_dim/6.0), 0),
          colourmap (colourmap),
          current (255),
          repeats (0) { }

        // set value at (x,y), rescaling as per colourmap parameters:
        void operator() (int x, int y, float value) {
          int val = colourmap.rescale (value);
          data[x+x_dim*y] = val;
        }

        // add yellow crosshairs at the specified position:
        void draw_crosshairs (int x0, int y0) {
          for (int x = 0; x < x_dim; ++x)
            data[x+x_dim*y0] = colourmap.crosshairs();
          for (int y = 0; y < y_dim; ++y)
            data[x0+x_dim*y] = colourmap.crosshairs();
        }

        // once slice is fully specified, encode and write to stdout:
        void write () {
          std::string out = VT::SixelStart + colourmap.spec();

          for (int y = 0; y < y_dim; y += 6)
            out += encode (y);

          out += VT::SixelStop;
          std::cout << out;
        }

      private:

        int x_dim, y_dim;
        std::vector<uint8_t> data;
        const ColourMap& colourmap;
        std::string buffer;
        uint8_t current;
        int repeats;

        std::string encode (int y0) {
          std::string out;

          for (int intensity = 0; intensity <= colourmap.maximum(); ++intensity) {
            for (int i = y0*x_dim; i < (y0+6)*x_dim; ++i) {
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
          for (int x = 0; x < x_dim; ++x) {
            int index = x + y0*x_dim;
            uint8_t s = 0;
            if (data[index] == intensity) s |= 1U; index += x_dim;
            if (data[index] == intensity) s |= 2U; index += x_dim;
            if (data[index] == intensity) s |= 4U; index += x_dim;
            if (data[index] == intensity) s |= 8U; index += x_dim;
            if (data[index] == intensity) s |= 16U; index += x_dim;
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

  }
}

#endif


