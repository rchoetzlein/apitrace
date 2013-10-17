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

#include <d3d11.h>

#include <assert.h>

#include <string.h>

#include "retrace.hpp"
#include "retrace_swizzle.hpp"


namespace retrace {


struct Region
{
    void *buffer;
    unsigned long long size;
};

typedef std::map<unsigned long long, Region> RegionMap;
static RegionMap regionMap;


static inline bool
contains(RegionMap::iterator &it, unsigned long long address) {
    return it->first <= address && (it->first + it->second.size) > address;
}


static inline bool
intersects(RegionMap::iterator &it, unsigned long long start, unsigned long long size) {
    unsigned long it_start = it->first;
    unsigned long it_stop  = it->first + it->second.size;
    unsigned long stop = start + size;
    return it_start < stop && start < it_stop;
}


// Iterator to the first region that contains the address, or the first after
static RegionMap::iterator
lowerBound(unsigned long long address) {
    RegionMap::iterator it = regionMap.lower_bound(address);

    while (it != regionMap.begin()) {
        RegionMap::iterator pred = it;
        --pred;
        if (contains(pred, address)) {
            it = pred;
        } else {
            break;
        }
    }

#ifndef NDEBUG
    if (it != regionMap.end()) {
        assert(contains(it, address) || it->first > address);
    }
#endif

    return it;
}

// Iterator to the first region that starts after the address
static RegionMap::iterator
upperBound(unsigned long long address) {
    RegionMap::iterator it = regionMap.upper_bound(address);

#ifndef NDEBUG
    if (it != regionMap.end()) {
        assert(it->first >= address);
    }
#endif

    return it;
}

void
addRegion(unsigned long long address, void *buffer, unsigned long long size)
{
    if (retrace::verbosity >= 2) {
        std::cout
            << "region "
            << std::hex
            << "0x" << address << "-0x" << (address + size)
            << " -> "
            << "0x" << (uintptr_t)buffer << "-0x" << ((uintptr_t)buffer + size)
            << std::dec
            << "\n";
    }

    if (!address) {
        // Ignore NULL pointer
        assert(!buffer);
        return;
    }

#ifndef NDEBUG
    RegionMap::iterator start = lowerBound(address);
    RegionMap::iterator stop = upperBound(address + size - 1);
    if (0) {
        // Forget all regions that intersect this new one.
        regionMap.erase(start, stop);
    } else {
        for (RegionMap::iterator it = start; it != stop; ++it) {
            std::cerr << std::hex << "warning: "
                "region 0x" << address << "-0x" << (address + size) << " "
                "intersects existing region 0x" << it->first << "-0x" << (it->first + it->second.size) << "\n" << std::dec;
            assert(intersects(it, address, size));
        }
    }
#endif

    assert(buffer);

    Region region;
    region.buffer = buffer;
    region.size = size;

    regionMap[address] = region;
}

static RegionMap::iterator
lookupRegion(unsigned long long address) {
    RegionMap::iterator it = regionMap.lower_bound(address);

    if (it == regionMap.end() ||
        it->first > address) {
        if (it == regionMap.begin()) {
            return regionMap.end();
        } else {
            --it;
        }
    }

    assert(contains(it, address));
    return it;
}

void
delRegion(unsigned long long address) {
    RegionMap::iterator it = lookupRegion(address);
    if (it != regionMap.end()) {
        regionMap.erase(it);
    } else {
        assert(0);
    }
}


void
delRegionByPointer(void *ptr) {
    for (RegionMap::iterator it = regionMap.begin(); it != regionMap.end(); ++it) {
        if (it->second.buffer == ptr) {
            regionMap.erase(it);
            return;
        }
    }
    assert(0);
}

void *
lookupAddress(unsigned long long address) {
    RegionMap::iterator it = lookupRegion(address);
    if (it != regionMap.end()) {
        unsigned long long offset = address - it->first;
        assert(offset < it->second.size);
        void *addr = (char *)it->second.buffer + offset;

        if (retrace::verbosity >= 2) {
            std::cout
                << "region "
                << std::hex
                << "0x" << address
                << " <- "
                << "0x" << (uintptr_t)addr
                << std::dec
                << "\n";
        }

        return addr;
    }

    if (retrace::debug && address >= 64 * 1024 * 1024) {
        /* Likely not an offset, but an address that should had been swizzled */
        std::cerr << "warning: passing high address 0x" << std::hex << address << std::dec << " as uintptr_t\n";
    }

    return (void *)(uintptr_t)address;
}


class Translator : protected trace::Visitor
{
protected:
    bool bind;

    void *result;

    void visit(trace::Null *) {
        result = NULL;
    }

    void visit(trace::Blob *blob) {
        result = blob->toPointer(bind);
    }

    void visit(trace::Pointer *p) {
        result = lookupAddress(p->value);
    }

public:
    Translator(bool _bind) :
        bind(_bind),
        result(NULL)
    {}

