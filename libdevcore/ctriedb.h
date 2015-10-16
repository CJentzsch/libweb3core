#pragma once

#include <memory>
#include "db.h"
#include "Common.h"
#include "Log.h"
#include "Exceptions.h"
#include "SHA3.h"
#include "MemoryDB.h"
#include "TrieCommon.h"
#include <libdevcrypto/OverlayDB.h>

#include <libdevcore/TrieDB.h>
using namespace std;
using namespace dev;

namespace dev
{

class CMemTrieNode
{
public:
	CMemTrieNode() {}
	virtual ~CMemTrieNode() {}

	virtual std::string const& at(bytesConstRef _key) const = 0;
	virtual CMemTrieNode* insert(bytesConstRef _key, std::string const& _value) = 0;
	virtual CMemTrieNode* remove(bytesConstRef _key) = 0;
	void putRLP(RLPStream& _parentStream) const;

#if ENABLE_DEBUG_PRINT
	void debugPrint(std::string const& _indent = "") const { std::cerr << std::hex << hash256() << ":" << std::dec << std::endl; debugPrintBody(_indent); }
#endif

	/// 256-bit hash of the node - this is a SHA-3/256 hash of the RLP of the node.
	h256 hash256() const { RLPStream s; makeRLP(s); return dev::sha3(s.out()); }
	bytes rlp() const { RLPStream s; makeRLP(s); return s.out(); }
	void mark() { m_hash256 = h256(); }

protected:
	virtual void makeRLP(RLPStream& _intoStream) const = 0;

#if ENABLE_DEBUG_PRINT
	virtual void debugPrintBody(std::string const& _indent = "") const = 0;
#endif

	static CMemTrieNode* newBranch(bytesConstRef _k1, std::string const& _v1, bytesConstRef _k2, std::string const& _v2);

private:
	mutable h256 m_hash256;
};

static const std::string c_nullString;


/**
 * @brief Merkle Patricia Tree "Trie": a modifed base-16 Radix tree.
 */
template <class _KeyType, class _DB>
class CMemTrie: public _DB
{
public:
	explicit CMemTrie(_DB* _db = nullptr): m_root(nullptr), m_db(_db) {}
	CMemTrie(_DB* _db, h256 const& _root, Verification _v = Verification::Normal): m_rootHash(_root), m_db(_db) {(void)_v;}
	~CMemTrie() { delete m_root; }

	void open(_DB* _db) { m_db = _db; }
	void open(_DB* _db, h256 const& _root, Verification _v = Verification::Normal) { m_db = _db; setRoot(_root, _v); }

	void setRoot(h256 const& _root, Verification _v = Verification::Normal);


	/// True if the trie is uninitialised (i.e. that the DB doesn't contain the root node).
	bool isNull() const {return false;} // TODO return !node(m_rootHash).size(); }
	/// True if the trie is initialised but empty (i.e. that the DB contains the root node which is empty).
	bool isEmpty() const {return false;} //TODO return m_rootHash == c_shaNull && node(m_rootHash).size(); }

	void init() { m_rootHash = hash256(); } // TODO  insert(&RLPNull)

	h256 hash256() const { return m_root ? m_root->hash256() : sha3(dev::rlp(bytesConstRef())); }
	bytes rlp() const { return m_root ? m_root->rlp() : dev::rlp(bytesConstRef()); }

	void debugPrint();

	std::string operator[](_KeyType _k) const { return at(_k); }

	std::string const& at(std::string const& _key) const;
	std::string const& at(bytesConstRef const& _key) const
	{
		return at(toHex(_key));
	}
	std::string const& at(h256 const& _key)
	{
		return at(_key.hex());
	}

	std::string at(_KeyType _k) const { return at(bytesConstRef((byte const*)&_k, sizeof(_KeyType))); }


