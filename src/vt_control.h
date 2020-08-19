#ifndef __VT_CODES_H__
#define __VT_CODES_H__

#include <sstream>
#include "mrtrix.h"
#include "debug.h"

#define VT_READ_BUFSIZE 256

namespace MR {
  namespace VT {

    constexpr const char* ClearScreen = "\x1b[2J";
    constexpr const char* SaveScreen = "\x1b[?47h";
    constexpr const char* RestoreScreen = "\x1b[?47l";
    constexpr const char* CursorHome = "\x1b[H";
    constexpr const char* ClearLine = "\x1b[2K";
    constexpr const char* ClearLineFromCursorRight = "\x1b[0K";

    constexpr const char* CursorOff = "\x1b[?25l";
    constexpr const char* CursorOn = "\x1b[?25h";

    constexpr const char* TextUnderscore = "\x1b[4m";
    constexpr const char* TextForegroundYellow = "\x1b[33m";
    constexpr const char* TextReset = "\x1b[0m";

    constexpr const char* MouseTrackingOn = "\x1b[?1002h";
    constexpr const char* MouseTrackingOff = "\x1b[?1002l";

    constexpr const char* UpDownArrow = "\u2194";
    constexpr const char* LeftRightArrow = "\u2195";

    constexpr const char* RequestCursorPosition = "\x1b[6n";

    constexpr char Escape = '\x1b';
    constexpr char CtrlC = '\x03';
    constexpr char CarriageReturn = '\r';
    constexpr char Backspace = '\x7F';
    constexpr int Up = 0x0141;
    constexpr int Down = 0x0142;
    constexpr int Right = 0x0143;
    constexpr int Left = 0x0144;
    constexpr int CSImask = 0x0100;
    constexpr int MouseEvent = 0x1000;
    constexpr int FunctionKey = 0x2000;

    enum MouseButton {
      MouseLeft, MouseMiddle, MouseRight,
      MouseRelease, MouseWheelUp, MouseWheelDown, MouseMoveLeft,
      MouseMoveMiddle, MouseMoveRight
    };

    constexpr inline int Ctrl (int c) { return c & 0x1F; }
    constexpr inline bool mouse_modifier (int c) { return c & 0x1C; }
    inline MouseButton mouse_button (int c)
    {
      switch (c & 0x63) {
        case 0x00: return MouseLeft;
        case 0x01: return MouseMiddle;
        case 0x02: return MouseRight;
        case 0x03: return MouseRelease;
        case 0x20: return MouseMoveLeft;
        case 0x21: return MouseMoveMiddle;
        case 0x22: return MouseMoveRight;
        case 0x40: return MouseWheelUp;
        case 0x41: return MouseWheelDown;
        default: throw Exception ("unexpected mouse button");
      }
    }


    inline std::string position_cursor_at (int row, int column)
    { return MR::printf ("\x1b[%d;%dH", row, column); }

    inline std::string move_cursor (int direction, int n)
    {
      char d=0;
      switch (direction) {
        case Up:    d = 'A'; break;
        case Down:  d = 'B'; break;
        case Left:  d = 'D'; break;
        case Right: d = 'C'; break;
        default: assert (0 /* invalid cursor direction */);
      }
      return MR::printf ("\x1b[%d", n)+d;
    }
    inline std::string position_cursor_at_col (int col)
    { return MR::printf ("\x1b[%dG", col); }


    void enter_raw_mode();
    void exit_raw_mode();



    class EventLoop
    {
      public:
        class CallBack {
          public:
            virtual bool operator() (int event, const std::vector<int>& param) = 0;
        };


        EventLoop (CallBack& callback) :
          callback (callback), current_char (0), nread (0) { }

        void run ();
      private:
        CallBack& callback;
        uint8_t buf[VT_READ_BUFSIZE];
        int current_char, nread;
        std::vector<int> param;

        uint8_t next() {
          ++current_char;
          if (current_char >= nread)
            fill_buffer();
          return buf[current_char];
        }

        void fill_buffer ();
        bool esc ();
        bool CSI ();
        bool OSC ();
        bool mouse ();
    };


  }
};


#endif