    void * operator() (trace::Value *node) {
        _visit(node);
        return result;
    }
};


void *
toPointer(trace::Value &value, bool bind) {
    return Translator(bind) (&value);
}



static std::map<unsigned long long, void *> _obj_map;

void
addObj(trace::Call &call, trace::Value &value, void *obj) {
    unsigned long long address = value.toUIntPtr();

    if (!address) {
        if (obj) {
            warning(call) << "unexpected non-null object\n";
        }
        return;
    }

    if (!obj) {
        warning(call) << "got null for object 0x" << std::hex << address << std::dec << "\n";
    }

    _obj_map[address] = obj;
    
    if (retrace::verbosity >= 2) {
        std::cout << std::hex << "obj 0x" << address << " -> 0x" << size_t(obj) << std::dec << "\n";
    }
}

void
delObj(trace::Value &value) {
    unsigned long long address = value.toUIntPtr();
    _obj_map.erase(address);
    if (retrace::verbosity >= 2) {
        std::cout << std::hex << "obj 0x" << address << " del\n";
    }
}

void *
toObjPointer(trace::Call &call, trace::Value &value) {
    unsigned long long address = value.toUIntPtr();

    void *obj;
    if (address) {
        obj = _obj_map[address];
        if (!obj) {
            warning(call) << "unknown object 0x" << std::hex << address << std::dec << "\n";
        }
    } else {
        obj = NULL;
    }

    if (retrace::verbosity >= 2) {
        std::cout << std::hex << "obj 0x" << address << " <- 0x" << size_t(obj) << std::dec << "\n";
    }

    return obj;
}


} /* retrace */





// RAMA
//
// Code added to retrace_swizzle.cpp

typedef unsigned long long							hashVal;			// 8-byte value 
typedef std::map< unsigned long long, int >			MapPtrToID;			// 8-byte pointer to ID
typedef std::map < unsigned long long, hashVal >	MapPtrToHash;
typedef std::map < hashVal, int >					MapHashToID;

std::map< void*, unsigned long long >		_revmaps;			// Locked addresses reverse lookup

MapPtrToID									mapPtrToBin;		// Map from buffer addresses to bin groups
MapPtrToHash								mapPtrToHash;		// Map from buffer addresses to bin groups

class StateBin {
public:
	std::string				name;				// Name of state bin
	int						curr;				// ID of the current bin state
	int						change;				// How was value changed?
	int						bytes;				// Number of bytes transfered for this state
	int						cnt;				// Number of unique states
	MapHashToID				hashmap;			// Map from buffer value to unique state ID
};
std::vector< StateBin* >	vecBins;			// set of hashes for each bin

void createBins ()
{
	StateBin* bin;
	for (int n=0; n < 32; n++ ) {
		bin = new StateBin;
		bin->name = "";
		bin->cnt = 0;
		bin->curr = BIN_UNDEF;
		bin->change = BIN_UNDEF;
		bin->bytes = 0;
		bin->hashmap.clear ();
		vecBins.push_back ( bin );
	}
}

void resetBins ()
{
	StateBin* bin;
	for (int n=0; n < NUM_BIN; n++ ) {
		bin = vecBins[n];
		bin->change = BIN_UNDEF;
	}
}

void assignToBin ( unsigned long long ptr, int bin_id )
{
	if ( bin_id == BIN_UNKNOWN ) return;
	mapPtrToBin [ ptr ] = bin_id;
}

int getBin ( unsigned long long ptr )
{	
	// locate resource ptr
	if ( mapPtrToBin.find (ptr) !=  mapPtrToBin.end() ) {
		return mapPtrToBin [ ptr ];
	}
	// not found
	return BIN_UNKNOWN;		
}

int assignIDToHash ( hashVal val, int bin_id )
{
	StateBin* bin = vecBins[bin_id];

	if ( bin->hashmap.find ( val ) == bin->hashmap.end() ) {
		// hash not found. new value.
		bin->hashmap [ val ] = bin->cnt;    // *100 + bin_id;
		bin->cnt++;
	}
	return bin->hashmap [ val ];
}
void setPtrHashID ( unsigned long long ptr, int id )
{
	mapPtrToHash [ ptr ] = id;
}
int getPtrHashID ( unsigned long long ptr )
{
	if ( mapPtrToHash.find ( ptr ) != mapPtrToHash.end() ) 
		return mapPtrToHash [ ptr ];
	return -1;	
}

void getStates ( std::vector<int>& ivec )
{
	ivec.clear ();
	for (int n=0; n < vecBins.size(); n++) {
		ivec.push_back ( vecBins[n]->curr );
	}	
}
#define mini(a,b)  ( (a<b) ? a : b )

