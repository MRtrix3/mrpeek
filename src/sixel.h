#ifndef __SIXEL_H__
#define __SIXEL_H__

#include "colourmap.h"
#include "vt_control.h"


namespace MR {
  namespace Sixel {

    namespace {
      bool need_newline_after_sixel = true;
#ifndef NDEBUG
      uint8_t* data_debug = nullptr;
#endif
    }

    constexpr float BrightnessIncrement = 0.01f;
    constexpr float ContrastIncrement = 0.03f;

    constexpr const char* SixelStart = "\033Pq$";
    constexpr const char* SixelStop = "\033\\";


    class CMap {
      public:
        CMap (int ID, int index, int ncolours) :
          ID (ID),
          index (index),
          ncolours (ncolours),
          _offset (NaN),
          _scale (NaN) { }


        // apply rescaling from floating-point value to clamped rescaled
        // integer:
        int operator() (float value) const {
          int val = std::round (_offset + _scale * value);
          return index + std::min (std::max (val,0), ncolours);
        }

        // set offset * scale parameters to adjust brightness / contrast:
        bool scaling_set () const { return std::isfinite (_offset) && std::isfinite (_scale); }
        void invalidate_scaling () { _offset = _scale = NaN; }
        void set_scaling (float offset, float scale) { _offset = offset*ncolours; _scale = scale*ncolours; }
        void set_scaling_min_max (float vmin, float vmax) { float dv = vmax - vmin; set_scaling (-vmin/dv, 1.0f/dv ); }
        void update_scaling (int x, int y) {
          float mid = (ncolours*(0.5f - BrightnessIncrement*x) - _offset) / _scale;
          _scale = std::exp (std::log(_scale) - ContrastIncrement * y);
          _offset = 0.5f*ncolours - _scale*mid;
        }
        const float offset () const { return _offset/ncolours; }
        const float scale () const { return _scale/ncolours; }
        const float min () const { return -offset() / scale(); }
        const float max () const { return (1.f - offset()) / scale(); }

        void set_levels (int levels) {
          float m = scale(), c = offset();
          ncolours = levels;
          set_scaling (c, m);
        }
        int levels () const { return ncolours; }

        int last_index () const { return index + ncolours; }

        std::string specifier () const {
          std::string out;
          if (ID<0)
            return out;
          const auto& map_fn = ::MR::ColourMap::maps[ID].basic_mapping;
          for (int n = 0; n <= ncolours; ++n) {
            const Eigen::Array3f colour = 100.0f*map_fn (float(n)/ncolours);
            out += "#"+str(index+n)+";2;"+
              str(std::round(colour[0]))+";"+
              str(std::round(colour[1]))+";"+
              str(std::round(colour[2]));
          }
          return out;
        }

        int ID, index;

      private:
        int ncolours;
        float _offset, _scale;
    };






    class ColourMaps {
      public:
        void add (int colourmap_ID, int num_colours) {
          cmaps.push_back({ colourmap_ID, next_index(), num_colours });
        }

        void add (const std::vector<std::array<int, 3>>& colours) {
          assert (cmaps.empty());
          fixed_cmap_specifier.clear();
          cmaps.push_back ({ -1, 0, int(colours.size()) });
          for (int n = 0; n < colours.size(); ++n) {
            fixed_cmap_specifier += "#"+str(n)+";2;"+
              str(colours[n][0])+";"+
              str(colours[n][1])+";"+
              str(colours[n][2]);
          }
        }
        int size () const { return cmaps.size(); }
        const CMap& operator[] (int n) const { return cmaps[n]; }
        CMap& operator[] (int n) { return cmaps[n]; }

        std::string specifier () const {
          std::string out = fixed_cmap_specifier;
          for (const auto& c : cmaps)
            out += c.specifier();
          return out;
        }
        const int maximum () const { return cmaps.back().last_index(); }

      private:
        std::vector<CMap> cmaps;
        std::string fixed_cmap_specifier;

        int next_index () const {
          return cmaps.size() ? cmaps.back().last_index()+1 : 0;
        }

    };






