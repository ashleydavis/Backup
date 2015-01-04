//
// Timer.cpp
//
// Copyright (c) 2011, Ashley Davis, ashleydavis@insanefx.com, www.insanefx.com
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted 
// provided that the following conditions are met:
//
// - Redistributions of source code must retain the above copyright notice, this list of conditions 
//   and the following disclaimer.
// - Redistributions in binary form must reproduce the above copyright notice, this list of conditions 
//   and the following disclaimer in the documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE 
// USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 

#include "Timer.h"

#include <windows.h> 

Timer::Timer()
{
#ifdef WIN32
	BOOL ok = QueryPerformanceFrequency((LARGE_INTEGER*)&frequency);
#endif // WIN32
}

void Timer::Start()
{
#ifdef WIN32
	BOOL ok = QueryPerformanceCounter((LARGE_INTEGER*)&startTime);
#endif // WIN32
}

double Timer::GetElapsedTime()
{
#ifdef WIN32
	LARGE_INTEGER curTime;
	BOOL ok = QueryPerformanceCounter(&curTime);

	return (((double) (curTime.QuadPart - ((LARGE_INTEGER*)&startTime)->QuadPart)) / ((LARGE_INTEGER*)&frequency)->QuadPart);
#else // WIN32
	return 0.0;
#endif // WIN32
}

double Timer::GetElapsedTimeAndReset()
{
#ifdef WIN32
	LARGE_INTEGER curTime;
	BOOL ok = QueryPerformanceCounter(&curTime);

	double elapsedTime = (((double) (curTime.QuadPart - ((LARGE_INTEGER*)&startTime)->QuadPart)) / ((LARGE_INTEGER*)&frequency)->QuadPart);

	*(LARGE_INTEGER*)&startTime = curTime;

	return elapsedTime;
#else // WIN32
	return 0.0;
#endif // WIN32
}