hashVal computeHash ( unsigned long long ptr, void* dat, int size )
{
	hashVal hash = 5381;    
	unsigned char* ch = (unsigned char*) dat;
	unsigned char* chend = ch + size;
	unsigned char* ptrdat = (unsigned char*) &ptr;

	hash = ((hash << 5) + hash) + ptrdat[0];
	hash = ((hash << 5) + hash) + ptrdat[1];
	hash = ((hash << 5) + hash) + ptrdat[2];
	hash = ((hash << 5) + hash) + ptrdat[3];
	if ( dat == 0x0 ) return hash;
	for (; ch != chend; ch++ ) {
		//fprintf ( g_fp, "%d", (unsigned int) *ch );
		hash = ((hash << 5) + hash) + (*ch);
    }
	//fprintf ( g_fp, "obj: %llud, hash: %llu\n", ptr, hash );
	return hash;
}

unsigned long long getLockedPtr ( void* dat )
{
	return _revmaps[dat];
	return 0;
}
#include <gl/gl.h>
#include <gl/glext.h>

unsigned long lastBindVBO = 0;
unsigned long lastBindTEX = 0;

void stateCall ( trace::Call& call ) 
{
	// OpenGL
	if ( strcmp(call.name(), "wglSwapBuffers") == 0 ) {
		stateSort ( BIN_SWITCH, "SwapBuffers", 100, BIN_PRESENT, 0, 0, 0, 0 );	
		return;
	}
	if ( strcmp(call.name(), "glDrawArrays")==0 ) {
		stateSort ( BIN_SWITCH, "DrawArrays", 101, BIN_DRAW, 0, 0, 0, call.arg(2).toSInt() );	
		return;
	}	
	if ( strcmp(call.name(), "glDrawElements")==0 ) {		
		if ( call.arg(3).toNull() != NULL ) {			// counterintuitive: if cast succeeds, trace::Null pointer is valid, i.e. not null. So, !=NULL means null value found.
			stateSort ( BIN_SWITCH, "DrawElem", 102, BIN_DRAW, 0, 0, 0, call.arg(1).toSInt() );	
		} else {
			const trace::Blob* blob = (trace::Blob*) (&call.arg(3));
			stateSort ( BIN_CREATE, "DrawElem", 102, BIN_DRAW, 0, 0, blob->buf, blob->size );	
		}
		return;
	}	
	if ( strcmp(call.name(), "glGenBuffers")==0 || strcmp(call.name(), "glGenBuffersARB")==0 ) {
		const trace::Array* arr = (trace::Array*) (&call.arg(1));
		unsigned long long ptr;
		for (int n=0; n < arr->values.size(); n++ ) {
			ptr = arr->values[n]->toUInt();
			stateSort ( BIN_CREATE, "GenBuffers", 104, BIN_UNKNOWN, ptr, ptr, 0, 0 );	
		}
		return;
	}
	if ( strcmp ( call.name(), "glBindBuffer" )==0 ) {
		GLenum target = static_cast<GLenum>((call.arg(0)).toSInt());
		unsigned long ptr = (call.arg(1)).toUInt();
		lastBindVBO = ptr;
		if ( target == GL_ARRAY_BUFFER )			stateSort ( BIN_SWITCH, "BindBuffer", 105, BIN_VERTEX0, ptr, ptr, 0, 0 );
		if ( target == GL_ELEMENT_ARRAY_BUFFER )	stateSort ( BIN_SWITCH, "BindBuffer", 105, BIN_INDEX, ptr, ptr, 0, 0 );
		return;
	}
	if ( strcmp ( call.name(), "glBufferData" )==0 && lastBindVBO != 0 ) {
		GLenum target = static_cast<GLenum>((call.arg(0)).toSInt());
		int size = (call.arg(1)).toSInt();
		char* dat = (char*) (call.arg(2)).toPointer();
		if ( target == GL_ARRAY_BUFFER )			stateSort ( BIN_SWITCH, "BufferData", 106, BIN_VERTEX0, lastBindVBO, lastBindVBO, dat, size  );
		if ( target == GL_ELEMENT_ARRAY_BUFFER )	stateSort ( BIN_SWITCH, "BufferData", 106, BIN_INDEX, lastBindVBO, lastBindVBO, dat, size );
		return;
	}

	if ( strcmp ( call.name(), "glCreateShader" )==0 ) {
		unsigned long ptr = (*call.ret).toUInt() + 20000;
		stateSort ( BIN_CREATE, "CreateShader", 107, BIN_SHADER, ptr, ptr, &ptr, 8 );
		return;
	}
	if ( strcmp ( call.name(), "glCreateProgram" )==0 ) {
		unsigned long ptr = (*call.ret).toUInt() + 20000;		
		stateSort ( BIN_CREATE, "CreateProgram", 108, BIN_SHADER, ptr, ptr, &ptr, 8 );
		return;
	}
	if ( strcmp ( call.name(), "glUseProgram" )==0 ) {
		unsigned long ptr = (call.arg(0)).toUInt() + 20000;
		stateSort ( BIN_SWITCH, "UseProgram", 109, BIN_SHADER, ptr, ptr, 0, 0 );
		return;
	}
	if ( strcmp (call.name(), "glGenTextures")==0 || strcmp(call.name(), "glGenTexturesEXT")==0 ) {
		const trace::Array* arr = (trace::Array *) (&call.arg(1));
		unsigned long long ptr;
		for (int n=0; n < arr->values.size(); n++ ) {
			ptr = arr->values[n]->toUInt() + 10000; 
			stateSort ( BIN_CREATE, "GenTextures", 110, BIN_TEXTURE, ptr, ptr, 0, 0 );	
		}
		return;
	}
	if ( strcmp ( call.name(), "glBindTexture" )==0 ) {
		unsigned long ptr = (call.arg(1)).toUInt() + 10000;
		lastBindTEX = ptr;
		stateSort ( BIN_SWITCH, "BindTexture", 111, BIN_TEXTURE, ptr, ptr, 0, 0 );
		return;
	}
	if ( strcmp ( call.name(), "glTexSubImage2D" )==0 && lastBindTEX != 0 ) {
		int size = (call.arg(4)).toSInt() * (call.arg(5)).toSInt();
		char* dat = (char*) (call.arg(8)).toPointer();
		GLenum format = static_cast<GLenum>((call.arg(6)).toSInt());
		switch ( format ) {
		case GL_RGB:	size *= 3;	break;
		case GL_RGBA:	size *= 4;	break;
		case GL_BGR:	size *= 3;	break;
		case GL_BGRA:	size *= 4;	break;
		}
		GLenum type = static_cast<GLenum>((call.arg(7)).toSInt());
		switch ( type ) {
		case GL_UNSIGNED_SHORT:	size *= 2;	break;
		case GL_UNSIGNED_INT:	size *= 4;	break;
		case GL_INT:			size *= 4;  break;
		case GL_FLOAT:			size *= 4;  break;
		}
		stateSort ( BIN_UPDATE, "TexSubImage2D", 112, BIN_TEXTURE, lastBindTEX, lastBindTEX, dat, size );
		return;
	}
	if ( strcmp ( call.name(), "glGetUniformLocation" )==0 ) {
		unsigned long ptr = (*call.ret).toSInt();		
		stateSort ( BIN_CREATE, "GetUniformLocation", 113, BIN_UNKNOWN, ptr, ptr, 0, 0 );
		return;
	}
	if ( strcmp ( call.name(), "glUniform1f" )==0 ) {
		unsigned long ptr = (call.arg(0)).toSInt();
		float dat[4];
		dat[0] = (call.arg(1)).toFloat();		
		stateSort ( BIN_UPDATE, "glUniform1f", 114, BIN_VSCONST0, ptr, ptr, dat, sizeof(float) );
		return;
	}
	if ( strcmp ( call.name(), "glUniform3f" )==0 ) {
		unsigned long ptr = (call.arg(0)).toSInt();
		float dat[4];
		dat[0] = (call.arg(1)).toFloat();		
		dat[1] = (call.arg(2)).toFloat();		
		dat[2] = (call.arg(3)).toFloat();		
		stateSort ( BIN_UPDATE, "glUniform3f", 115, BIN_VSCONST0, ptr, ptr, dat, sizeof(float)*3 );
		return;
	}
	if ( strcmp ( call.name(), "glUniform4f" )==0 ) {
		unsigned long ptr = (call.arg(0)).toSInt();
		float dat[4];
		dat[0] = (call.arg(1)).toFloat();		
		dat[1] = (call.arg(2)).toFloat();		
		dat[2] = (call.arg(3)).toFloat();		
		dat[3] = (call.arg(4)).toFloat();		
		stateSort ( BIN_UPDATE, "glUniform4f", 116, BIN_VSCONST0, ptr, ptr, dat, sizeof(float)*4 );
		return;
	}
	if ( strcmp ( call.name(), "glUniformMatrix4fv" )==0 ) {
		unsigned long ptr = (call.arg(0)).toSInt();
		const trace::Array* arr = (trace::Array*) (&call.arg(3));
		float dat[16];
		for (int n=0; n < 16; n++) {
			dat[n] = arr->values[n]->toFloat();
		}
		stateSort ( BIN_UPDATE, "glUniformMatrix4fv", 117, BIN_VSCONST0+1, ptr, ptr, dat, sizeof(float)*16 );
		return;
	}
	if ( strcmp ( call.name(), "glShaderSource" )==0 ) {
		unsigned long ptr = (call.arg(0)).toUInt() + 20000;
		const trace::Array* arr = (trace::Array*) (&call.arg(2));
		char dat[65535];
		char* pos = dat;
		int len;
		for (int n=0; n < arr->values.size(); n++ ) {
			len = strlen( arr->values[n]->toString() );
			strncpy ( pos, arr->values[n]->toString(), len );
			pos += len+1;			
		}		
		int siz = pos - dat;
		stateSort ( BIN_UPDATE, "ShaderSource", 118, BIN_SHADER, ptr, ptr, dat, siz );
		return;
	}
	if ( strcmp(call.name(), "glVertexPointer")==0 ) {		
		if ( call.arg(3).toPointer() != NULL ) {			
			const trace::Blob* blob = dynamic_cast<trace::Blob*>  (&call.arg(3));		
			if ( blob == 0x0 ) {
				stateSort ( BIN_SWITCH, "VertPointer", 119, BIN_VERTEX0, call.arg(3).toUIntPtr(), call.arg(3).toUIntPtr(), 0, 0 );
			} else {
				stateSort ( BIN_UPDATE, "VertPointer", 119, BIN_VERTEX0, 0, 0, blob->buf, blob->size );
			}
		}
		return;
	}	
	if ( strcmp(call.name(), "glNormalPointer")==0 ) {
		if ( call.arg(2).toPointer() != NULL ) {
			const trace::Blob* blob = dynamic_cast<trace::Blob*> (&call.arg(2));			
			if ( g_pass == 2 && call.no == 19610 ) {
				bool stop = true;
			}
			if ( blob == 0x0 ) {
				stateSort ( BIN_SWITCH, "NormPointer", 120, BIN_VERTEX1, call.arg(2).toUIntPtr(), call.arg(2).toUIntPtr(), 0, 0 );
			} else {
				stateSort ( BIN_UPDATE, "NormPointer", 120, BIN_VERTEX1, 0, 0, blob->buf, blob->size );
			}			
		}
		return;
	}
	if ( strcmp ( call.name(), "glLoadMatrixd" )==0 ) {				
		if ( call.arg(0).toNull() == NULL ) {		
			const trace::Array* arr = (trace::Array*) (&call.arg(0));
			float dat[16];
			for (int n=0; n < 16; n++) {
				dat[n] = arr->values[n]->toFloat();
			}
			stateSort ( BIN_UPDATE, "glLoadMatrixd", 121, BIN_PSCONST4, 0, 0, dat, sizeof(float)*16 ); 		
		}
		return;
	}
	if ( strcmp ( call.name(), "glLoadMatrixf" )==0 ) {		
		if ( call.arg(0).toNull() == NULL ) {	
			const trace::Array* arr = (trace::Array*) (&call.arg(0));
			float dat[16];
			for (int n=0; n < 16; n++) {
				dat[n] = arr->values[n]->toFloat();
			}				
			stateSort ( BIN_UPDATE, "glLoadMatrixf", 122, BIN_PSCONST4, 0, 0, dat, sizeof(float)*16 ); 
		}
		return;
	}


	// DirectX 11
	if ( strcmp(call.name(), "IDXGISwapChain::Present") == 0 ) {
		stateSort ( BIN_SWITCH, "Present", 0, BIN_PRESENT, 0, 0, 0, 0 );	
		return;
	}	
	if ( strcmp(call.name(), "ID3D10Device::DrawIndexed")==0 || strcmp(call.name(),"ID3D11DeviceContext::DrawIndexed")==0 ) {				// must come before draw (substr search)
		stateSort ( BIN_SWITCH, "DrawIdx", 1, BIN_DRAW, 0, 0, 0, call.arg(1).toUInt() );	
		return;
	}	
	if ( strcmp(call.name(), "ID3D10Device::DrawInstanced")==0 || strcmp(call.name(),"ID3D11DeviceContext::DrawInstanced")==0 ) {
		stateSort ( BIN_SWITCH, "DrawIst", 2, BIN_DRAW, 0, 0, 0, call.arg(1).toUInt() * call.arg(2).toUInt() );	
		return;
	}	
	if ( strcmp(call.name(), "ID3D10Device::Draw")==0 || strcmp(call.name(),"ID3D11DeviceContext::Draw")==0 ) {
		stateSort ( BIN_SWITCH, "Draw   ", 3, BIN_DRAW, 0, 0, 0, call.arg(1).toUInt() );	
		return;
	}	
	if ( strcmp(call.name(), "ID3D10Device::CreateBuffer")==0 || strcmp(call.name(),"ID3D11Device::CreateBuffer")==0  ) {		 
		const trace::Array* arr = (trace::Array*) (&call.arg(3));
		unsigned long long ptr;
		for (int n=0; n < arr->values.size(); n++ ) {
			ptr = arr->values[n]->toUIntPtr();
			stateSort ( BIN_CREATE, "CreateBuffer", 4, BIN_UNKNOWN, ptr, ptr, 0, 0 );
		}
		return;
	}
	if ( strcmp(call.name(), "ID3D10Device::CreateRenderTargetView")==0 || strcmp(call.name(),"ID3D11Device::CreateRenderTargetView")==0 ) {
		const trace::Array* arr = (trace::Array*) (&call.arg(3));
		unsigned long ptr = arr->values[0]->toUIntPtr();
		stateSort ( BIN_CREATE, "CreateRenderTargetView", 5, BIN_RENDER, ptr, ptr, &ptr, 8 );
		return;
	}	
	if ( strcmp(call.name(), "ID3D10Device::OMSetRenderTargets")==0 || strcmp(call.name(),"ID3D11DeviceContext::OMSetRenderTargets")==0  ) {		
		const trace::Array* arr = (trace::Array*) (&call.arg(2));
		unsigned long long ptr;
		for (int n=0; n < arr->values.size(); n++ ) {
			ptr = arr->values[n]->toUIntPtr();
			stateSort ( BIN_SWITCH, "OMSetRenderTargets", 6, BIN_RENDER, ptr, ptr, &ptr, 8 );		// object is the value
		}
		return;
	}	
	if ( strcmp(call.name(), "ID3D10Device::CreateRasterizerState")==0 || strcmp(call.name(),"ID3D11Device::CreateRasterizerState")==0  ) {
		const trace::Array* arr = (trace::Array*) (&call.arg(2));
		unsigned long ptr = arr->values[0]->toUIntPtr();
		stateSort ( BIN_CREATE, "CreateRasterizerState", 7, BIN_RASTER, ptr, ptr, &ptr, 8 );
		return;
	}	
	if ( strcmp(call.name(), "ID3D10Device::RSSetState")==0 || strcmp(call.name(),"ID3D11DeviceContext::RSSetState")==0   ) {				
		unsigned long long ptr = call.arg(1).toUIntPtr();
		stateSort ( BIN_SWITCH, "RSSetState", 8, BIN_RASTER, ptr, ptr, &ptr, 8 );		// object is the value		
		return;
	}
	if ( strstr ( call.name(), "ID3D10Device1::CreateVertexShader" ) != 0x0 ) {
		const trace::Array* arr = (trace::Array*) (&call.arg(3));
		unsigned long ptr = arr->values[0]->toUIntPtr();
		stateSort ( BIN_CREATE, "CreateVertexShader", 9, BIN_SHADER, ptr, ptr, &ptr, 8 );
		return;
	}
	if ( strstr ( call.name(), "ID3D11Device::CreateVertexShader" ) != 0x0 ) {
		const trace::Array* arr = (trace::Array*) (&call.arg(4));
		unsigned long ptr = arr->values[0]->toUIntPtr();
		stateSort ( BIN_CREATE, "CreateVertexShader", 10, BIN_SHADER, ptr, ptr, &ptr, 8 );
		return;
	}
	if ( strstr ( call.name(), "ID3D10Device::CreatePixelShader" ) != 0x0 ) {
		const trace::Array* arr = (trace::Array*) (&call.arg(3));
		unsigned long ptr = arr->values[0]->toUIntPtr();
		stateSort ( BIN_CREATE, "CreatePixelShader", 11, BIN_SHADER, ptr, ptr, &ptr, 8 );
		return;
	}	
	if ( strstr ( call.name(), "ID3D11Device::CreatePixelShader" ) != 0x0 ) {
		const trace::Array* arr = (trace::Array*) (&call.arg(4));
		unsigned long ptr = arr->values[0]->toUIntPtr();
		stateSort ( BIN_CREATE, "CreatePixelShader", 12, BIN_SHADER, ptr, ptr, &ptr, 8 );
		return;
	}	
	if ( strcmp(call.name(), "ID3D10Device::VSSetShader")==0 || strcmp(call.name(),"ID3D11DeviceContext::VSSetShader")==0   ) {		
		unsigned long long shader_ptr = call.arg(1).toUIntPtr();
		stateSort ( BIN_SWITCH, "VSSetShader", 13, BIN_SHADER, shader_ptr, shader_ptr, 0, 0 );	
		return;
	}	
	if ( strcmp(call.name(), "ID3D10Device::PSSetShader")==0 || strcmp(call.name(),"ID3D11DeviceContext::PSSetShader")==0  ) {		
		unsigned long long shader_ptr = call.arg(1).toUIntPtr();
		stateSort ( BIN_SWITCH, "PSSetShader", 14, BIN_SHADER, shader_ptr, shader_ptr, 0, 0 );	
		return;
	}
	if ( strcmp ( call.name(), "ID3D10Buffer::Map" ) == 0 ) {
		const trace::Array* arr = (trace::Array*) (&call.arg(3));
		stateSort ( BIN_UPDATE, "Map", 15, BIN_UNKNOWN, arr->values[0]->toUIntPtr(), 0, 0, 0 );		
		ID3D11DeviceContext *_this = static_cast<ID3D11DeviceContext *>(retrace::toObjPointer(call, call.arg(0)));
		//void* dest = _maps[_this];
		//_revmaps [ dest ] = arr->values[0]->toUIntPtr();
		return;
	}
	if ( strcmp ( call.name(), "ID3D11DeviceContext::Map" ) == 0 ) {
		stateSort ( BIN_UPDATE, "Map", 15, BIN_UNKNOWN, call.arg(1).toUIntPtr(), 0, 0, 0 );		
		ID3D11DeviceContext *_this = static_cast<ID3D11DeviceContext *>(retrace::toObjPointer(call, call.arg(0)));
		//void* dest = _maps[_this];
		//_revmaps [ dest ] = call.arg(1).toUIntPtr();
		return;
	}
	if (  strcmp(call.name(), "ID3D10Device::UpdateSubresource")==0 || strcmp(call.name(),"ID3D11DeviceContext::UpdateSubresource")==0 ) {
		const trace::Blob* blob = (trace::Blob*) (&call.arg(4));
		stateSort ( BIN_UPDATE, "UpdateSubresource", 16, BIN_UNKNOWN, call.arg(1).toUIntPtr(), call.arg(1).toUIntPtr(), blob->buf, blob->size );
		return;
	}
	if ( strcmp(call.name(), "ID3D10Device::IASetVertexBuffers")==0 || strcmp(call.name(),"ID3D11DeviceContext::IASetVertexBuffers")==0 ) {
		int num = (call.arg(2)).toUInt();
		const trace::Array* arr = (trace::Array*) (&call.arg(3));
		if (num > 5 ) num = 5;
		for (int n=0; n < num; n++) {
			unsigned long long ptr = arr->values[n]->toUIntPtr();
			stateSort ( BIN_SWITCH, "IASetVertexBuffers", 17, BIN_VERTEX0+n, ptr, ptr, 0, 0 );
		}
		return;
	}
	if ( strcmp(call.name(), "ID3D10Device::IASetIndexBuffer")==0 || strcmp(call.name(),"ID3D11DeviceContext::IASetIndexBuffer")==0  ) {		
		unsigned long long ptr = call.arg(1).toUIntPtr();
		stateSort ( BIN_SWITCH, "IASetIndexBuffer", 18, BIN_INDEX, ptr, ptr, 0, 0 );
		return;
	}
	if ( strcmp(call.name(), "ID3D10Device::VSSetConstantBuffers")==0 || strcmp(call.name(),"ID3D11DeviceContext::VSSetConstantBuffers")==0 ) {
		int num = (call.arg(2)).toUInt();
		const trace::Array* arr = (trace::Array*) (&call.arg(3));
		if (num > 5 ) num = 5;
		for (int n=0; n < num; n++) {
			unsigned long long ptr = arr->values[n]->toUIntPtr();
			stateSort ( BIN_SWITCH, "VSSetConstantBuffers", 19, BIN_VSCONST0 + n, ptr, ptr, 0, 0 );
		}
		return;
	}
	if ( strcmp(call.name(), "ID3D10Device::PSSetConstantBuffers")==0 || strcmp(call.name(),"ID3D11DeviceContext::PSSetConstantBuffers")==0  ) {
		int num = (call.arg(2)).toUInt();
		const trace::Array* arr = (trace::Array*) (&call.arg(3));
		if (num > 5 ) num = 5;
		for (int n=0; n < num; n++) {
			unsigned long long ptr = arr->values[n]->toUIntPtr();
			stateSort ( BIN_SWITCH, "PSSetConstantBuffers", 20, BIN_PSCONST0 + n, ptr, ptr, 0, 0 );
		}
		return;
	}	
	
}

