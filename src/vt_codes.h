#ifndef __VT_CODES_H__
#define __VT_CODES_H__

namespace MR {
  namespace VT {

    const char* SixelStart = "\033Pq$";
    const char* SixelStop = "\033\\";

    const char* QueryReportStatus = "\0335n";
    const char* QueryDeviceAttributes = "\033[c";
    const char* LocalEchoOn = "\033[12l";
    const char* LocalEchoOff = "\033[12h";



  }
};


#endif

