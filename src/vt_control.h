#ifndef __VT_CODES_H__
#define __VT_CODES_H__

#include <sstream>
#include "mrtrix.h"

namespace MR {
  namespace VT {

    constexpr const char* ClearScreen = "\x1b[2J";
    constexpr const char* SaveScreen = "\x1b[?47h";
    constexpr const char* RestoreScreen = "\x1b[?47l";
    constexpr const char* CursorHome = "\x1b[H";
    constexpr const char* ClearLine = "\x1b[2K";
    constexpr const char* ClearLineFromCursorRight = "\x1b[0K";
    constexpr const char* CarriageReturn = "\r";

    constexpr const char* CursorOff = "\x1b[?25l";
    constexpr const char* CursorOn = "\x1b[?25h";

    constexpr const char* TextUnderscore = "\x1b[4m";
    constexpr const char* TextForegroundYellow = "\x1b[33m";
    constexpr const char* TextReset = "\x1b[0m";

    constexpr const char* MouseTrackingOn = "\x1b[?1002h";
    constexpr const char* MouseTrackingOff = "\x1b[?1002l";

    constexpr char Escape = '\x1b';
    constexpr char CtrlC = '\x03';
    constexpr char Backspace = '\x7F';
    constexpr int Up = 0x0101;
    constexpr int Down = 0x0102;
    constexpr int Right = 0x0103;
    constexpr int Left = 0x0104;
    constexpr int PageUp = 0x0105;
    constexpr int PageDown = 0x0106;
    constexpr int Tab = 0x0009;
    constexpr int ShiftTab = 0x0109;
    constexpr int Home = 0x0110;
    constexpr int End = 0x0111;
    constexpr int Delete = 0x0112;
    constexpr int MouseLeft = 0x0201;
    constexpr int MouseMiddle = 0x0202;
    constexpr int MouseRight = 0x0203;
    constexpr int MouseWheelUp = 0x0204;
    constexpr int MouseWheelDown = 0x0205;
    constexpr int MouseRelease = 0x0206;
    constexpr int MouseMoveLeft = 0x0211;
    constexpr int MouseMoveMiddle = 0x0212;
    constexpr int MouseMoveRight = 0x0213;

    constexpr inline int Ctrl (int c) { return c & 0x1F; }

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
    int read_user_input (int& x, int& y);

    void get_cursor_position (int& row, int& col);
    inline void get_terminal_size (int& rows, int& cols)
    {
      std::cout << position_cursor_at (999,999);
      get_cursor_position (rows,cols);
    }

  }
};


#endif

