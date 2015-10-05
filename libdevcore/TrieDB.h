/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file TrieDB.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#pragma once

#include <memory>
#include "db.h"
#include "Common.h"
#include "Log.h"
#include "Exceptions.h"
#include "SHA3.h"
#include "MemoryDB.h"
#include "TrieCommon.h"

namespace dev
{

struct TrieDBChannel: public LogChannel  { static const char* name(); static const int verbosity = 17; };
#define tdebug clog(TrieDBChannel)

struct InvalidTrie: virtual dev::Exception {};
extern const h256 c_shaNull;
extern const h256 EmptyTrie;

enum class Verification {
	Skip,
	Normal
};

/**
 * @brief Merkle Patricia Tree "Trie": a modifed base-16 Radix tree.
 * This version uses a database backend.
 * Usage:
 * @code
 * GenericTrieDB<MyDB> t(&myDB);
 * assert(t.isNull());
 * t.init();
 * assert(t.isEmpty());
 * t.insert(x, y);
 * assert(t.at(x) == y.toString());
 * t.remove(x);
 * assert(t.isEmpty());
 * @endcode
 */
template <class _DB>
class GenericTrieDB
{
public:
	using DB = _DB;

	explicit GenericTrieDB(DB* _db = nullptr): m_db(_db) {}
	GenericTrieDB(DB* _db, h256 const& _root, Verification _v = Verification::Normal) { open(_db, _root, _v); }
	~GenericTrieDB() {}

	void open(DB* _db) { m_db = _db; }
	void open(DB* _db, h256 const& _root, Verification _v = Verification::Normal) { m_db = _db; setRoot(_root, _v); }

	void init() {}

	void setRoot(h256 const& _root, Verification _v = Verification::Normal)
	{
		(void)_root;
		(void)_v;
	}

	/// True if the trie is uninitialised (i.e. that the DB doesn't contain the root node).
	bool isNull() const { return true; }
	/// True if the trie is initialised but empty (i.e. that the DB contains the root node which is empty).
	bool isEmpty() const {return true; }

	h256 const& root() const {return m_root; }

	std::string at(bytes const& _key) const { return at(&_key); }
	std::string at(bytesConstRef _key) const;
	void insert(bytes const& _key, bytes const& _value) { insert(&_key, &_value); }
	void insert(bytesConstRef _key, bytes const& _value) { insert(_key, &_value); }
	void insert(bytes const& _key, bytesConstRef _value) { insert(&_key, _value); }
	void insert(bytesConstRef _key, bytesConstRef _value);
	void remove(bytes const& _key) { remove(&_key); }
	void remove(bytesConstRef _key);
	bool contains(bytes const& _key) { return contains(&_key); }
	bool contains(bytesConstRef _key) { return !at(_key).empty(); }

	class iterator
	{
	public:
		using value_type = std::pair<bytesConstRef, bytesConstRef>;

		iterator() {}
		explicit iterator(GenericTrieDB const* _db);
		iterator(GenericTrieDB const* _db, bytesConstRef _key);

		iterator& operator++() { next(); return *this; }

		value_type operator*() const { return at(); }
		value_type operator->() const { return at(); }

		bool operator==(iterator const& _c) const { return _c.m_trail == m_trail; }
		bool operator!=(iterator const& _c) const { return _c.m_trail != m_trail; }

		value_type at() const;

	private:
		void next();
		void next(NibbleSlice _key);

		struct Node
		{
			std::string rlp;
			std::string key;		// as hexPrefixEncoding.
			byte child;				// 255 -> entering, 16 -> actually at the node, 17 -> exiting, 0-15 -> actual children.

			// 255 -> 16 -> 0 -> 1 -> ... -> 15 -> 17

			void setChild(unsigned _i) { child = _i; }
			void setFirstChild() { child = 16; }
			void incrementChild() { child = child == 16 ? 0 : child == 15 ? 17 : (child + 1); }

			bool operator==(Node const& _c) const { return rlp == _c.rlp && key == _c.key && child == _c.child; }
			bool operator!=(Node const& _c) const { return !operator==(_c); }
		};

	protected:
		std::vector<Node> m_trail;
		GenericTrieDB<DB> const* m_that;
	};

	iterator begin() const { return iterator(this); }
	iterator end() const { return iterator(); }
	iterator lower_bound(bytesConstRef _key) const { return iterator(this, _key); }

	/// Used for debugging, scans the whole trie.
	h256Hash leftOvers(std::ostream* _out = nullptr) const
	{
		(void)_out;
		return h256Hash();
	}

	/// Used for debugging, scans the whole trie.
	void debugStructure(std::ostream& _out) const
	{
		leftOvers(&_out);
	}

