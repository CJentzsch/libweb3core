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

class TrieNode
{
public:
	TrieNode() {}
	virtual ~TrieNode() {}

	virtual std::string const& at(bytesConstRef _key) const = 0;
	virtual TrieNode* insert(bytesConstRef _key, std::string const& _value) = 0;
	virtual TrieNode* remove(bytesConstRef _key) = 0;
	void putRLP(RLPStream& _parentStream) const;

#if ENABLE_DEBUG_PRINT
	void debugPrint(std::string const& _indent = "") const { std::cerr << std::hex << hash256() << ":" << std::dec << std::endl; debugPrintBody(_indent); }
#endif

	/// 256-bit hash of the node - this is a SHA-3/256 hash of the RLP of the node.
	h256 hash256() const { RLPStream s; makeRLP(s); return dev::sha3(s.out()); }
	bytes rlp() const { RLPStream s; makeRLP(s); return s.out(); }
	void mark() { m_hash256 = h256(); }

	static std::vector<TrieNode*> inserts;

protected:
	virtual void makeRLP(RLPStream& _intoStream) const = 0;

#if ENABLE_DEBUG_PRINT
	virtual void debugPrintBody(std::string const& _indent = "") const = 0;
#endif

	static TrieNode* newBranch(bytesConstRef _k1, std::string const& _v1, bytesConstRef _k2, std::string const& _v2);

private:
	mutable h256 m_hash256;
};

static const std::string c_nullString;

/**
 * @brief Merkle Patricia Tree "Trie": a modifed base-16 Radix tree.
 */
template <class _KeyType, class _DB>
class BaseTrie: public _DB
{
public:
	explicit BaseTrie(_DB* _db = nullptr): m_hashed(true), m_rootNode(nullptr), m_db(_db) {}
	BaseTrie(_DB* _db, h256 const& _root, Verification _v = Verification::Normal): m_hashed(true), m_rootNode(nullptr), m_root(_root), m_db(_db) {(void)_v;}
	~BaseTrie() { delete m_rootNode; }

	void open(_DB* _db) { m_db = _db; }
	void open(_DB* _db, h256 const& _root, Verification _v = Verification::Normal) { m_db = _db; setRoot(_root, _v); }

	void setRoot(h256 const& _root, Verification _v = Verification::Normal);

	/// True if the trie is uninitialised (i.e. that the DB doesn't contain the root node).
	bool isNull() const { return m_rootNode ? true : false;} // TODO return !node(m_rootHash).size(); }
	/// True if the trie is initialised but empty (i.e. that the DB contains the root node which is empty).
	bool isEmpty() const {return m_root == c_shaNull;} //TODO return m_rootHash == c_shaNull && node(m_rootHash).size(); }

	void init() { insertNode(&RLPNull); setRoot(sha3(RLPNull));}  //insert(sha3(RLPNull), RLPNull); }

	h256 root() const { return m_rootNode ? m_rootNode->hash256() : sha3(dev::rlp(bytesConstRef())); }
	bytes rlp() const { return m_rootNode ? m_rootNode->rlp() : dev::rlp(bytesConstRef()); }

	void debugPrint();

	std::string operator[](_KeyType _k) const { return at(_k); }
	std::string at(_KeyType _k) const { return atInternal(bytesConstRef((byte const*)&_k, sizeof(_KeyType))); }

	void insert(_KeyType const& _key, bytesConstRef _value) { insertInternal(bytesConstRef((byte const*)&_key, sizeof(_KeyType)), _value); }
	void insert(_KeyType const& _key, bytes const& _value) { insertInternal(bytesConstRef((byte const*)&_key, sizeof(_KeyType)), bytesConstRef(&_value)); }
	void insert(bytes const& _key, bytes const& _value) { insertInternal(bytesConstRef(&_key), bytesConstRef(&_value)); }

	void remove(_KeyType const& _key) {  removeInternal(bytesConstRef((byte const*)&_key, sizeof(_KeyType))); }

	void insertNode(TrieNode* _node);
	void insertNode(bytesConstRef _v) { auto h = sha3(_v); m_db->insert(h, _v);}
	void removeNode(TrieNode* _node);
	void removeNode(bytesConstRef _v) { auto h = sha3(_v); m_db->kill(h, _v);}
	std::string node(h256 const& _h) const { return m_db->lookup(_h); }

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
		iterator(BaseTrie const* _db, bytesConstRef _key);

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

	void setHashed(bool _isHashed) { m_hashed = _isHashed; }
	bool isHashed() { return m_hashed; }

protected:
	std::string const& atInternal(bytesConstRef _key) const;
	void insertInternal(bytesConstRef _key, bytesConstRef _value);// { insert(asString(sha3(_key).asBytes()), asString(_value)); }
	void removeInternal(bytesConstRef _key);

	bool m_hashed;
	TrieNode* m_rootNode;
	h256 m_root;
	_DB* m_db = nullptr;
};

#define ENABLE_DEBUG_PRINT 0

/*/
#define APPEND_CHILD appendData
/*/
#define APPEND_CHILD appendRaw
/**/


class TrieExtNode: public TrieNode
{
public:
	TrieExtNode(bytesConstRef _bytes): m_ext(_bytes.begin(), _bytes.end()) {}

	bytes m_ext;
};

class TrieBranchNode: public TrieNode
{
public:
	TrieBranchNode(std::string const& _value): m_value(_value)
	{
		memset(m_nodes.data(), 0, sizeof(TrieNode*) * 16);
	}

