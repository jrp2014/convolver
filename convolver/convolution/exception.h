#pragma once
// Convolver: DSP plug-in for Windows Media Player that convolves an impulse respose
// filter it with the input stream.
//
// Copyright (C) 2005  John Pavel
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include <atlbase.h>
#include <exception>
#include <string>
#include <sstream>


class convolutionException : public std::exception
{
public:
	convolutionException(std::string diagnostic) : std::exception(), diagnostic_(diagnostic) {}

	virtual ~convolutionException() {}

	virtual const char* what() const
	{
		return diagnostic_.c_str();
	}

protected:
	std::string diagnostic_;
};

class channelPathsException : public convolutionException
{
public:
	channelPathsException(std::string what, const TCHAR* path) : 
	  convolutionException("Problem with filter paths: "  + std::string(CT2CA(path)) + ": " + what) {}
};

class convolutionListException : public convolutionException
{
public:
	convolutionListException(std::string what, const TCHAR* path) :
	  convolutionException("Problem with filter paths list: " + std::string(CT2CA(path)) + ": " + what) {}
};

class wavfileException : public convolutionException
{
public:
	wavfileException(std::string what, const TCHAR* path, const char* strerror) :
	  convolutionException("Problem with sound file: " + std::string(CT2CA(path)) + ": " + what + " " + std::string(strerror)) {}
};


class filterException : public convolutionException
{
public:
	filterException(std::string what="") : convolutionException("Problem with filter: " + what) {}
	filterException(HRESULT& hr) : convolutionException("Problem with filter: ")
	{
		std::ostringstream ss;
		ss << diagnostic_ << std::hex << hr;
		diagnostic_ = ss.str();
	}
};

class versionException : public convolutionException
{
public:
	versionException(std::string what) :
	  convolutionException("Problem getting version information: " + what) {}
};