	void insert(std::string const& _key, std::string const& _value);
	void insert(bytesConstRef const& _key, bytesConstRef _value) { insert(asString(_key), asString(_value)); }
	void insert(_KeyType const& _key, std::string const& _value) { insert(bytesConstRef((byte const*)&_key, sizeof(_KeyType)), _value); }
	void insert(_KeyType const& _key, bytesConstRef _value) { insert(bytesConstRef((byte const*)&_key, sizeof(_KeyType)), _value); }
	void insert(_KeyType const& _key, bytes const& _value) { insert(bytesConstRef((byte const*)&_key, sizeof(_KeyType)), bytesConstRef(&_value)); }


	void remove(std::string const& _key);
	void remove(h256 const& _key) { remove(_key.hex()); }
	void remove(h160 const& _key) { remove(_key.hex()); }
	void remove(bytes const& _key) { remove(asString(_key)); }
	void remove(bytesConstRef _key) { remove(asString(_key)); }

	// compability functions
	h256 const& root() const { return m_rootHash; }

	_DB const* db() const { return m_db; }
	_DB* db() { return m_db; }

	h256Hash leftOvers(std::ostream* _out = nullptr) const {(void)_out; return h256Hash();}
	void debugStructure(std::ostream& _out) const {(void)_out;}

	//iterator //TODO

	class iterator
	{
	public:
		using value_type = std::pair<_KeyType, bytesConstRef>;

		iterator() {}
		//explicit iterator(GenericTrieDB const* _db);
		iterator(CMemTrie const* _db, bytesConstRef _key);

		iterator& operator++() { /*next();*/ return *this; }

		value_type operator*() const { return at(); }
		value_type operator->() const { return at(); }

		bool operator==(iterator const& _c) const {(void)_c; return true;}// _c.m_trail == m_trail; }
		bool operator!=(iterator const& _c) const {(void)_c; return true;}//_c.m_trail != m_trail; }

		value_type at() const {return value_type();}
	};

	iterator begin() const { return iterator(); }
	iterator end() const { return iterator(); }

	iterator lower_bound(_KeyType _k) const { return iterator(this, bytesConstRef((byte const*)&_k, sizeof(_KeyType))); }


private:
	CMemTrieNode* m_root;
	h256 m_rootHash;
	_DB* m_db = nullptr;
};


#define ENABLE_DEBUG_PRINT 0

/*/
#define APPEND_CHILD appendData
/*/
#define APPEND_CHILD appendRaw
/**/


class CTrieExtNode: public CMemTrieNode
{
public:
	CTrieExtNode(bytesConstRef _bytes): m_ext(_bytes.begin(), _bytes.end()) {}

	bytes m_ext;
};

class CTrieBranchNode: public CMemTrieNode
{
public:
	CTrieBranchNode(std::string const& _value): m_value(_value)
	{
		memset(m_nodes.data(), 0, sizeof(CMemTrieNode*) * 16);
	}

	CTrieBranchNode(byte _i1, CMemTrieNode* _n1, std::string const& _value = std::string()): m_value(_value)
	{
		memset(m_nodes.data(), 0, sizeof(CMemTrieNode*) * 16);
		m_nodes[_i1] = _n1;
	}

	CTrieBranchNode(byte _i1, CMemTrieNode* _n1, byte _i2, CMemTrieNode* _n2)
	{
		memset(m_nodes.data(), 0, sizeof(CMemTrieNode*) * 16);
		m_nodes[_i1] = _n1;
		m_nodes[_i2] = _n2;
	}

	virtual ~CTrieBranchNode()
	{
		for (auto i: m_nodes)
			delete i;
	}

#if ENABLE_DEBUG_PRINT
	virtual void debugPrintBody(std::string const& _indent) const
	{

		if (m_value.size())
			std::cerr << _indent << "@: " << m_value << std::endl;
		for (auto i = 0; i < 16; ++i)
			if (m_nodes[i])
			{
				std::cerr << _indent << std::hex << i << ": " << std::dec;
				m_nodes[i]->debugPrint(_indent + "  ");
			}
	}
#endif

	virtual std::string const& at(bytesConstRef _key) const override;
	virtual CMemTrieNode* insert(bytesConstRef _key, std::string const& _value) override;
	virtual CMemTrieNode* remove(bytesConstRef _key) override;
	virtual void makeRLP(RLPStream& _parentStream) const override;

private:
	/// @returns (byte)-1 when no active branches, 16 when multiple active and the index of the active branch otherwise.
	byte activeBranch() const;

