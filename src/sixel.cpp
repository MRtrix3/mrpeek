#include "sixel.h"

namespace MR {
  namespace Sixel {

    namespace {
      bool need_newline_after_sixel = true;
    }

    std::string CMap::specifier () const {
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






    std::string Encoder::write () {
      std::string out = SixelStart + colourmap.specifier();

      int y = 0;
      for (; y < y_dim; y += 6)
        out += encode (y);

      out += SixelStop;

      if (need_newline_after_sixel)
        out += VT::move_cursor (VT::Down,1) + VT::CarriageReturn;

      return out;
    }






    std::string Encoder::encode (int y0) {
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





    std::string Encoder::encode (const int y0, const int intensity)
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






    void init()
    {
      int row, col;
      std::cout << VT::CursorHome << SixelStart << "#0;2;0;0;0$#0?!200-" << SixelStop;

      struct CallBack : public VT::EventLoop::CallBack
      {
        CallBack (int& x, int& y) : x (x), y(y) { }
        bool operator() (int event, const std::vector<int>& param) override {
          if (!event) {
            std::cout << VT::RequestCursorPosition;
            std::cout.flush();
            return true;
          }
          if (event == (VT::CSImask | 'R')) {
            x = param[0];
            y = param[1];
            return false;
          }
          return event != 'q';
        }
        int& x;
        int& y;
      } callback (row,col);

      VT::EventLoop (callback).run();
      need_newline_after_sixel = (row==1);
    }

  }
}

