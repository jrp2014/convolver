// makeIR.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "Common\dxstdafx.h"
#include <fstream>
#include <ios>
#include <vector>
#if defined(DEBUG) | defined(_DEBUG)
#include "debugging\debugging.h"
#endif
#include "convolution\wavefile.h"
#include "convolution\waveformat.h"
#include "convolution\exception.h"


int _tmain(int argc, _TCHAR* argv[])
{

	HRESULT	hr = S_OK;

#if	defined(DEBUG) | defined(_DEBUG)
	debugstream.sink (apDebugSinkConsole::sOnly);
#endif


	if(argc != 3)
	{
		std::wcerr << "Usage: makeIR input.txt output.wav" << std::endl;
		return 1;
	}

	std::basic_string< _TCHAR > inputFilename (argv[1], _tcslen(argv[2]));
	std::ifstream  input(_wfopen(argv[1], L"r"));		// Hack for UNICODE

	if (input.fail() || !input.is_open())
	{
		std::wcerr << "Failed to open " << inputFilename << std::endl;
		return 1;
	}

#ifndef LIBSNDFILE
	CWaveFileHandle	output;
	UINT nSizeWrote = 0;
#endif

	int nSamples = 0;

#ifdef LIBSNDFILE
	SF_INFO sf_info;
	::ZeroMemory(&sf_info, sizeof(sf_info));
#else
	WAVEFORMATEXTENSIBLE Wave;
	ZeroMemory(&Wave, sizeof(Wave));
#endif

	try
	{
		input.exceptions(std::ios::badbit | std::ios::failbit | std::ios::eofbit | std::ios::badbit);

#ifndef LIBSNDFILE
		WORD wFormatTag = 0;
#endif

		std::string sFormatTag;
		input >> sFormatTag;

		if(sFormatTag == "PCM")
		{
#ifdef LIBSNDFILE
			sf_info.format = SF_FORMAT_WAV;
#else
			wFormatTag = Wave.Format.wFormatTag = WAVE_FORMAT_PCM;
#endif
		}
		else
		{
			if(sFormatTag == "IEEE")
			{
#ifdef LIBSNDFILE
				sf_info.format = SF_FORMAT_WAV;
#else
				wFormatTag = Wave.Format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
#endif
			}
#ifdef LIBSNDFILE
			else
			{
				std::cerr << "Unrecognised format: " << sFormatTag << ". Should be PCM or IEEE" << std::endl;
				return 1;
			}
#else
			else
			{
				if (sFormatTag == "ExtensiblePCM")
				{
					Wave.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
					Wave.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
					wFormatTag = WAVE_FORMAT_PCM;
				}
				else
				{
					if (sFormatTag == "ExtensibleIEEE")
					{
						Wave.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
						Wave.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
						wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
					}
					else
					{
						std::cerr << "Unrecognised format: " << sFormatTag << ". Should be PCM or IEEE or ExtensiblePCM or ExtensibleIEEE" << std::endl;
						return 1;
					}
				}
			}
#endif
		}
#if	defined(DEBUG) | defined(_DEBUG)
		cdebug << "sFormatTag " << sFormatTag << std::endl;
#endif

		// Get the Wave format
#ifdef LIBSNDFILE
		WORD wBitsPerSample=0;
		input >> wBitsPerSample;
		if (sFormatTag == "PCM")
		{
			switch (wBitsPerSample)
			{
			case 8:
				sf_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_S8;
				break;
			case 16:
				sf_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
				break;
			case 24:
				sf_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
				break;
			case 32:
				sf_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_32;
				break;
			default:
				std::cerr << "Unrecognised bits per sample: " << wBitsPerSample << ". Should be 8, 16, 24 or 32" << std::endl;
			}
		}
		else
		{
			if (sFormatTag == "IEEE")
			{
				switch (wBitsPerSample)
				{
				case 32:
					sf_info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
					break;
				case 64:
					sf_info.format = SF_FORMAT_WAV | SF_FORMAT_DOUBLE;
					break;
				default:
					std::cerr << "Unrecognised bits per sample: " << wBitsPerSample << ". Should be 32 or 64" << std::endl;
				}
			}
			else
			{
				std::cerr << "Unrecognised format tag: " << sFormatTag << ". Should be PCM or IEEE." << std::endl;
			}
		}

#else
		input >> Wave.Format.wBitsPerSample;
#if	defined(DEBUG) | defined(_DEBUG)
		cdebug << "wBitsPerSample " << Wave.Format.wBitsPerSample << std::endl;
#endif
		if (Wave.Samples.wValidBitsPerSample % 8 != 0)
		{
			std::cerr << "Bits per sample (" << Wave.Format.wBitsPerSample << ") must be a multiple of 8" << std::endl;
			return 1;
		}
		Wave.Samples.wValidBitsPerSample = Wave.Format.wBitsPerSample;
#endif

#ifdef LIBSNDFILE
		input >> sf_info.channels;
#if	defined(DEBUG) | defined(_DEBUG)
		cdebug << "nChannels " << sf_info.channels << std::endl;
#endif
#else
		input >> Wave.Format.nChannels;
#if	defined(DEBUG) | defined(_DEBUG)
		cdebug << "nChannels " << Wave.Format.nChannels << std::endl;
#endif
		Wave.Format.nBlockAlign =  Wave.Format.nChannels * Wave.Format.wBitsPerSample / 8;
#endif

#ifdef LIBSNDFILE
		input >> sf_info.samplerate;
#if	defined(DEBUG) | defined(_DEBUG)
		cdebug << "samplerate " << sf_info.samplerate << std::endl;
#endif
#else
		input >> Wave.Format.nSamplesPerSec;
#if	defined(DEBUG) | defined(_DEBUG)
		cdebug << "nSamplesPerSec " << Wave.Format.nSamplesPerSec << std::endl;
#endif
		Wave.Format.nAvgBytesPerSec= Wave.Format.nSamplesPerSec * Wave.Format.nBlockAlign;

		if(Wave.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			input >> Wave.Samples.wValidBitsPerSample;
			if(Wave.Samples.wValidBitsPerSample > Wave.Format.wBitsPerSample)
			{
				std::cerr << "Valid bits per sample (" << Wave.Samples.wValidBitsPerSample << ") must be <= " <<
					" bits per sample -- the container size (" << Wave.Format.wBitsPerSample << ")" << std::endl;
				return 1;
			}
			if (Wave.Samples.wValidBitsPerSample % 8 != 0)
			{
				std::cerr << "Valid bits per sample (" << Wave.Samples.wValidBitsPerSample << ") must be a multiple of 8" << std::endl;
				return 1;
			}
#if	defined(DEBUG) | defined(_DEBUG)
			cdebug << "wValidBitsPerSample " << Wave.Samples.wValidBitsPerSample << std::endl;
#endif
			input >> Wave.dwChannelMask;
#if	defined(DEBUG) | defined(_DEBUG)
			cdebug << "dwChannelMask " << std::hex << Wave.dwChannelMask << std::dec << std::endl;
#endif
		}
#endif

		// Open the output file

#ifdef LIBSNDFILE
		CWaveFileHandle output(argv[2], SFM_WRITE, &sf_info);
#else
#if	defined(DEBUG) | defined(_DEBUG)
		std::wcerr  << "Writing to " << std::basic_string< _TCHAR >(argv[2], _tcslen(argv[2])) << std::endl;
#endif
		hr = output->Open(argv[2], &Wave.Format, WAVEFILE_WRITE);
		if(FAILED(hr))
		{
			std::wcerr << "Failed to open output file " << std::basic_string< _TCHAR >(argv[2], _tcslen(argv[3])) << std::endl;
			return hr;
		}
#endif

		while (!input.eof())		// Will never be true as EOF will throw exception
		{
#ifdef LIBSNDFILE
			if (sFormatTag == "PCM")
			{
				std::vector<int> samples(sf_info.channels);
				for (int nChannel = 0; nChannel < sf_info.channels; ++nChannel)
				{
					input >> samples[nChannel];
					++nSamples;
				}
				if (output.write_int(&samples[0], sf_info.channels) != sf_info.channels)
				{
					std::cerr << "Failed to write " << sf_info.channels << " items at sample " << nSamples << std::endl;
				};
			}
			else
			{
				if(sFormatTag == "IEEE")
				{
					std::vector<float> samples(sf_info.channels);
					for (int nChannel = 0; nChannel < sf_info.channels; ++nChannel)
					{
						input >> samples[nChannel];
						++nSamples;
					}

					if (output.write_float(&samples[0], sf_info.channels) != sf_info.channels) // write 1 frame
					{
						std::cerr << "Failed to write a frame at sample " << nSamples << std::endl;
					}
				}
			}
#else
			switch(wFormatTag)
			{
			case WAVE_FORMAT_PCM:
				{
					switch (Wave.Format.wBitsPerSample)
					{
					case 8:
						{
							// Get the input sample (-128 .. 127)
							int sample;
							input >> sample;

							// Truncate if exceeded full scale.
							if (sample > 127)
							{
								std::cerr << "8-bit sample number " << nSamples <<": " << sample << "truncated to 127" << std:: endl;
								sample = 127;
							}
							if (sample < -128)
							{
								std::cerr << "8-bit sample number " << nSamples <<": " << sample << "truncated to -128" << std:: endl;
								sample = -128;
							}

							// Convert to 0..255 and write
							sample += 128;
							hr = output->Write(Wave.Format.wBitsPerSample/ 8, (BYTE *)&sample, &nSizeWrote);
							if (FAILED(hr))
							{
								std::cerr << "Failed to write 8-bit sample number " << nSamples << " (" << sample << ")" << std::endl;
								return hr;
							}
						}
						break;

					case 16:
						{
							// 16-bit sound is -32768..32767 with 0 == silence
							int sample;
							input >> sample;

							// Truncate if exceeded full scale.
							if (sample > 32767)
							{
								std::cerr << "16-bit sample number " << nSamples <<": " << sample << "truncated to 32767" << std:: endl;
								sample = 32767;
							}
							if (sample < -32768)
							{
								std::cerr << "16-bit sample number " << nSamples <<": " << sample << "truncated to -32768" << std:: endl;
								sample = -32768;
							}

							// Write to output buffer.
							hr = output->Write(Wave.Format.wBitsPerSample/ 8, (BYTE *)&sample, &nSizeWrote);
							if (FAILED(hr))
							{
								std::cerr << "Failed to write 16-bit sample number " << nSamples << " (" << sample << ")" << std::endl;
								return hr;
							}
						}
						break;

					case 24:
						{
							// Get the input sample
							int sample;
							input >> sample;

							const int iClip = 1 << (Wave.Samples.wValidBitsPerSample - 1);

							// Truncate if exceeded full scale.
							if (sample > (iClip - 1))
							{
								std::cerr << "24-bit sample number " << nSamples <<": " << sample << "truncated to " << iClip - 1 << std:: endl;
								sample = iClip - 1;
							}
							if (sample < -iClip)
							{
								std::cerr << "24-bit sample number " << nSamples <<": " << sample << "truncated to " << -iClip  << std:: endl;
								sample = -iClip;
							}

							// Write to output buffer.
							BYTE outputSample[3];
							outputSample[0] = sample & 0xFF;
							outputSample[1] = (sample >> 8) & 0xFF;
							outputSample[2] = (sample >> 16) & 0xFF;
							hr = output->Write(Wave.Format.wBitsPerSample/ 8, outputSample, &nSizeWrote);
							if (FAILED(hr))
							{
								std::cerr << "Failed to write 24-bit sample number " << nSamples << " (" << sample << ")" << std::endl;
								return hr;
							}
						}
						break;

					case 32:
						{
							// Get the input sample
							int sample;
							input >> sample;
							const int iClip = 1 << (Wave.Samples.wValidBitsPerSample - 1);

							// Truncate if exceeded full scale.
							if (sample > (iClip - 1))
							{
								std::cerr << "32-bit sample number " << nSamples <<": " << sample << "truncated to " << iClip - 1 << std:: endl;
								sample = iClip - 1;
							}
							if (sample < -iClip)
							{
								std::cerr << "32-bit sample number " << nSamples <<": " << sample << "truncated to " << -iClip  << std:: endl;
								sample = -iClip;
							}

							// Write output
							BYTE outputSample[4];
							outputSample[0] = sample & 0xFF;
							outputSample[1] = (sample >> 8) & 0xFF;
							outputSample[2] = (sample >> 16) & 0xFF;
							outputSample[3]	= (sample >> 24) & 0xFF;
							hr = output->Write(Wave.Format.wBitsPerSample / 8, outputSample, &nSizeWrote);
							if (FAILED(hr))
							{
								std::cerr << "Failed to write 32-bit sample number " << nSamples << " (" << sample << ")" << std::endl;
								return hr;
							}
						}
						break;

					default:
						{
							std::cerr << Wave.Format.wBitsPerSample << "-bit PCM is not supported" << std::endl;
							return E_FAIL;
						}
					}
				}
				break;
			case WAVE_FORMAT_IEEE_FLOAT:
				{
					switch(Wave.Format.wBitsPerSample)
					{
					case 32:
						{
							float sample = 0;
							input >> sample ;
							std::cerr << sample << std::endl;
							hr = output->Write(Wave.Format.wBitsPerSample / 8, (BYTE *)&sample, &nSizeWrote);
							if (FAILED(hr))
							{
								std::cerr << "Failed to write sample number " << nSamples << " (" << sample << ")" << std::endl;
								return hr;
							}
						}
						break;
					default:
						{
							std::cerr << Wave.Format.wBitsPerSample << "-bit IEEE float is not supported" << std::endl;
							return E_FAIL;
						}
					}
				}
				break;
			default:
				assert(false);
			} // format switch

			++nSamples;

			if(Wave.Format.wBitsPerSample / 8 != nSizeWrote)
			{
				std::cerr << "Only wrote " << nSizeWrote << " of " << Wave.Format.wBitsPerSample* 8 << "bytes for sample number " 
					<< nSamples  << std::endl;
				return 1;
			}
#endif
		}// while ! eof / good
	} // try

	catch(const convolutionException& error)
	{
			std::cerr << error.what() << std::endl;
			return 1;
	}
	catch(const std::ios_base::failure& error)
	{
		if(!input.eof())
		{
			std::cerr << "I/O exception: " << error.what() << std::endl;
			return 1;
		}
		// else fall through
	}
	catch(const std::exception& error)
	{
		std::cerr << "Standard exception: " << error.what() << std::endl;
		return 1;
	}
	catch (...)
	{
		std::cerr << "Failed." << std::endl;
		return 1;
	}

#ifdef LIBSNDFILE
#else
	std::cerr << waveFormatDescription(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(&Wave), nSamples, "Output format: ") << std::endl;

	if (nSamples % Wave.Format.nChannels != 0)
	{
		std::cerr << "The number of samples (" << nSamples << ") was not a multiple of the number of channels (" << Wave.Format.nChannels << 
			"). Invalid output file produced." << std::endl;
		return 1;
	}
#endif

#ifndef LIBSNDFILE
	output->Close();
#endif

	return hr;
}

