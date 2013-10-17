/**************************************************************************
 *
 * Copyright 2011-2012 Jose Fonseca
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

#ifndef _RETRACE_SWIZZLE_HPP_
#define _RETRACE_SWIZZLE_HPP_


#include <map>

#include "trace_model.hpp"


namespace retrace {


/**
 * Handle map.
 *
 * It is just like a regular std::map<T, T> container, but lookups of missing
 * keys return the key instead of default constructor.
 *
 * This is necessary for several GL named objects, where one can either request
 * the implementation to generate an unique name, or pick a value never used
 * before.
 *
 * XXX: In some cases, instead of returning the key, it would make more sense
 * to return an unused data value (e.g., container count).
 */
template <class T>
class map
{
private:
    typedef std::map<T, T> base_type;
    base_type base;

public:

    T & operator[] (const T &key) {
        typename base_type::iterator it;
        it = base.find(key);
        if (it == base.end()) {
            return (base[key] = key);
        }
        return it->second;
    }
    
    const T & operator[] (const T &key) const {
        typename base_type::const_iterator it;
        it = base.find(key);
        if (it == base.end()) {
            return (base[key] = key);
        }
        return it->second;
    }

    /*
     * Handle situations where the application declares an array like
     *
     *   uniform vec4 myMatrix[4];
     *
     * then calls glGetUniformLocation(..., "myMatrix") and then infer the slot
     * numbers rather than explicitly calling glGetUniformLocation(...,
     * "myMatrix[0]"), etc.
     */
    T lookupUniformLocation(const T &key) {
        typename base_type::const_iterator it;
        it = base.upper_bound(key);
        if (it != base.begin()) {
            --it;
        } else {
            return (base[key] = key);
        }
        T t = it->second + (key - it->first);
        return t;
    }
};


void
addRegion(unsigned long long address, void *buffer, unsigned long long size);

void
delRegionByPointer(void *ptr);

void *
toPointer(trace::Value &value, bool bind = false);


void
addObj(trace::Call &call, trace::Value &value, void *obj);

void
delObj(trace::Value &value);

void *
toObjPointer(trace::Call &call, trace::Value &value);


} /* namespace retrace */


//----------- RAMA
// State sorting

#define BIN_UNDEF		0
#define BIN_CREATE		1
#define BIN_UPDATE		2
#define BIN_SWITCH		3
#define BIN_NOCHANGE	4

#define BIN_SHADER		0
#define BIN_RENDER		1
#define BIN_VIEWPORT	2
#define BIN_RASTER		3
#define BIN_DEPTH		4
#define BIN_BLEND		5
#define BIN_SAMPLER		6
#define BIN_INPUT		7
#define BIN_TEXTURE		8
#define BIN_VERTEX0		9		// IA slot 0
#define BIN_VERTEX1		10		// IA slot 1 ..
#define BIN_VERTEX2		11
#define BIN_VERTEX3		12
#define BIN_VERTEX4		13	
#define BIN_VSCONST0	14
#define BIN_VSCONST1	15
#define BIN_VSCONST2	16
#define BIN_VSCONST3	17
#define BIN_VSCONST4	18
#define BIN_PSCONST0	19
#define BIN_PSCONST1	20
#define BIN_PSCONST2	21
#define BIN_PSCONST3	22
#define BIN_PSCONST4	23
#define BIN_INDEX		24
#define NUM_BIN			25

#define BIN_DRAW		25
#define BIN_PRESENT		26

#define BIN_UNKNOWN		250


extern std::map<void *, void *> _maps;
extern std::map< void*, unsigned long long >	_revmaps;			// Locked addresses reverse lookup

void createBins ();
void assignToBin ( unsigned long long ptr, int bin_id );
void setPtrHashID ( unsigned long long ptr, int id );
int getBin ( unsigned long long ptr );
int mapToHash ( trace::Value& value, int bin_id );
void getStates ( std::vector<int>& ivec );
unsigned long long getLockedPtr ( void* dat );

void stateCall ( trace::Call& call );
void stateSort ( int create_type, char* name, int nameID, int bin, unsigned long long obj_ptr, unsigned long long dat_ptr, void* dat, int size );



#endif /* _RETRACE_SWIZZLE_HPP_ */

