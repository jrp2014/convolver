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
//
// Based upon
// Alexandrescu, Andrei. "Modern C++ Design: Generic Programming and Design 
//     Patterns Applied". Copyright (c) 2001. Addison-Wesley.

#include <map>


  


template<class AbstractProduct, typename IdentifierType>
class Factory
{
private:

	typedef std::map<IdentifierType, AbstractProduct*> IdToProductMap;
	IdToProductMap associations_;

	template<typename Product>
	Product* CreateObject()
	{                                                                                                        \
		return new Product();
	}      

public:

	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename Product>
	void Register(const IdentifierType& id)
	{
		if(!associations_.insert(value_type(id, CreateObject<Product>())).second)
		{
			throw std::range_error("Internal error: Register");
		}
	}

	AbstractProduct* const & Map(const IdentifierType& id) const
	{
		const_iterator pos = associations_.find(id);
		if(pos != associations_.end())
		{
			return pos->second;
		}
		else
		{
			return NULL;
			//throw std::range_error("Internal error: Map");
		}
	}

	//bool Unregister(const IdentifierType& id)
	//{
	//	return associations_.erase(id) == 1;
	//}

	bool Registered(const IdentifierType& id) const
	{
		return associations_.find(id) != associations_.end();
	}

	size_type size() const
	{
		return associations_.size();
	}

	const_iterator begin() const
	{
		return	associations_.begin();
	}

	iterator begin()
	{
		return	associations_.begin();
	}

	const_iterator end() const
	{
		return	associations_.end();
	}

	iterator end()
	{
		return	associations_.end();
	}


	virtual ~Factory()
	{
		for(iterator pos = begin(); pos != end(); ++pos)
			delete pos->second;
	}
};

