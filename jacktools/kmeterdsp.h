// ------------------------------------------------------------------------
//
//  Copyright (C) 2008-2015 Fons Adriaensen <fons@linuxaudio.org>
//  Copyright (C) 2016 Filipe Coelho <falktx@falktx.com>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// ------------------------------------------------------------------------


#ifndef __KMETERDSP_H
#define __KMETERDSP_H


class Kmeterdsp
{
public:

    Kmeterdsp (void);
    ~Kmeterdsp (void);

    void process (float *p, int n);
    float read (void);

    static void init (int fsamp, int fsize, float hold, float fall);

private:

    float          _z0, _z1, _z2;  // filter state
    float          _dpk;           // current digital peak value
    int            _cnt;	   // digital peak hold counter


    static int     _hold;          // number of JACK periods to hold peak value
    static float   _fall;          // per period fallback multiplier for peak value
    static float   _wdcf;          // dc filter coefficient
    static float   _wrms;          // ballistic filter coefficient.
};


#endif
