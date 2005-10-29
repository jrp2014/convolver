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

#include "convolution\samplebuffer.h"

#if defined(DEBUG) | defined(_DEBUG)
void DumpChannelBuffer(const ChannelBuffer &buffer )
{
	const size_t LIMIT = 16; // Don't print too much
	size_t limit = buffer.size() > LIMIT ? LIMIT : buffer.size();

	for (int nSample = 0; nSample < limit; ++nSample)
	{
		cdebug << buffer[nSample] << " ";
	}
}

void DumpSampleBuffer(const SampleBuffer& buffer)
{
	cdebug << "SampleBuffer: " ;

	for (int nChannel = 0; nChannel < buffer.size(); ++nChannel)
	{
		 cdebug << std::endl << "[Channel " << nChannel << ": "; DumpChannelBuffer(buffer[nChannel]); cdebug << "]";
	}
}

void DumpPartitionedBuffer(const PartitionedBuffer& buffer)
{
	cdebug << "PartitionedBuffer: "  ;
	for (int nPartition = 0; nPartition < buffer.size(); ++nPartition)
	{
		cdebug << "{Partition " << nPartition << ": " ; DumpSampleBuffer(buffer[nPartition]); cdebug << "}" << std::endl; 

	}
}
#endif

