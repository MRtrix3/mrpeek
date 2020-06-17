#ifndef __VT_CODES_H__
#define __VT_CODES_H__

namespace MR {
  namespace VT {

    constexpr const char* SixelStart = "\033Pq$";
    constexpr const char* SixelStop = "\033\\";

    constexpr const char* CursorOff = "\033[?25l";
    constexpr const char* CursorOn = "\033[?25h";


    void enter_raw_mode();
    void exit_raw_mode();
    int read_user_input();


  }
};


#endif

