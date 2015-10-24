#include "ctriedb.h"

using namespace std;
using namespace dev;

namespace dev
{
BaseDB* TrieNode::m_db = nullptr;

void TrieNode::putRLP(RLPStream& _parentStream) const
{
	RLPStream s;
	makeRLP(s);
	if (s.out().size() < 32)
		_parentStream.APPEND_CHILD(s.out());
	else
		_parentStream << dev::sha3(s.out());
}

void TrieNode::insertNodeInDB()
{
	bytes rlp = this->rlp(); // horribly inefficient - needs improvement TODO
	m_db->insert(hash256(), bytesConstRef(&rlp));
}

TrieNode* TrieNode::lookupNode(h256 const& _h) const
{
	(void)_h;
	return new TrieInfixNodeCJ(bytesConstRef(), h256());
//	RLP rlp(m_db->lookup(_h));
//	// construct Node
//	if (rlp.itemCount() == 17)
//	{
//		// branch node
////		assert(rlp.isList());
////		std::array<TrieNode*, 16> nodes;
////		for (auto const& node : nodes)


////		TrieBranchNodeCJ* n = new TrieBranchNodeCJ();
//	}
//	else
//	{
//		// TrieExtNodeCJ
//		if (rlp[0].payload()[0] & 0x20 != 0)
//			// is leaf
//			;
//		else
//			// is Infix
	//TrieInfixNodeCJ* node = new TrieInfixNodeCJ(rlp[0].payload());
//			;

//	}


}

void TrieBranchNodeCJ::makeRLP(RLPStream& _intoStream) const
{
	_intoStream.appendList(17);
	for (auto i: m_nodes)
		if (i)
			lookupNode(i)->putRLP(_intoStream);
		else
			_intoStream << "";
	_intoStream << m_value;
}

void TrieLeafNodeCJ::makeRLP(RLPStream& _intoStream) const
{
	_intoStream.appendList(2) << hexPrefixEncode(m_ext, true) << m_value;
}

void TrieInfixNodeCJ::makeRLP(RLPStream& _intoStream) const
{
	assert(m_next);
	_intoStream.appendList(2);
	_intoStream << hexPrefixEncode(m_ext, false);
	lookupNode(m_next)->putRLP(_intoStream);
}

TrieNode* TrieNode::newBranch(bytesConstRef _k1, std::string const& _v1, bytesConstRef _k2, std::string const& _v2)
{
	unsigned prefix = commonPrefix(_k1, _k2);

	TrieNode* ret;
	if (_k1.size() == prefix)
	{
		TrieLeafNodeCJ* leaf = new TrieLeafNodeCJ(_k2.cropped(prefix + 1), _v2);
		leaf->insertNodeInDB();
		ret = new TrieBranchNodeCJ(_k2[prefix], leaf->hash256(), _v1);
	}
	else if (_k2.size() == prefix)
	{
		TrieLeafNodeCJ* leaf = new TrieLeafNodeCJ(_k1.cropped(prefix + 1), _v1);
		leaf->insertNodeInDB();
		ret = new TrieBranchNodeCJ(_k1[prefix], leaf->hash256(), _v2);
	}
	else // both continue after split
	{
		TrieLeafNodeCJ* leaf1 = new TrieLeafNodeCJ(_k1.cropped(prefix + 1), _v1);
		leaf1->insertNodeInDB();
		TrieLeafNodeCJ* leaf2 = new TrieLeafNodeCJ(_k2.cropped(prefix + 1), _v2);
		leaf2->insertNodeInDB();
		ret = new TrieBranchNodeCJ(_k1[prefix], leaf1->hash256(), _k2[prefix], leaf2->hash256());
	}
	if (prefix)
	{
		// have shared prefix - split.
		ret->insertNodeInDB();
		ret = new TrieInfixNodeCJ(_k1.cropped(0, prefix), ret->hash256());
	}
	ret->insertNodeInDB();
	return ret;
}

std::string const& TrieBranchNodeCJ::at(bytesConstRef _key) const
{
	if (_key.empty())
		return m_value;
	else if (m_nodes[_key[0]] != h256())
		return lookupNode(m_nodes[_key[0]])->at(_key.cropped(1));
	return c_nullString;
}

TrieNode* TrieBranchNodeCJ::insert(bytesConstRef _key, std::string const& _value)
{
	assert(_value.size());
	mark();
	if (_key.empty())
		m_value = _value;
	else
	{
		if (!m_nodes[_key[0]])
		{
			TrieLeafNodeCJ* n = new TrieLeafNodeCJ(_key.cropped(1), _value);
			n->insertNodeInDB();
			m_nodes[_key[0]] = n->hash256();// new TrieLeafNodeCJ(_key.cropped(1), _value); // TODO add to leveldb
		}
		else
		{
			TrieNode* n = lookupNode(m_nodes[_key[0]])->insert(_key.cropped(1), _value);
			n->insertNodeInDB();
			m_nodes[_key[0]] = n->hash256();
		}
		lookupNode(m_nodes[_key[0]])->insertNodeInDB();
	}
	this->insertNodeInDB();
	return this;
}

TrieNode* TrieBranchNodeCJ::remove(bytesConstRef _key)
{
	if (_key.empty())
		if (m_value.size())
		{
			m_value.clear();
			return rejig();
		}
		else {}
	else if (m_nodes[_key[0]] != h256())
	{
		TrieNode* n = lookupNode(m_nodes[_key[0]])->remove(_key.cropped(1));
		m_nodes[_key[0]] = n->hash256();
		return rejig();
	}
	this->insertNodeInDB();
	return this;
}

TrieNode* TrieBranchNodeCJ::rejig()
{
	mark();
	byte n = activeBranch();

	if (n == (byte)-1 && m_value.size())
	{
		// switch to leaf
		auto r = new TrieLeafNodeCJ(bytesConstRef(), m_value);
		delete this;
		r->insertNodeInDB();
		return r;
	}
	else if (n < 16 && m_value.empty())
	{
		// only branching to n...
		if (auto b = dynamic_cast<TrieBranchNodeCJ*>(lookupNode(m_nodes[n])))
		{
			// switch to infix
			m_nodes[n] = h256();
			delete this;
			TrieNode* node = new TrieInfixNodeCJ(bytesConstRef(&n, 1), b->hash256());
			node->insertNodeInDB();
			return node;
		}
		else
		{
			auto x = dynamic_cast<TrieExtNodeCJ*>(lookupNode(m_nodes[n]));
			assert(x);
			// include in child
			pushFront(x->m_ext, n);
			m_nodes[n] = h256();
			delete this;
			x->insertNodeInDB();
			return x;
		}
	}
	this->insertNodeInDB();
	return this;
}

byte TrieBranchNodeCJ::activeBranch() const
{
	byte n = (byte)-1;
	for (int i = 0; i < 16; ++i)
		if (m_nodes[i] != h256())
		{
			if (n == (byte)-1)
				n = (byte)i;
			else
				return 16;
		}
	return n;
}

TrieNode* TrieInfixNodeCJ::insert(bytesConstRef _key, std::string const& _value)
{
	assert(_value.size());
	mark();
	if (contains(_key))
	{
		m_next = lookupNode(m_next)->insert(_key.cropped(m_ext.size()), _value)->hash256();
		this->insertNodeInDB();
		return this;
	}
	else
	{
		unsigned prefix = commonPrefix(_key, m_ext);
		if (prefix)
		{
			// one infix becomes two infixes, then insert into the second
			// instead of pop_front()...
			trimFront(m_ext, prefix);
			TrieInfixNodeCJ* ret = new TrieInfixNodeCJ(_key.cropped(0, prefix), insert(_key.cropped(prefix), _value)->hash256());
			ret->insertNodeInDB();
			return ret; // TODO add to leveldb
		}
		else
		{
			// split here.
			auto f = m_ext[0];
			trimFront(m_ext, 1);
			TrieNode* n = m_ext.empty() ? lookupNode(m_next) : this;
			n->insertNodeInDB();
			if (n != this)
			{
				m_next = h256();
				delete this; // TODO remove from leveldb
			}
			TrieBranchNodeCJ* ret = new TrieBranchNodeCJ(f, n->hash256());
			ret->insert(_key, _value);
			ret->insertNodeInDB();
			return ret;
		}
	}
}

TrieNode* TrieInfixNodeCJ::remove(bytesConstRef _key)
{
	if (contains(_key))
	{
		mark();
		m_next = lookupNode(m_next)->remove(_key.cropped(m_ext.size()))->hash256();
		if (auto p = dynamic_cast<TrieExtNodeCJ*>(lookupNode(m_next)))
		{
			// merge with child...
			m_ext.reserve(m_ext.size() + p->m_ext.size());
			for (auto i: p->m_ext)
				m_ext.push_back(i);
			p->m_ext = m_ext;
			p->mark();
			m_next = h256();
			delete this;
			p->insertNodeInDB();
			return p;
		}
		if (!m_next)
		{
			delete this;
			return nullptr;
		}
	}
	this->insertNodeInDB();
	return this;
}

TrieNode* TrieLeafNodeCJ::insert(bytesConstRef _key, std::string const& _value)
{
	assert(_value.size());
	mark();
	if (contains(_key))
	{
		m_value = _value;
		this->insertNodeInDB();
		return this; // TODO add to leveldb
	}
	else
	{
		// create new trie.
		auto n = TrieNode::newBranch(_key, _value, bytesConstRef(&m_ext), m_value);
		delete this;
		n->insertNodeInDB();
		return n;
	}
}

TrieNode* TrieLeafNodeCJ::remove(bytesConstRef _key)
{
	if (contains(_key))
	{
		delete this;
		return nullptr;
	}
	this->insertNodeInDB();
	return this;
}
}
