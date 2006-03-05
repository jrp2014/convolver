#pragma once
// Convolver: DSP plug-in for Windows Media Player that convolves an impulse respose
// filter it with the input stream.
//
// Copyright (C) 2005 John Pavel
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
// Based upon
// Alexandrescu, Andrei. "Modern C++ Design: Generic Programming and Design 
// Patterns Applied". Copyright (c) 2001. Addison-Wesley.

#include <map>
#include <boost/call_traits.hpp>

// Mapper is not really a Factory class because it owns the created Products
// and so maps between a Identifier and a Product, not a Product creator.
template<class AbstractProduct, typename IdentifierType>
class Mapper
{
private:

	typedef typename std::map<IdentifierType, AbstractProduct*> IdToProductMap;

	IdToProductMap association_;

	template<typename Product>
		Product* CreateObject()
	{
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
		if(!association_.insert(value_type(id, CreateObject<Product>())).second)
		{
			throw std::range_error("Internal error: Duplicate registeration");
		}
	}

	AbstractProduct* const & Map(const IdentifierType& id) const
	{
		const_iterator pos = association_.find(id);
		if(pos != association_.end())
		{
			return pos->second;
		}
		else
		{
			return NULL;
			//throw std::range_error("Internal error: Map");
		}
	}

	bool Unregister(const IdentifierType& id)
	{
		return association_.erase(id) == 1;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	const_iterator begin() const
	{
		return	association_.begin();
	}

	iterator begin()
	{
		return	association_.begin();
	}

	const_iterator end() const
	{
		return	association_.end();
	}

	iterator end()
	{
		return	association_.end();
	}


	virtual ~Mapper()
	{
		for(iterator pos = begin(); pos != end(); ++pos)
			delete pos->second;
	}
};

// A real set of factory classes, taking variable number of parameters

template<typename BaseClassType, typename ClassType>
BaseClassType CreateObject()
{
	return new ClassType();
}

template<typename BaseClassType, typename IdentifierType>
class ObjectFactory //<BaseClassType(), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)();
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, ClassType>;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)();
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		association_.erase(association_.begin(), association_.end());
	}

protected:
	IdToProductMap association_;
};


template<typename BaseClassType, typename A0, typename ClassType>
BaseClassType CreateObject( A0 a0 )
{
	return new ClassType ( a0 );
}

template<typename BaseClassType, typename A0, typename IdentifierType>
class ObjectFactory<BaseClassType ( A0 ), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)( A0 a0 );
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, A0, ClassType>;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id, A0 a0 ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)( a0 );
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		association_.erase(association_.begin(), association_.end());
	}

protected:
	IdToProductMap association_;
};


template<typename BaseClassType, typename A0, typename A1, typename ClassType>
BaseClassType CreateObject( A0 a0, A1 a1 )
{
	return new ClassType ( a0, a1 );
}

template<typename BaseClassType, typename A0, typename A1, typename IdentifierType>
class ObjectFactory<BaseClassType ( A0, A1 ), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)( A0 a0, A1 a1 );
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, A0, A1, ClassType>;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id, A0 a0, A1 a1 ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)( a0, a1 );
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		association_.erase(association_.begin(), association_.end());
	}

protected:
	IdToProductMap association_;
};


template<typename BaseClassType, typename A0, typename A1, typename A2, typename ClassType>
BaseClassType CreateObject( A0 a0, A1 a1, A2 a2 )
{
	return new ClassType ( a0, a1, a2 );
}

template<typename BaseClassType, typename A0, typename A1, typename A2, typename IdentifierType>
class ObjectFactory<BaseClassType ( A0, A1, A2 ), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)( A0 a0, A1 a1, A2 a2 );
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, A0, A1, A2, ClassType>;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id, A0 a0, A1 a1, A2 a2 ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)( a0, a1, a2 );
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		association_.erase(association_.begin(), association_.end());
	}

protected:
	IdToProductMap association_;
};


template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename ClassType>
BaseClassType CreateObject( A0 a0, A1 a1, A2 a2, A3 a3 )
{
	return new ClassType ( a0, a1, a2, a3 );
}

