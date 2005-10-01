
#include "channelpaths.h"

ChannelPaths::ChannelPaths(TCHAR szConfigFileName[MAX_PATH], const int& nChannels, const int& nPartitions) :
nPartitions(nPartitions),
nChannels(nChannels)
{

	USES_CONVERSION;
	std::ifstream  config(T2A(szConfigFileName));

	if (config == NULL)
	{
		throw TEXT("Failed to open config file");
	}

	try
	{
		config.exceptions(std::ios::badbit | std::ios::failbit | std::ios::eofbit | std::ios::badbit);

		while(true)		// will throw eof exception when all paths read
		{
			std::string FilterFilename;
			config >> FilterFilename;

			std::vector<ChannelPath::ScaledChannel> inChannel;
			while(true)
			{
				// Require at least one input channel
				float scale;
				config >> scale;

				if(scale < 0)
					throw TEXT("Negative input channel number. Invalid");

				// The integer part designates the input channel
				int channel = floor(scale);

				if(channel > nChannels - 1)
					throw TEXT("Invalid input Channel number");

				// The fractional part is the scaling factor to be applied
				scale -= channel;
				// 0 implies no scaling to be applied (ie, scale = 1)
				if(scale == 0)
					scale = 1.0f;

				inChannel.push_back(ChannelPath::ScaledChannel(channel, scale));

				int nextchar = config.peek();
				if (nextchar == '\n' || nextchar == EOF)
					break;
			}

			// Get the output channels for this filter
			std::vector<ChannelPath::ScaledChannel> outChannel;
			while(true)
			{


				float scale;
				config >> scale;

				if(scale < 0)
					throw TEXT("Negative output channel number. Invalid");

				// The integer part designates the output channel
				int channel = floor(scale);

				if(channel > nChannels - 1)
					throw TEXT("Invalid output Channel number");

				// The fractional part is the scaling factor to be applied
				scale -= channel;
				// 0 implies no scaling to be applied (ie, scale = 1)
				if(scale == 0)
					scale = 1.0f;

				outChannel.push_back(ChannelPath::ScaledChannel(channel, scale));

				int nextchar = config.peek();
				if (nextchar == '\n' || nextchar == EOF)
					break;
			}

			Paths.push_back(ChannelPath(CA2CT(FilterFilename.c_str()), nChannels, nPartitions, inChannel, outChannel));


		}
	}
	catch(const std::ios_base::failure& error)
	{
		if(config.eof())
		{
			// Verify
			if (Paths.size() > 0)
			{
				nPartitionLength = Paths[0].filter.nPartitionLength;			// in frames (a frame contains the interleaved samples for each channel)
				nHalfPartitionLength = Paths[0].filter.nHalfPartitionLength;	// in frames
				nFilterLength = Paths[0].filter.nFilterLength;					// nFilterLength = nPartitions * nPartitionLength
			}
			else
			{
				throw "Must specify at least one filter";
			}

			for(int i = 0; i < Paths.size(); ++i)
			{
				if(Paths[i].filter.nChannels != 1)
				{
					throw TEXT("Filters must have one channel");
				}

				if(Paths[i].filter.nFilterLength != nFilterLength)
				{
					throw TEXT("Filters must all be of the same length");
				}
			}
#if defined(DEBUG) | defined(_DEBUG)
			Dump();
#endif
		}
		else
		{
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << "I/O exception: " << error.what() << std::endl;
#endif
			throw;
		}
		// else fall through
	}
	catch(const std::exception& error)
	{
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << "Standard exception: " << error.what() << std::endl;
#endif
		throw;
	}
	catch (const char* what)
	{
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << "Failed: " << what << std::endl;
#endif
		throw;
	}
	catch (const TCHAR* what)
	{
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << "Failed: " << what << std::endl;
#endif
		throw;
	}
	catch (...)
	{
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << "Failed." << std::endl;
#endif
		throw;
	}
}

#if defined(DEBUG) | defined(_DEBUG)
void ChannelPaths::Dump()
{
	cdebug << "ChannelPaths: " << " nChannels:" << nChannels << " nPartitions:" << nPartitions << " nPartitionLength:" << nPartitionLength <<
		" nHalfPartitionLength:" << nHalfPartitionLength << " nFilterLength:" << nFilterLength << std::endl;
	for(int i = 0; i < Paths.size(); ++i) 
		Paths[i].Dump();
}

void ChannelPaths::ChannelPath::Dump()
{
	cdebug << "inChannel:";
	for(int i=0; i<inChannel.size(); ++i)
	{
		cdebug << " "; inChannel[i].Dump();
	}

	cdebug << std::endl << "outChannel:";
	for(int i=0; i<outChannel.size(); ++i)
	{
		cdebug << " "; outChannel[i].Dump();
	}
	cdebug << std::endl;
}

void ChannelPaths::ChannelPath::ScaledChannel::Dump()
{
	cdebug << nChannel << "/" << fScale;
}

#endif
