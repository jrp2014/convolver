// version.h

#pragma once
#pragma warning(disable : 4505 4710)
#pragma comment(lib, "version.lib")

#include <string>
#include <sstream>
#include <iomanip>
#include <exception>
#include <new>
#include <windows.h>

class version
{
public:

	version(std::wstring app_name)
	{
		// Get version info
		DWORD h = 0;

		DWORD resource_size = ::GetFileVersionInfoSize(const_cast<wchar_t*>(app_name.c_str()), &h);
		if(resource_size)
		{
			resource_data_ = new unsigned char[resource_size];
			if(resource_data_)
			{
				if(::GetFileVersionInfo(const_cast<wchar_t*>(app_name.c_str()),
					0,
					resource_size,
					static_cast<LPVOID>(resource_data_)) != FALSE)
				{
					UINT size = 0;

					// Get language information
					if(::VerQueryValue(static_cast<LPVOID>(resource_data_),
						TEXT("\\VarFileInfo\\Translation"),
						reinterpret_cast<LPVOID*>(&language_information_),
						&size) == FALSE)
						throw versionException("Requested localized version information not available");
				}
				else
				{
					std::stringstream exception;
					exception << "Could not get version information (Windows error: " << ::GetLastError() << ")";
					throw versionException(exception.str().c_str());
				}
			}
			else
				throw std::bad_alloc();
		}
		else
		{
			std::stringstream exception;
			exception << "No version information found (Windows error: " << ::GetLastError() << ")";
			throw versionException(exception.str().c_str());
		}
	}

	version()
	{
		// Get application name
		TCHAR buf[MAXSTRING] = TEXT("");

		if(::GetModuleFileName(0, buf, MAXSTRING))
		{
			std::wstring app_name = buf;
			app_name = app_name.substr(app_name.rfind(TEXT("\\")) + 1);

			version::version(app_name);
		}
		else
			throw versionException("Could not get application name");
	}

	~version() { delete [] resource_data_; }
	std::wstring get_product_name() const { return get_value(TEXT("ProductName")); }
	std::wstring get_internal_name() const { return get_value(TEXT("InternalName")); }
	std::wstring get_product_version() const { return get_value(TEXT("ProductVersion")); }
	std::wstring get_special_build() const { return get_value(TEXT("SpecialBuild")); }
	std::wstring get_private_build() const { return get_value(TEXT("PrivateBuild")); }
	std::wstring get_copyright() const { return get_value(TEXT("LegalCopyright")); }
	std::wstring get_trademarks() const { return get_value(TEXT("LegalTrademarks")); }
	std::wstring get_comments() const { return get_value(TEXT("Comments")); }
	std::wstring get_company_name() const { return get_value(TEXT("CompanyName")); }
	std::wstring get_file_version() const { return get_value(TEXT("FileVersion")); }
	std::wstring get_file_description() const { return get_value(TEXT("FileDescription")); }

private:

	struct language
	{
		WORD language_;
		WORD code_page_;

		language()
		{
			language_  = 0;
			code_page_ = 0;
		}
	};

	unsigned char	*resource_data_;
	language		*language_information_;

	std::wstring get_value(const std::wstring &key) const
	{
		if(resource_data_)
		{
			UINT              size   = 0;
			std::wstringstream t;
			LPVOID            value  = 0;

			// Build query string
			t << TEXT("\\StringFileInfo\\");

			// The following hack is required to work around int->unicode hex conversion limitations
			for (int i=2*sizeof(WORD) - 1; i>=0; --i) {
				t << "0123456789ABCDEF"[((language_information_->language_ >> i*4) & 0xF)];
			}

			for (int i=2*sizeof(WORD) - 1; i>=0; --i) {
				t << "0123456789ABCDEF"[((language_information_->code_page_ >> i*4) & 0xF)];
			}		

			t << TEXT("\\") << key;

			if(::VerQueryValue(static_cast<LPVOID>(resource_data_),
				const_cast<LPTSTR>(t.str().c_str()),
				static_cast<LPVOID*>(&value),
				&size) != FALSE)
				return static_cast<LPTSTR>(value);
		}

		return TEXT("Unknown version");
	}
};
