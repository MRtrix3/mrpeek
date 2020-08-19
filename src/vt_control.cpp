#include <iostream>
#include <unistd.h>
#include <thread>

#ifndef MRTRIX_WINDOWS
# include <termios.h>
# include <poll.h>
#endif


#include "exception.h"
#include "vt_control.h"
#include "debug.h"
#include "mrtrix.h"


// implementation mostly gleaned from various sites, particularly:
// https://www.man7.org/linux/man-pages/man4/console_codes.4.html

// implementation currently ignores a lot of the possible control sequences#
// it is hoped that these would only be produced as output to the terminal, rather
// than received as input to the application...


namespace MR {
  namespace VT {



    namespace {
#ifndef MRTRIX_WINDOWS
      struct termios orig_termios;
#endif
    }


    void enter_raw_mode ()
    {
#ifndef MRTRIX_WINDOWS
      if (!isatty (STDOUT_FILENO))
        if (!freopen ("/dev/tty", "a", stdout))
          throw Exception ("failed to remap stdout to the terminal");

      if (!isatty (STDIN_FILENO))
        if (!freopen ("/dev/tty", "r", stdin))
          throw Exception ("failed to remap stdin to the terminal");

      // enable raw mode:
      struct termios raw;
      tcgetattr(STDIN_FILENO, &raw);
      orig_termios = raw;
      raw.c_iflag &= ~(ICRNL | IXON);
      raw.c_oflag &= ~(OPOST);
      raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
      raw.c_cc[VMIN] = 0;
      raw.c_cc[VTIME] = 0;
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif

      std::cout << CursorOff << MouseTrackingOn;
      std::cout.flush();
    }


    void exit_raw_mode ()
    {
#ifndef MRTRIX_WINDOWS
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
#endif
      std::cout << CursorOn << MouseTrackingOff << "\n";
      std::cout.flush();
    }








    void EventLoop::run ()
    {
      while (true) {
        param.clear();
        uint8_t c = next();

        if (c == Escape) {
         if (!esc())
           return;
        }
        else if (c == 0x9B) {
          if (!CSI())
            return;
        }
        else if (!callback (c, param))
          return;
      }
    }




    bool EventLoop::esc ()
    {
      if (current_char+1 >= nread)
        return callback (Escape, param);

      uint8_t c = next();
      if (c == '[')
        return CSI();
      else if (c == ']')
        return OSC();
      else if (c == 'O') {
        c = next();
        return callback (FunctionKey + c - 'P', param);
      }
      else if (c == Escape) {
        if (!callback (Escape, param))
          return false;
        return esc();
      }
      return true;
    }



    bool EventLoop::CSI ()
    {
      uint8_t c = next();
      if (c == '[') {
        next();
        return true;
      }
      if (c == 'M')
        return mouse ();

      // ignore initial question mark if encountered. Not sure whether this is
      // the right thing to do...
      if (c == '?')
        c = next();

      std::string buf;
      for (int n = 0; n < 16; ++n) {
        if (c >= '0' && c <= '9')
          buf.push_back (c);
        else if (c == ';') {
          param.push_back (buf.size() ? to<int>(buf) : 0);
          buf.clear();
        }
        else if (c == Escape) {
          param.clear();
          return esc();
        }
        else if ((c >= 0x07 && c <= 0x0F) || c == 0x7F) { /* Control characters - ignore */ }
        else if (c == 0x9B) { // CSI
          param.clear();
          return CSI();
        }
        else if (c == 0x18 || c == 0x1A) // abort
          return true;
        else {
          if (buf.size())
            param.push_back (to<int>(buf));
          return callback (CSImask | c, param);
        }
        c = next();
      }
      throw Exception ("unexpected input!");
    }


    bool EventLoop::OSC ()
    {
      throw Exception ("unexpected OSC sequence");
    }




    bool EventLoop::mouse ()
    {
      param.push_back (next()-0x20);
      param.push_back (next()-0x20);
      param.push_back (next()-0x20);

      return callback (MouseEvent, param);
    }



    void EventLoop::fill_buffer ()
    {
      current_char = 0;

#ifndef MRTRIX_WINDOWS
      struct pollfd pfd;
      pfd.fd = STDIN_FILENO;
      pfd.events = POLLIN;

      // if nothing on input stream, invoke idle event:
      poll (&pfd, 1, 0);
      if (!pfd.revents)
        callback (0, param);

      do {
        poll (&pfd, 1, -1);
        if (pfd.revents != POLLIN)
          throw Exception ("unexpected error on input stream");
        nread = read (STDIN_FILENO, buf, VT_READ_BUFSIZE);
        if (nread == -1 && errno != EAGAIN)
          throw Exception ("error reading user input");
      } while (nread == 0);
#endif
    }




  }
}