template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename IdentifierType>
class ObjectFactory<BaseClassType ( A0, A1, A2, A3 ), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)( A0 a0, A1 a1, A2 a2, A3 a3 );
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, A0, A1, A2, A3, ClassType>;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id, A0 a0, A1 a1, A2 a2, A3 a3 ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)( a0, a1, a2, a3 );
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		association_.erase(association_.begin(), association_.end());
	}

protected:
	IdToProductMap association_;
};


template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename ClassType>
BaseClassType CreateObject( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4 )
{
	return new ClassType ( a0, a1, a2, a3, a4 );
}

template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename IdentifierType>
class ObjectFactory<BaseClassType ( A0, A1, A2, A3, A4 ), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4 );
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, A0, A1, A2, A3, A4, ClassType>;
	}


	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id, A0 a0, A1 a1, A2 a2, A3 a3, A4 a4 ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)( a0, a1, a2, a3, a4 );
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		association_.erase(association_.begin(), association_.end());
	}

protected:
	IdToProductMap association_;
};


template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename ClassType>
BaseClassType CreateObject( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5 )
{
	return new ClassType ( a0, a1, a2, a3, a4, a5 );
}

template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename IdentifierType>
class ObjectFactory<BaseClassType ( A0, A1, A2, A3, A4, A5 ), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5 );
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, A0, A1, A2, A3, A4, A5, ClassType>;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id, A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5 ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)( a0, a1, a2, a3, a4, a5 );
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		association_.erase(association_.begin(), association_.end());
	}

protected:
	IdToProductMap association_;
};


template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename ClassType>
BaseClassType CreateObject( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6 )
{
	return new ClassType ( a0, a1, a2, a3, a4, a5, a6 );
}

template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename IdentifierType>
class ObjectFactory<BaseClassType ( A0, A1, A2, A3, A4, A5, A6 ), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6 );
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, A0, A1, A2, A3, A4, A5, A6, ClassType>;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id, A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6 ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)( a0, a1, a2, a3, a4, a5, a6 );
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		association_.erase(association_.begin(), association_.end());
	}

protected:
	IdToProductMap association_;
};


template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename ClassType>
BaseClassType CreateObject( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7 )
{
	return new ClassType ( a0, a1, a2, a3, a4, a5, a6, a7 );
}

template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename IdentifierType>
class ObjectFactory<BaseClassType ( A0, A1, A2, A3, A4, A5, A6, A7 ), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7 );
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, A0, A1, A2, A3, A4, A5, A6, A7, ClassType>;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id, A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7 ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)( a0, a1, a2, a3, a4, a5, a6, a7 );
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		association_.erase(association_.begin(), association_.end());
	}

protected:
	IdToProductMap association_;
};


template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename ClassType>
BaseClassType CreateObject( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8 )
{
	return new ClassType ( a0, a1, a2, a3, a4, a5, a6, a7, a8 );
}

template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename IdentifierType>
class ObjectFactory<BaseClassType ( A0, A1, A2, A3, A4, A5, A6, A7, A8 ), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8 );
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, A0, A1, A2, A3, A4, A5, A6, A7, A8, ClassType>;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id, A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8 ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)( a0, a1, a2, a3, a4, a5, a6, a7, a8 );
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		association_.erase(association_.begin(), association_.end());
	}

protected:
	IdToProductMap association_;
};


template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename ClassType>
BaseClassType CreateObject( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9 )
{
	return new ClassType ( a0, a1, a2, a3, a4, a5, a6, a7, a8, a9 );
}

template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename IdentifierType>
class ObjectFactory<BaseClassType ( A0, A1, A2, A3, A4, A5, A6, A7, A8, A9 ), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9 );
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, ClassType>;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id, A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9 ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)( a0, a1, a2, a3, a4, a5, a6, a7, a8, a9 );
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		association_.erase(association_.begin(), association_.end());
	}

protected:
	IdToProductMap association_;
};


