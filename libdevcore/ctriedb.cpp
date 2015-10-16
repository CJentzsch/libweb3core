#include "ctriedb.h"

using namespace std;
using namespace dev;

namespace dev
{


void CMemTrieNode::putRLP(RLPStream& _parentStream) const
{
	RLPStream s;
	makeRLP(s);
	if (s.out().size() < 32)
		_parentStream.APPEND_CHILD(s.out());
	else
		_parentStream << dev::sha3(s.out());
}

void CTrieBranchNode::makeRLP(RLPStream& _intoStream) const
{
	_intoStream.appendList(17);
	for (auto i: m_nodes)
		if (i)
			i->putRLP(_intoStream);
		else
			_intoStream << "";
	_intoStream << m_value;
}

void CTrieLeafNode::makeRLP(RLPStream& _intoStream) const
{
	_intoStream.appendList(2) << hexPrefixEncode(m_ext, true) << m_value;
}

void CTrieInfixNode::makeRLP(RLPStream& _intoStream) const
{
	assert(m_next);
	_intoStream.appendList(2);
	_intoStream << hexPrefixEncode(m_ext, false);
	m_next->putRLP(_intoStream);
}

CMemTrieNode* CMemTrieNode::newBranch(bytesConstRef _k1, std::string const& _v1, bytesConstRef _k2, std::string const& _v2)
{
	unsigned prefix = commonPrefix(_k1, _k2);

	CMemTrieNode* ret;
	if (_k1.size() == prefix)
		ret = new CTrieBranchNode(_k2[prefix], new CTrieLeafNode(_k2.cropped(prefix + 1), _v2), _v1);
	else if (_k2.size() == prefix)
		ret = new CTrieBranchNode(_k1[prefix], new CTrieLeafNode(_k1.cropped(prefix + 1), _v1), _v2);
	else // both continue after split
		ret = new CTrieBranchNode(_k1[prefix], new CTrieLeafNode(_k1.cropped(prefix + 1), _v1), _k2[prefix], new CTrieLeafNode(_k2.cropped(prefix + 1), _v2));

	if (prefix)
		// have shared prefix - split.
		ret = new CTrieInfixNode(_k1.cropped(0, prefix), ret);

	return ret;
}

std::string const& CTrieBranchNode::at(bytesConstRef _key) const
{
	if (_key.empty())
		return m_value;
	else if (m_nodes[_key[0]] != nullptr)
		return m_nodes[_key[0]]->at(_key.cropped(1));
	return c_nullString;
}

CMemTrieNode* CTrieBranchNode::insert(bytesConstRef _key, std::string const& _value)
{
	assert(_value.size());
	mark();
	if (_key.empty())
		m_value = _value;
	else
		if (!m_nodes[_key[0]])
			m_nodes[_key[0]] = new CTrieLeafNode(_key.cropped(1), _value);
		else
			m_nodes[_key[0]] = m_nodes[_key[0]]->insert(_key.cropped(1), _value);
	return this;
}

CMemTrieNode* CTrieBranchNode::remove(bytesConstRef _key)
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
	return this;
}

CMemTrieNode* CTrieBranchNode::rejig()
{
	mark();
	byte n = activeBranch();

	if (n == (byte)-1 && m_value.size())
	{
		// switch to leaf
		auto r = new CTrieLeafNode(bytesConstRef(), m_value);
		delete this;
		return r;
	}
	else if (n < 16 && m_value.empty())
	{
		// only branching to n...
		if (auto b = dynamic_cast<CTrieBranchNode*>(m_nodes[n]))
		{
			// switch to infix
			m_nodes[n] = nullptr;
			delete this;
			return new CTrieInfixNode(bytesConstRef(&n, 1), b);
		}
		else
		{
			auto x = dynamic_cast<CTrieExtNode*>(m_nodes[n]);
			assert(x);
			// include in child
			pushFront(x->m_ext, n);
			m_nodes[n] = nullptr;
			delete this;
			return x;
		}
	}

	return this;
}

byte CTrieBranchNode::activeBranch() const
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

CMemTrieNode* CTrieInfixNode::insert(bytesConstRef _key, std::string const& _value)
{
	assert(_value.size());
	mark();
	if (contains(_key))
	{
		m_next = m_next->insert(_key.cropped(m_ext.size()), _value);
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

			return new CTrieInfixNode(_key.cropped(0, prefix), insert(_key.cropped(prefix), _value));
		}
		else
		{
			// split here.
			auto f = m_ext[0];
			trimFront(m_ext, 1);
			CMemTrieNode* n = m_ext.empty() ? m_next : this;
			if (n != this)
			{
				m_next = nullptr;
				delete this;
			}
			CTrieBranchNode* ret = new CTrieBranchNode(f, n);
			ret->insert(_key, _value);
			return ret;
		}
	}
}

CMemTrieNode* CTrieInfixNode::remove(bytesConstRef _key)
{
	if (contains(_key))
	{
		mark();
		m_next = m_next->remove(_key.cropped(m_ext.size()));
		if (auto p = dynamic_cast<CTrieExtNode*>(m_next))
		{
			// merge with child...
			m_ext.reserve(m_ext.size() + p->m_ext.size());
			for (auto i: p->m_ext)
				m_ext.push_back(i);
			p->m_ext = m_ext;
			p->mark();
			m_next = nullptr;
			delete this;
			return p;
		}
		if (!m_next)
		{
			delete this;
			return nullptr;
		}
	}
	return this;
}

CMemTrieNode* CTrieLeafNode::insert(bytesConstRef _key, std::string const& _value)
{
	assert(_value.size());
	mark();
	if (contains(_key))
	{
		m_value = _value;
		return this;
	}
	else
	{
		// create new trie.
		auto n = CMemTrieNode::newBranch(_key, _value, bytesConstRef(&m_ext), m_value);
		delete this;
		return n;
	}
}

CMemTrieNode* CTrieLeafNode::remove(bytesConstRef _key)
{
	if (contains(_key))
	{
		delete this;
		return nullptr;
	}
	return this;
}
}