static unsigned long long g_bytes = 0;
static int g_frame = 0;

 #define TXT_OUT

// write call
int packOutput ( char typ, int bin, int size, unsigned long long objptr, unsigned long long valid, char* name, char nameID )
{
	if ( retrace::stateTraceTxt ) {
		fprintf ( g_fp, "C: %02d %d %llu %llu %s\n", bin, size, objptr, valid, name);		
	}
	if ( retrace::stateTraceRaw ) {
		unsigned long long tstart = 0;	
		unsigned long long tstop = 0;	
		char* buf = g_buf;
		memcpy ( buf, (char*) &typ, 1 );				buf+=1;	
		memcpy ( buf, (char*) &nameID, 1 );				buf+=1;
		memcpy ( buf, (char*) &tstart, 8 );				buf+=8;
		memcpy ( buf, (char*) &tstop, 8 );				buf+=8;		// 18 byte header
		memcpy ( buf, (char*) &bin, 4 );				buf+=4;
		memcpy ( buf, (char*) &size, 4 );				buf+=4;		
		memcpy ( buf, (char*) &valid, 4 );				buf+=4;
		memcpy ( buf, (char*) &objptr, 8 );				buf+=8;		// 20 byte call data
	
		fwrite ( g_buf, 18+20, 1, g_fp2 );
	}
	return 18+20;
}