template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename ClassType>
BaseClassType CreateObject( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10 )
{
	return new ClassType ( a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10 );
}

template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename IdentifierType>
class ObjectFactory<BaseClassType ( A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10 ), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10 );
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, ClassType>;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id, A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10 ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)( a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10 );
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		association_.erase(association_.begin(), association_.end());
	}

protected:
	IdToProductMap association_;
};


template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename ClassType>
BaseClassType CreateObject( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10, A11 a11 )
{
	return new ClassType ( a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11 );
}

template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename IdentifierType>
class ObjectFactory<BaseClassType ( A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11 ), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10, A11 a11 );
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, ClassType>;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id, A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10, A11 a11 ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)( a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11 );
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		association_.erase(association_.begin(), association_.end());
	}

protected:
	IdToProductMap association_;
};


template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename A12, typename ClassType>
BaseClassType CreateObject( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10, A11 a11, A12 a12 )
{
	return new ClassType ( a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12 );
}

template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename A12, typename IdentifierType>
class ObjectFactory<BaseClassType ( A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12 ), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10, A11 a11, A12 a12 );
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, ClassType>;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id, A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10, A11 a11, A12 a12 ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)( a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12 );
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		association_.erase(association_.begin(), association_.end());
	}

protected:
	IdToProductMap association_;
};


template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename A12, typename A13, typename ClassType>
BaseClassType CreateObject( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10, A11 a11, A12 a12, A13 a13 )
{
	return new ClassType ( a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13 );
}

template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename A12, typename A13, typename IdentifierType>
class ObjectFactory<BaseClassType ( A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13 ), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10, A11 a11, A12 a12, A13 a13 );
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13, ClassType>;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id, A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10, A11 a11, A12 a12, A13 a13 ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)( a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13 );
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		for(iterator i=association_.begin(); i != association_.end(); ++i)
			delete i->second;

		association_.erase(association_.begin(), association_.end());
	}

protected:
	IdToProductMap association_;
};


template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename A12, typename A13, typename A14, typename ClassType>
BaseClassType CreateObject( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10, A11 a11, A12 a12, A13 a13, A14 a14 )
{
	return new ClassType ( a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14 );
}

template<typename BaseClassType, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename A12, typename A13, typename A14, typename IdentifierType>
class ObjectFactory<BaseClassType ( A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13, A14 ), IdentifierType>
{
private:
	typedef BaseClassType (*CreateObjectFunc)( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10, A11 a11, A12 a12, A13 a13, A14 a14 );
	typedef typename std::map<IdentifierType, CreateObjectFunc> IdToProductMap;

public:
	typedef typename IdToProductMap::const_iterator const_iterator;
	typedef typename IdToProductMap::iterator iterator;
	typedef typename IdToProductMap::size_type size_type;
	typedef typename IdToProductMap::value_type value_type;

	template<typename ClassType> 
		void Register(IdentifierType id)
	{
		if(Registered(id)) throw std::range_error("Internal error: Duplicate registeration");
		association_[id] = &CreateObject<BaseClassType, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13, A14, ClassType>;
	}

	bool Registered(const IdentifierType& id) const
	{
		return association_.find(id) != association_.end();
	}

	bool Unregister(IdentifierType id)
	{
		return (association_.erase(id) == 1);
	}

	BaseClassType Create(IdentifierType id, A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10, A11 a11, A12 a12, A13 a13, A14 a14 ) const
	{
		const_iterator iter = association_.find(id);
		if (iter == association_.end()) return NULL;
		return ((*iter).second)( a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14 );
	}

	const_iterator begin() const
	{
		return association_.begin();
	}

	iterator begin()
	{
		return association_.begin();
	}

	const_iterator end() const
	{
		return association_.end();
	}

	iterator end()
	{
		return association_.end();
	}

	size_type size() const
	{
		return association_.size();
	}

	~ObjectFactory()
	{
		association_.erase(association_.begin(), association_.end());
	}

protected: 
	IdToProductMap association_;
};