	/// Used for debugging, scans the whole trie.
	/// @param _requireNoLeftOvers if true, requires that all keys are reachable.
	bool check(bool _requireNoLeftOvers) const
	{
		try
		{
			return leftOvers().empty() || !_requireNoLeftOvers;
		}
		catch (...)
		{
			cwarn << boost::current_exception_diagnostic_information();
			return false;
		}
	}

	/// Get the underlying database.
	/// @warning This can be used to bypass the trie code. Don't use these unless you *really*
	/// know what you're doing.
	DB const* db() const { return m_db; }
	DB* db() { return m_db; }

private:

	std::string deref(RLP const& _n) const;
	std::string node(h256 const& _h) const { return m_db->lookup(_h); }

	h256 m_root;
	DB* m_db = nullptr;
};

template <class DB>
std::ostream& operator<<(std::ostream& _out, GenericTrieDB<DB> const& _db)
{
	for (auto const& i: _db)
		_out << escaped(i.first.toString(), false) << ": " << escaped(i.second.toString(), false) << std::endl;
	return _out;
}

/**
 * Different view on a GenericTrieDB that can use different key types.
 */
template <class Generic, class _KeyType>
class SpecificTrieDB: public Generic
{
public:
	using DB = typename Generic::DB;
	using KeyType = _KeyType;

	SpecificTrieDB(DB* _db = nullptr): Generic(_db) {}
	SpecificTrieDB(DB* _db, h256 _root, Verification _v = Verification::Normal): Generic(_db, _root, _v) {}

	std::string operator[](KeyType _k) const { return at(_k); }

	bool contains(KeyType _k) const { return Generic::contains(bytesConstRef((byte const*)&_k, sizeof(KeyType))); }
	std::string at(KeyType _k) const { return Generic::at(bytesConstRef((byte const*)&_k, sizeof(KeyType))); }
	void insert(KeyType _k, bytesConstRef _value) { Generic::insert(bytesConstRef((byte const*)&_k, sizeof(KeyType)), _value); }
	void insert(KeyType _k, bytes const& _value) { insert(_k, bytesConstRef(&_value)); }
	void remove(KeyType _k) { Generic::remove(bytesConstRef((byte const*)&_k, sizeof(KeyType))); }

	class iterator: public Generic::iterator
	{
	public:
		using Super = typename Generic::iterator;
		using value_type = std::pair<KeyType, bytesConstRef>;

		iterator() {}
		iterator(Generic const* _db): Super(_db) {}
		iterator(Generic const* _db, bytesConstRef _k): Super(_db, _k) {}

		value_type operator*() const { return at(); }
		value_type operator->() const { return at(); }

		value_type at() const;
	};

	iterator begin() const { return this; }
	iterator end() const { return iterator(); }

};

template <class Generic, class KeyType>
std::ostream& operator<<(std::ostream& _out, SpecificTrieDB<Generic, KeyType> const& _db)
{
	for (auto const& i: _db)
		_out << i.first << ": " << escaped(i.second.toString(), false) << std::endl;
	return _out;
}

template <class _DB>
class HashedGenericTrieDB: private SpecificTrieDB<GenericTrieDB<_DB>, h256>
{
	using Super = SpecificTrieDB<GenericTrieDB<_DB>, h256>;

public:
	using DB = _DB;

	HashedGenericTrieDB(DB* _db = nullptr): Super(_db) {}
	HashedGenericTrieDB(DB* _db, h256 _root, Verification _v = Verification::Normal): Super(_db, _root, _v) {}

	using Super::open;
	using Super::init;
	using Super::setRoot;

	/// True if the trie is uninitialised (i.e. that the DB doesn't contain the root node).
	using Super::isNull;
	/// True if the trie is initialised but empty (i.e. that the DB contains the root node which is empty).
	using Super::isEmpty;

	using Super::root;
	using Super::db;

	using Super::leftOvers;
	using Super::check;
	using Super::debugStructure;

	std::string at(bytesConstRef _key) const { return Super::at(sha3(_key)); }
	bool contains(bytesConstRef _key) { return Super::contains(sha3(_key)); }
	void insert(bytesConstRef _key, bytesConstRef _value) { Super::insert(sha3(_key), _value); }
	void remove(bytesConstRef _key) { Super::remove(sha3(_key)); }

	// empty from the PoV of the iterator interface; still need a basic iterator impl though.
	class iterator
	{
	public:
		using value_type = std::pair<bytesConstRef, bytesConstRef>;

		iterator() {}
		iterator(HashedGenericTrieDB const*) {}
		iterator(HashedGenericTrieDB const*, bytesConstRef) {}

		iterator& operator++() { return *this; }
		value_type operator*() const { return value_type(); }
		value_type operator->() const { return value_type(); }

