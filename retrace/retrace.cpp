/**************************************************************************
 *
 * Copyright 2011 Jose Fonseca
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/


#include <string.h>
#include <iostream>

#include "os_time.hpp"
#include "retrace.hpp"

// RAMA
//
// Added to retrace.cpp

int		frame = 0;
bool	g_statesort = true;
int		g_startframe = 0;
FILE*	g_fp = 0x0;
FILE*	g_fp2 = 0x0;
char	g_buf[2048];
int		g_pass = 1;
int		cDraw = 0;
int		cDrawNdx = 0;

extern void stateCall ( trace::Call& call );


namespace retrace {


trace::DumpFlags dumpFlags = 0;


static bool call_dumped = false;


static void dumpCall(trace::Call &call) {
    if (verbosity >= 0 && !call_dumped) {
        std::cout << std::hex << call.thread_id << std::dec << " ";
        trace::dump(call, std::cout, dumpFlags);
        std::cout.flush();
        call_dumped = true;
    }
}


std::ostream &warning(trace::Call &call) {
    dumpCall(call);

    std::cerr << call.no << ": ";
    std::cerr << "warning: ";

    return std::cerr;
}


void ignore(trace::Call &call) {
    (void)call;
}

void unsupported(trace::Call &call) {
    warning(call) << "unsupported " << call.name() << " call\n";
}

inline void Retracer::addCallback(const Entry *entry) {
    assert(entry->name);
    assert(entry->callback);
    map[entry->name] = entry->callback;
}


void Retracer::addCallbacks(const Entry *entries) {
    while (entries->name && entries->callback) {
        addCallback(entries++);
    }
}


void Retracer::retrace(trace::Call &call) {
    call_dumped = false;

    Callback callback = 0;

    trace::Id id = call.sig->id;
    if (id >= callbacks.size()) {
        callbacks.resize(id + 1);
        callback = 0;
    } else {
        callback = callbacks[id];
    }

    if (!callback) {
        Map::const_iterator it = map.find(call.name());
        if (it == map.end()) {
            callback = &unsupported;
        } else {
            callback = it->second;
        }
        callbacks[id] = callback;
    }

    assert(callback);
    assert(callbacks[id] == callback);

    if (verbosity >= 1) {
        if (verbosity >= 2 ||
            (!(call.flags & trace::CALL_FLAG_VERBOSE) &&
             callback != &ignore)) {
            dumpCall(call);
        }
    }

	//if ( call.no < 2230 || call.no > 2240 ) {
    
	callback(call);

	// RAMA
	if ( frameNo >= g_startframe && (retrace::stateTraceRaw || retrace::stateTraceTxt) )
		stateCall(call);	
	
	//}	
}


} /* namespace retrace */
