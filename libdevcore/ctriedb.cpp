#include "ctriedb.h"

using namespace std;
using namespace dev;

namespace dev
{
BaseDB* TrieNode::m_db = nullptr;
//std::vector<TrieNode*> TrieNode::inserts = {};

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

void TrieBranchNode::makeRLP(RLPStream& _intoStream) const
{
	_intoStream.appendList(17);
	for (auto i: m_nodes)
		if (i)
			i->putRLP(_intoStream);
		else
			_intoStream << "";
	_intoStream << m_value;
}

void TrieLeafNode::makeRLP(RLPStream& _intoStream) const
{
	_intoStream.appendList(2) << hexPrefixEncode(m_ext, true) << m_value;
}

void TrieInfixNode::makeRLP(RLPStream& _intoStream) const
{
	assert(m_next);
	_intoStream.appendList(2);
	_intoStream << hexPrefixEncode(m_ext, false);
	m_next->putRLP(_intoStream);
}

TrieNode* TrieNode::newBranch(bytesConstRef _k1, std::string const& _v1, bytesConstRef _k2, std::string const& _v2)
{
	unsigned prefix = commonPrefix(_k1, _k2);

	TrieNode* ret;
	if (_k1.size() == prefix)
	{
		TrieLeafNode* leaf = new TrieLeafNode(_k2.cropped(prefix + 1), _v2);
		leaf->insertNodeInDB();
		ret = new TrieBranchNode(_k2[prefix], leaf, _v1);
	}
	else if (_k2.size() == prefix)
	{
		TrieLeafNode* leaf = new TrieLeafNode(_k1.cropped(prefix + 1), _v1);
		leaf->insertNodeInDB();
		ret = new TrieBranchNode(_k1[prefix], leaf, _v2);
	}
	else // both continue after split
	{
		TrieLeafNode* leaf1 = new TrieLeafNode(_k1.cropped(prefix + 1), _v1);
		leaf1->insertNodeInDB();
		TrieLeafNode* leaf2 = new TrieLeafNode(_k2.cropped(prefix + 1), _v2);
		leaf2->insertNodeInDB();
		ret = new TrieBranchNode(_k1[prefix], leaf1, _k2[prefix], leaf2);
	}
	if (prefix)
		// have shared prefix - split.
		ret = new TrieInfixNode(_k1.cropped(0, prefix), ret);
	ret->insertNodeInDB();
	return ret;
}

std::string const& TrieBranchNode::at(bytesConstRef _key) const
{
	if (_key.empty())
		return m_value;
	else if (m_nodes[_key[0]] != nullptr)
		return m_nodes[_key[0]]->at(_key.cropped(1));
	return c_nullString;
}

TrieNode* TrieBranchNode::insert(bytesConstRef _key, std::string const& _value)
{
	assert(_value.size());
	mark();
	if (_key.empty())
		m_value = _value;
	else
	{
		if (!m_nodes[_key[0]])
			m_nodes[_key[0]] = new TrieLeafNode(_key.cropped(1), _value); // TODO add to leveldb
		else
			m_nodes[_key[0]] = m_nodes[_key[0]]->insert(_key.cropped(1), _value);
		m_nodes[_key[0]]->insertNodeInDB();
	}
	this->insertNodeInDB();
	return this;
}

TrieNode* TrieBranchNode::remove(bytesConstRef _key)
{
	if (_key.empty())
		if (m_value.size())
		{
			m_value.clear();
			return rejig();
		}
		else {}
	else if (m_nodes[_key[0]] != nullptr)
	{
		m_nodes[_key[0]] = m_nodes[_key[0]]->remove(_key.cropped(1));
		return rejig();
	}
	this->insertNodeInDB();
	return this;
}

TrieNode* TrieBranchNode::rejig()
{
	mark();
	byte n = activeBranch();

	if (n == (byte)-1 && m_value.size())
	{
		// switch to leaf
		auto r = new TrieLeafNode(bytesConstRef(), m_value);
		delete this;
		r->insertNodeInDB();
		return r;
	}
	else if (n < 16 && m_value.empty())
	{
		// only branching to n...
		if (auto b = dynamic_cast<TrieBranchNode*>(m_nodes[n]))
		{
			// switch to infix
			m_nodes[n] = nullptr;
			delete this;
			TrieNode* node = new TrieInfixNode(bytesConstRef(&n, 1), b);
			node->insertNodeInDB();
			return node;
		}
		else
		{
			auto x = dynamic_cast<TrieExtNode*>(m_nodes[n]);
			assert(x);
			// include in child
			pushFront(x->m_ext, n);
			m_nodes[n] = nullptr;
			delete this;
			x->insertNodeInDB();
			return x;
		}
	}
	this->insertNodeInDB();
	return this;
}

byte TrieBranchNode::activeBranch() const
{
	byte n = (byte)-1;
	for (int i = 0; i < 16; ++i)
		if (m_nodes[i] != nullptr)
		{
			if (n == (byte)-1)
				n = (byte)i;
			else
				return 16;
		}
	return n;
}

TrieNode* TrieInfixNode::insert(bytesConstRef _key, std::string const& _value)
{
	assert(_value.size());
	mark();
	if (contains(_key))
	{
		m_next = m_next->insert(_key.cropped(m_ext.size()), _value);
		this->insertNodeInDB();
		return this; // TODO add to leveldb
	}
	else
	{
		unsigned prefix = commonPrefix(_key, m_ext);
		if (prefix)
		{
			// one infix becomes two infixes, then insert into the second
			// instead of pop_front()...
			trimFront(m_ext, prefix);
			TrieInfixNode* ret = new TrieInfixNode(_key.cropped(0, prefix), insert(_key.cropped(prefix), _value));
			ret->insertNodeInDB();
			return ret; // TODO add to leveldb
		}
		else
		{
			// split here.
			auto f = m_ext[0];
			trimFront(m_ext, 1);
			TrieNode* n = m_ext.empty() ? m_next : this;
			if (n != this)
			{
				m_next = nullptr;
				delete this; // TODO remove from leveldb
			}
			TrieBranchNode* ret = new TrieBranchNode(f, n); // TODO add to leveldb
			ret->insert(_key, _value);
			ret->insertNodeInDB();
			return ret;
		}
	}
}

TrieNode* TrieInfixNode::remove(bytesConstRef _key)
{
	if (contains(_key))
	{
		mark();
		m_next = m_next->remove(_key.cropped(m_ext.size()));
		if (auto p = dynamic_cast<TrieExtNode*>(m_next))
		{
			// merge with child...
			m_ext.reserve(m_ext.size() + p->m_ext.size());
			for (auto i: p->m_ext)
				m_ext.push_back(i);
			p->m_ext = m_ext;
			p->mark();
			m_next = nullptr;
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

TrieNode* TrieLeafNode::insert(bytesConstRef _key, std::string const& _value)
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

TrieNode* TrieLeafNode::remove(bytesConstRef _key)
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