// write frame
int packOutput ( char typ, int g_frame, int g_bytes )
{
	if ( retrace::stateTraceTxt ) {
		fprintf ( g_fp, "FRAME: %d  (%llu)\n", g_frame++, g_bytes);			
	}
	if ( retrace::stateTraceRaw ) {
		unsigned long long tstart = 0;	
		unsigned long long tstop = 0;	
		char* buf = g_buf;
		char nameID = 0;	// present
		memcpy ( buf, (char*) &typ, 1 );				buf+=1;
		memcpy ( buf, (char*) &nameID, 1 );				buf+=1;
		memcpy ( buf, (char*) &tstart, 8 );				buf+=8;
		memcpy ( buf, (char*) &tstop, 8 );				buf+=8;
		memcpy ( buf, (char*) &g_frame, 4 );			buf+=4;
		memcpy ( buf, (char*) &g_bytes, 4 );			buf+=4;
		fwrite ( g_buf, 18+8, 1, g_fp2 );
	}
	return 18+8;
}

// write draw
int packOutput ( char typ, int prim_cnt, char* name, char nameID )
{
	char ct[5] = {'x', 'c', 'u', 's', '-'};
	int draw_bytes = 0;
	for (int n=0; n < NUM_BIN; n++ )
		draw_bytes += vecBins[n]->bytes;	
	
	if ( retrace::stateTraceRaw ) {
		unsigned long long tstart = 0;	
		unsigned long long tstop = 0;	
		char* buf = g_buf;	
		memcpy ( buf, (char*) &typ, 1 );				buf+=1;
		memcpy ( buf, (char*) &nameID, 1 );				buf+=1;
		memcpy ( buf, (char*) &tstart, 8 );				buf+=8;
		memcpy ( buf, (char*) &tstop, 8 );				buf+=8;	
	
		for (int n=0; n < NUM_BIN; n++ ) {
			memcpy ( buf, (char*) &vecBins[n]->curr, 4 );	buf+=4;		// val ID
			memcpy ( buf, (char*) &vecBins[n]->change, 1 );	buf+=1;		// change byte
			memcpy ( buf, (char*) &vecBins[n]->bytes, 4 );	buf+=4;		// bytes transfered			
		}
		// draw data
		char d = 'D';
		memcpy ( buf, (char*) &prim_cnt, 4 );			buf+=4;			// prim count
		memcpy ( buf, (char*) &d, 1 );					buf+=1;			// change byte ("draw")
		memcpy ( buf, (char*) &draw_bytes, 4 );			buf+=4;			// bytes transfered entire draw

		fwrite ( g_buf, 18 + NUM_BIN*9 + 9, 1, g_fp2 );
	}

	if ( retrace::stateTraceTxt ) {
		fprintf ( g_fp, "%s: ", name);
		for (int n=0; n < NUM_BIN; n++ ) 
			fprintf ( g_fp,  "%d%c[%d] ", vecBins[n]->curr, ct[ vecBins[n]->change ], vecBins[n]->bytes );
		fprintf ( g_fp, " %dD[%d]\n", prim_cnt, draw_bytes);
	}

	return draw_bytes;
}