	CMemTrieNode* rejig();

	std::array<CMemTrieNode*, 16> m_nodes;
	std::string m_value;
};

class CTrieLeafNode: public CTrieExtNode
{
public:
	CTrieLeafNode(bytesConstRef _key, std::string const& _value): CTrieExtNode(_key), m_value(_value) {}

#if ENABLE_DEBUG_PRINT
	virtual void debugPrintBody(std::string const& _indent) const
	{
		assert(m_value.size());
		std::cerr << _indent;
		if (m_ext.size())
			std::cerr << toHex(m_ext, 1) << ": ";
		else
			std::cerr << "@: ";
		std::cerr << m_value << std::endl;
	}
#endif

	virtual std::string const& at(bytesConstRef _key) const override { return contains(_key) ? m_value : c_nullString; }
	virtual CMemTrieNode* insert(bytesConstRef _key, std::string const& _value) override;
	virtual CMemTrieNode* remove(bytesConstRef _key) override;
	virtual void makeRLP(RLPStream& _parentStream) const override;

private:
	bool contains(bytesConstRef _key) const { return _key.size() == m_ext.size() && !memcmp(_key.data(), m_ext.data(), _key.size()); }

	std::string m_value;
};

class CTrieInfixNode: public CTrieExtNode
{
public:
	CTrieInfixNode(bytesConstRef _key, CMemTrieNode* _next): CTrieExtNode(_key), m_next(_next) {}
	virtual ~CTrieInfixNode() { delete m_next; }

#if ENABLE_DEBUG_PRINT
	virtual void debugPrintBody(std::string const& _indent) const
	{
		std::cerr << _indent << toHex(m_ext, 1) << ": ";
		m_next->debugPrint(_indent + "  ");
	}
#endif

	virtual std::string const& at(bytesConstRef _key) const override { assert(m_next); return contains(_key) ? m_next->at(_key.cropped(m_ext.size())) : c_nullString; }
	virtual CMemTrieNode* insert(bytesConstRef _key, std::string const& _value) override;
	virtual CMemTrieNode* remove(bytesConstRef _key) override;
	virtual void makeRLP(RLPStream& _parentStream) const override;

private:
	bool contains(bytesConstRef _key) const { return _key.size() >= m_ext.size() && !memcmp(_key.data(), m_ext.data(), m_ext.size()); }

	CMemTrieNode* m_next;
};



template <class _KeyType, class _DB>
void CMemTrie<_KeyType, _DB>::debugPrint()
{
#if ENABLE_DEBUG_PRINT
	if (m_root)
		m_root->debugPrint();
#endif
}
template <class _KeyType, class _DB>
std::string const& CMemTrie<_KeyType, _DB>::at(std::string const& _key) const
{
	if (!m_root)
		return c_nullString;
	auto h = asNibbles(_key);
	return m_root->at(bytesConstRef(&h));
}

template <class _KeyType, class _DB>
void CMemTrie<_KeyType, _DB>::insert(std::string const& _key, std::string const& _value)
{
	if (_value.empty())
		remove(_key);
	auto h = asNibbles(_key);
	m_root = m_root ? m_root->insert(&h, _value) : new CTrieLeafNode(bytesConstRef(&h), _value);
}

template <class _KeyType, class _DB>
void CMemTrie<_KeyType, _DB>::remove(std::string const& _key)
{
	if (m_root)
	{
		auto h = asNibbles(_key);
		m_root = m_root->remove(&h);
	}
}
template <class _KeyType, class _DB>
void CMemTrie<_KeyType, _DB>::setRoot(h256 const& _root, Verification _v)
{
	m_rootHash = _root;
	if (_v == Verification::Normal)
	{
		if (m_rootHash == c_shaNull && !m_db->exists(m_rootHash))
			init();
	}
//		if (!node(m_root).size())
//			BOOST_THROW_EXCEPTION(RootNotFound());
}

}