    class ViewPort {
      public:
        ViewPort (uint8_t* data, int x_dim, int y_dim, int x_stride) :
          data (data),
          x_dim (x_dim),
          y_dim (y_dim),
          x_stride (x_stride) {
#ifndef NDEBUG
            int x = (data - data_debug) % x_stride;
            int y = (data - data_debug) / x_stride;
            std::cerr << "viewport at " << x << " " << y
              << ", size " << x_dim << " " << y_dim
              << ", stride " << x_stride
              << ", max " << x+x_dim << " " << y+y_dim << "\n";
#endif
          }

        uint8_t& operator() (int x, int y) const {
          assert (x >= 0 && x < x_dim);
          assert (y >= 0 && y < y_dim);
          return data[x+x_stride*y];
        }

        int xdim () const { return x_dim; }
        int ydim () const { return y_dim; }

        ViewPort viewport (int x, int y, int size_x = -1, int size_y = -1) const {
          if (size_x < 0) size_x = x_dim-x;
          if (size_y < 0) size_y = y_dim-y;
          return { data + x + y*x_stride, size_x, size_y, x_stride };
        }

      private:
        uint8_t* data;
        int x_dim, y_dim, x_stride;
    };






    // template parameters set horizontal and vertical grid size
    class Encoder {
      public:
        Encoder (int x_dim, int y_dim, const ColourMaps& colourmap) :
          colourmap (colourmap),
          x_dim (x_dim),
          y_dim (y_dim),
          data (x_dim*y_dim, 0),
          current (255),
          repeats (0) {
#ifndef NDEBUG
            data_debug = &data[0]; std::cerr << "canvas: " << x_dim << " " << y_dim << "\n";
#endif
          }

        // once slice is fully specified, encode and write to string:
        std::string write () {
          std::string out = SixelStart + colourmap.specifier();

          int y = 0;
          for (; y < y_dim; y += 6)
            out += encode (y);

          out += SixelStop;

          if (need_newline_after_sixel)
            out += VT::move_cursor (VT::Down,1) + VT::CarriageReturn;

          return out;
        }

        ViewPort viewport (int x, int y, int size_x = -1, int size_y = -1) {
          if (size_x < 0) size_x = x_dim-x;
          if (size_y < 0) size_y = y_dim-y;
          return { &data[0] + x + y*x_dim, size_x, size_y, x_dim };
        }

        ViewPort viewport () {
          return { &data[0], x_dim, y_dim, x_dim };
        }

      private:

        const ColourMaps& colourmap;
        int x_dim, y_dim;
        std::vector<uint8_t> data;
        std::string buffer;
        uint8_t current;
        int repeats;

        std::string encode (int y0) {
          const int nsixels = std::min (y_dim-y0, 6);
          std::string out;

          for (int intensity = 0; intensity <= colourmap.maximum(); ++intensity) {
            for (int i = y0*x_dim; i < (y0+nsixels)*x_dim; ++i) {
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
          const int nsixels = std::min (y_dim-y0, 6);
          std::string out;
          clear();
          for (int x = 0; x < x_dim; ++x) {
            const int index = x + y0*x_dim;
            uint8_t s = 0;
            switch (nsixels) {
              case 6: if (data[index+5*x_dim] == intensity) s |= 32U;
              case 5: if (data[index+4*x_dim] == intensity) s |= 16U;
              case 4: if (data[index+3*x_dim] == intensity) s |=  8U;
              case 3: if (data[index+2*x_dim] == intensity) s |=  4U;
              case 2: if (data[index+  x_dim] == intensity) s |=  2U;
              case 1: if (data[index]         == intensity) s |=  1U; break;
              default: assert (false /* shouldn't be here*/);
            }
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






    inline void init()
    {
      int row, col;
      std::cout << VT::CursorHome << SixelStart << "#0;2;0;0;0$#0?!200-" << SixelStop;
      VT::get_cursor_position (row,col);
      need_newline_after_sixel = (row==1);
    }

  }
}

#endif