	TrieBranchNode(byte _i1, TrieNode* _n1, std::string const& _value = std::string()): m_value(_value)
	{
		memset(m_nodes.data(), 0, sizeof(TrieNode*) * 16);
		m_nodes[_i1] = _n1;
	}

	TrieBranchNode(byte _i1, TrieNode* _n1, byte _i2, TrieNode* _n2)
	{
		memset(m_nodes.data(), 0, sizeof(TrieNode*) * 16);
		m_nodes[_i1] = _n1;
		m_nodes[_i2] = _n2;
	}

	virtual ~TrieBranchNode()
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
	virtual TrieNode* insert(bytesConstRef _key, std::string const& _value) override;
	virtual TrieNode* remove(bytesConstRef _key) override;
	virtual void makeRLP(RLPStream& _parentStream) const override;

private:
	/// @returns (byte)-1 when no active branches, 16 when multiple active and the index of the active branch otherwise.
	byte activeBranch() const;

	TrieNode* rejig();

	std::array<TrieNode*, 16> m_nodes;
	std::string m_value;
};

class TrieLeafNode: public TrieExtNode
{
public:
	TrieLeafNode(bytesConstRef _key, std::string const& _value): TrieExtNode(_key), m_value(_value) {}

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
	virtual TrieNode* insert(bytesConstRef _key, std::string const& _value) override;
	virtual TrieNode* remove(bytesConstRef _key) override;
	virtual void makeRLP(RLPStream& _parentStream) const override;

private:
	bool contains(bytesConstRef _key) const { return _key.size() == m_ext.size() && !memcmp(_key.data(), m_ext.data(), _key.size()); }

	std::string m_value;
};

class TrieInfixNode: public TrieExtNode
{
public:
	TrieInfixNode(bytesConstRef _key, TrieNode* _next): TrieExtNode(_key), m_next(_next) {}
	virtual ~TrieInfixNode() { delete m_next; }

#if ENABLE_DEBUG_PRINT
	virtual void debugPrintBody(std::string const& _indent) const
	{
		std::cerr << _indent << toHex(m_ext, 1) << ": ";
		m_next->debugPrint(_indent + "  ");
	}
#endif

	virtual std::string const& at(bytesConstRef _key) const override { assert(m_next); return contains(_key) ? m_next->at(_key.cropped(m_ext.size())) : c_nullString; }
	virtual TrieNode* insert(bytesConstRef _key, std::string const& _value) override;
	virtual TrieNode* remove(bytesConstRef _key) override;
	virtual void makeRLP(RLPStream& _parentStream) const override;

private:
	bool contains(bytesConstRef _key) const { return _key.size() >= m_ext.size() && !memcmp(_key.data(), m_ext.data(), m_ext.size()); }

	TrieNode* m_next;
};

template <class _KeyType, class _DB>
void BaseTrie<_KeyType, _DB>::insertNode(TrieNode* _node)
{
	//(void) _node;
	bytes rlp = _node->rlp(); // horribly inefficient - needs improvement TODO
	m_db->insert(_node->hash256(), bytesConstRef(&rlp));

	// insert nodes from TrieNode::inserts list
	for (auto const& p: TrieNode::inserts)
	{
		bytes rlp = p->rlp();
		m_db->insert(p->hash256(), bytesConstRef(&rlp));
		delete p;
	}
	TrieNode::inserts.clear();
}

template <class _KeyType, class _DB>
void BaseTrie<_KeyType, _DB>::removeNode(TrieNode* _node)
{
	m_db->kill(_node->hash256());
}

template <class _KeyType, class _DB>
void BaseTrie<_KeyType, _DB>::debugPrint()
{
#if ENABLE_DEBUG_PRINT
	if (m_root)
		m_root->debugPrint();
#endif
}
template <class _KeyType, class _DB>
std::string const& BaseTrie<_KeyType, _DB>::atInternal(bytesConstRef _key) const
{
	if (!m_rootNode)
		return c_nullString;

	if (m_hashed)
	{
		h256 key = sha3(_key);
		_key = bytesConstRef((byte const*)&key, sizeof(h256));
	}

	auto h = asNibbles(_key);
	return m_rootNode->at(bytesConstRef(&h));
}

template <class _KeyType, class _DB>
void BaseTrie<_KeyType, _DB>::insertInternal(bytesConstRef _key, bytesConstRef _value)
{
	if (m_hashed)
	{
		h256 key = sha3(_key);
		_key = bytesConstRef((byte const*)&key, sizeof(h256));
	}
	if (_value.empty())
		removeInternal(_key);
	auto h = asNibbles(_key);

	m_rootNode = m_rootNode ? m_rootNode->insert(bytesConstRef(&h), _value.toString()) : new TrieLeafNode(bytesConstRef(&h), _value.toString());
	insertNode(m_rootNode);
}

template <class _KeyType, class _DB>
void BaseTrie<_KeyType, _DB>::removeInternal(bytesConstRef _key)
{
	if (m_rootNode)
	{
		if (m_hashed)
		{
			h256 key = sha3(_key);
			_key = bytesConstRef((byte const*)&key, sizeof(h256));
		}
		auto h = asNibbles(_key);
		m_rootNode = m_rootNode->remove(bytesConstRef(&h));
		insertNode(m_rootNode);
	}
}
template <class _KeyType, class _DB>
void BaseTrie<_KeyType, _DB>::setRoot(h256 const& _root, Verification _v)
{
	m_root = _root;

	if (_v == Verification::Normal)
	{
		if (m_root == c_shaNull && !m_db->exists(m_root))
			init();
	}
		if (!node(m_root).size())
			BOOST_THROW_EXCEPTION(RootNotFound());
}

}