		bool operator==(iterator const&) const { return true; }
		bool operator!=(iterator const&) const { return false; }

		value_type at() const { return value_type(); }
	};
	iterator begin() const { return iterator(); }
	iterator end() const { return iterator(); }
};

// Hashed & Hash-key mapping
template <class _DB>
class FatGenericTrieDB: private SpecificTrieDB<GenericTrieDB<_DB>, h256>
{
	using Super = SpecificTrieDB<GenericTrieDB<_DB>, h256>;

public:
	using DB = _DB;
	FatGenericTrieDB(DB* _db = nullptr): Super(_db) {}
	FatGenericTrieDB(DB* _db, h256 _root, Verification _v = Verification::Normal): Super(_db, _root, _v) {}

	using Super::init;
	using Super::isNull;
	using Super::isEmpty;
	using Super::root;
	using Super::leftOvers;
	using Super::check;
	using Super::open;
	using Super::setRoot;
	using Super::db;
	using Super::debugStructure;

	std::string at(bytesConstRef _key) const { return Super::at(sha3(_key)); }
	bool contains(bytesConstRef _key) { return Super::contains(sha3(_key)); }
	void insert(bytesConstRef _key, bytesConstRef _value)
	{
		h256 hash = sha3(_key);
		Super::insert(hash, _value);
		Super::db()->insertAux(hash, _key);
	}

	void remove(bytesConstRef _key) { Super::remove(sha3(_key)); }

	class iterator : public GenericTrieDB<_DB>::iterator
	{
	public:
		using Super = typename GenericTrieDB<_DB>::iterator;

		iterator() { }
		iterator(FatGenericTrieDB const* _trie): Super(_trie) { }

		typename Super::value_type at() const
		{
			//Super::value_type()
			auto hashed = Super::at();
			m_key = static_cast<FatGenericTrieDB const*>(Super::m_that)->db()->lookupAux(h256(hashed.first));
			return std::make_pair(&m_key, std::move(hashed.second));
		}

	private:
		mutable bytes m_key;
	};
	iterator begin() const { return iterator(); }
	iterator end() const { return iterator(); }
};

template <class KeyType, class DB> using TrieDB = SpecificTrieDB<GenericTrieDB<DB>, KeyType>;

}

// Template implementations...
namespace dev
{

//template <class DB> std::string GenericTrieDB<DB>::at(bytesConstRef _key) const
//{
//(void)_key;
//	return std::string();
//}
template <class DB> GenericTrieDB<DB>::iterator::iterator(GenericTrieDB const* _db)
{
	m_that = _db;
	m_trail.push_back({_db->node(_db->m_root), std::string(1, '\0'), 255});	// one null byte is the HPE for the empty key.
	next();
}

template <class DB> GenericTrieDB<DB>::iterator::iterator(GenericTrieDB const* _db, bytesConstRef _fullKey)
{
	m_that = _db;
	m_trail.push_back({_db->node(_db->m_root), std::string(1, '\0'), 255});	// one null byte is the HPE for the empty key.
	next(_fullKey);
}

template <class DB> typename GenericTrieDB<DB>::iterator::value_type GenericTrieDB<DB>::iterator::at() const
{
	assert(m_trail.size());
	Node const& b = m_trail.back();
	assert(b.key.size());
	assert(!(b.key[0] & 0x10));	// should be an integer number of bytes (i.e. not an odd number of nibbles).

	RLP rlp(b.rlp);
	return std::make_pair(bytesConstRef(b.key).cropped(1), rlp[rlp.itemCount() == 2 ? 1 : 16].payload());
}

template <class KeyType, class DB> typename SpecificTrieDB<KeyType, DB>::iterator::value_type SpecificTrieDB<KeyType, DB>::iterator::at() const
{
	auto p = Super::at();
	value_type ret;
	assert(p.first.size() == sizeof(KeyType));
	memcpy(&ret.first, p.first.data(), sizeof(KeyType));
	ret.second = p.second;
	return ret;
}

template <class DB> std::string GenericTrieDB<DB>::deref(RLP const& _n) const
{
	return _n.isList() ? _n.data().toString() : node(_n.toHash<h256>());
}

template <class DB> void GenericTrieDB<DB>::insert(bytesConstRef _key, bytesConstRef _value)
{
(void)_key;(void)_value;
}

template <class DB> std::string GenericTrieDB<DB>::at(bytesConstRef _key) const
{
	(void)_key;
	return std::string();
}

template <class DB> void GenericTrieDB<DB>::remove(bytesConstRef _key)
{
(void)_key;
}

template <class DB> void GenericTrieDB<DB>::iterator::next(NibbleSlice _key)
{
(void)_key;
}

template <class DB> void GenericTrieDB<DB>::iterator::next()
{

}

}