void stateSort ( int change_type, char* name, int nameID, int bin, unsigned long long objptr, unsigned long long datptr, void* dat, int size )
{
	int id = -1;	
	if ( g_pass == 1 ) {		
		if ( bin >= BIN_DRAW ) return;
		assignToBin ( objptr, bin );		// assign pointer to bin based on function type. unknowns will be retrieved in pass 2
		//fprintf ( g_fp, "ptr: %llu, bin: %d (%s)\n", objptr, bin, name );
	} else {
		if ( bin == BIN_PRESENT ) {
			// write frame data
			packOutput ( 'F', g_frame, g_bytes );	
			g_bytes = 0;
			return;
		}
		if ( bin == BIN_DRAW ) {		
			// write draw data
			packOutput ( 'C', bin, size, objptr, vecBins[bin]->curr, name, nameID );
			int draw_bytes = packOutput ( 'D', size, name, nameID );
			g_bytes += draw_bytes;

			// clear bins for next draw
			for (int n=0; n < NUM_BIN; n++) {				
				vecBins[n]->bytes = 0;						// reset bin bytes for next draw				
				if ( vecBins[n]->change != BIN_UNDEF ) vecBins[n]->change = BIN_NOCHANGE;		// reset change status for next draw
			}
			
			return;
		}
		bin = getBin ( objptr );
		if ( bin == BIN_UNKNOWN ) bin = getBin ( datptr );
		if ( bin == BIN_UNKNOWN ) return;
		if ( change_type==BIN_CREATE || change_type==BIN_UPDATE ) {						
			if ( change_type <= vecBins[bin]->change || vecBins[bin]->change == BIN_UNDEF) {
				// compute hash value
				hashVal hval = computeHash ( objptr, dat, size );
				// insert value into hash (uniquely)
				id = assignIDToHash ( hval, bin );	
				// update the object to the current hash value
				setPtrHashID ( objptr, id );
				// update the state bin to current hash value 
				vecBins[bin]->curr = id;
				vecBins[bin]->change = change_type;	
				vecBins[bin]->bytes = 0;
				if ( change_type == BIN_UPDATE ) vecBins[bin]->bytes = size;
			}
		} else {
			// Switch			
			id = getPtrHashID ( objptr );
			if ( id != -1 && (change_type <= vecBins[bin]->change || vecBins[bin]->change == BIN_UNDEF )) {
				vecBins[bin]->curr = id;
				vecBins[bin]->change = change_type;			
				vecBins[bin]->bytes = 0;
			}
		}		
		// write call data
		packOutput ( 'C', bin, size, objptr, vecBins[bin]->curr, name, nameID );
	}	
	
} 
