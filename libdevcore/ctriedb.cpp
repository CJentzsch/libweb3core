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

TrieNode* TrieNode::lookupNode(bytes const& _h) const
{
	RLP rlp;
	if (_h.size() < 32)
		rlp = RLP(_h);
	else if (_h.size() == 32)
	{
		h256 hash(_h);
		rlp = RLP(m_db->lookup(hash));
	}
	else
		BOOST_THROW_EXCEPTION(ValueTooLarge() << errinfo_comment("Node size needs to be smaller than 32 bytesor a hash"));

	assert(!rlp.isEmpty());
	assert(!rlp.isNull());

	// construct Node
	if (rlp.itemCount() == 17)
	{
		// branch node
		//cout << "create branch\n";
		assert(rlp.isList());
		std::array<bytes, 16> nodes;
		for (byte i = 0; i < 16; i++)
		{
			//cout << "lookUpNode - branch i: " << int(i) << endl;
			if (rlp[i].isList())
				nodes[i] = bytes(rlp[i].data().begin(), rlp[i].data().end());
			else
				nodes[i] = bytes(rlp[i].payload().begin(), rlp[i].payload().end() );
		}
		string value = rlp[16].payload().toString();

		TrieBranchNodeCJ* n = new TrieBranchNodeCJ(nodes, value);
		assert(n->rlpOrHash() == _h);

		return n;
	}
	else
	{
		assert(rlp.itemCount() == 2);
		// TrieExtNodeCJ

		if ((rlp[0].payload()[0] & 0x20) != 0)
		{
			// is leaf
			bytes key;
			if (rlp[1].isList())
			{
				key = asNibbles(rlp[0].data());
				key.erase(key.begin());
			}
			else
			{
				key = asNibbles(rlp[0].payload());
				key.erase(key.begin());

				//if (key[0] == 0)
				//	key = asNibbles(rlp[0].toBytesConstRef().cropped(1)); //remove prefix
				//cout << "KEY: " << key << endl;
			}
			bytes key2 = asNibbles(rlp[0].toBytesConstRef().cropped(1));
			//bytes  //remove prefix


			TrieLeafNodeCJ* leaf = new TrieLeafNodeCJ(bytesConstRef(&key), rlp[1].toString());

			if (leaf->rlpOrHash() != _h)
			{
				TrieLeafNodeCJ* leaf2 = new TrieLeafNodeCJ(bytesConstRef(&key2), rlp[1].toString());
				return leaf2;

			}
			assert(leaf->rlpOrHash() == _h);
			//cout << "done\n";
			return leaf;
		}
		else
		{
			// is Infix
			//cout << "create infix\n";
			//cout << "RRRRRRRRRRRRRRRRRRrlp: " << rlp << endl;
			//cout << "rlp: " << rlp.payload().toBytes() << endl;
			bytes key;
			bytesConstRef value;
			//bytes key = bytes(rlp[0].data().cropped(1).begin(), rlp[0].data().cropped(1).end()) ;// asNibbles(rlp[0].toBytesConstRef().cropped(1)); //remove prefix
			if (rlp[1].isList())
			{
				key = asNibbles(rlp[0].payload());
				key.erase(key.begin()); //remove prefix

				value = rlp[1].data();
			}
			else
			{
				key = asNibbles(rlp[0].data());
				key.erase(key.begin()); //remove prefix

				value = rlp[1].payload();
			}

			////cout << "value: " << value << endl;

			//cout << "rlp[0].data(): " << rlp[0].data() << endl;
			//cout << "rlp[0].data().cropped(1): " << rlp[0].data().cropped(1) << endl;
			//cout << "bytes(rlp[0].data().begin(), rlp[0].data().end()): " << bytes(rlp[0].data().begin(), rlp[0].data().end()) << endl;
			//cout << "key: " << key << endl;
			//cout << "rlp[1].payload()" << rlp[1].payload().toBytes() << " size: " << rlp[1].payload().size() << endl;
			TrieInfixNodeCJ* inFix = new TrieInfixNodeCJ(&key, value);
			//cout << "done\n";
			inFix->insertNodeInDB();
			assert(inFix->rlpOrHash() == _h);
			return inFix;
		}

	}
	//cout << "done lookup\n";

}

void TrieBranchNodeCJ::makeRLP(RLPStream& _intoStream) const
{
	_intoStream.appendList(17);
	for (auto i: m_nodes)
	{
		//cout << "TrieBranchNodeCJ::makeRLP i:" << i << endl;
		if (!i.empty())
			lookupNode(i)->putRLP(_intoStream);
		else
			_intoStream << "";
	}
	_intoStream << m_value;
}

void TrieLeafNodeCJ::makeRLP(RLPStream& _intoStream) const
{
	_intoStream.appendList(2) << hexPrefixEncode(m_ext, true) << m_value;
}

void TrieInfixNodeCJ::makeRLP(RLPStream& _intoStream) const
{
	assert(!m_next.empty());
	_intoStream.appendList(2);
	_intoStream << hexPrefixEncode(m_ext, false);
	//cout << "AHA\n";
	lookupNode(m_next)->putRLP(_intoStream);
	//cout << "BHB\n";
}

TrieNode* TrieNode::newBranch(bytesConstRef _k1, std::string const& _v1, bytesConstRef _k2, std::string const& _v2)
{
	unsigned prefix = commonPrefix(_k1, _k2);

	TrieNode* ret;
	if (_k1.size() == prefix)
	{
		TrieLeafNodeCJ* leaf = new TrieLeafNodeCJ(_k2.cropped(prefix + 1), _v2);
		leaf->insertNodeInDB();
		ret = new TrieBranchNodeCJ(_k2[prefix], leaf->rlpOrHash(), _v1);
	}
	else if (_k2.size() == prefix)
	{
		TrieLeafNodeCJ* leaf = new TrieLeafNodeCJ(_k1.cropped(prefix + 1), _v1);
		leaf->insertNodeInDB();
		ret = new TrieBranchNodeCJ(_k1[prefix], leaf->rlpOrHash(), _v2);
	}
	else // both continue after split
	{
		TrieLeafNodeCJ* leaf1 = new TrieLeafNodeCJ(_k1.cropped(prefix + 1), _v1);
		leaf1->insertNodeInDB();
		TrieLeafNodeCJ* leaf2 = new TrieLeafNodeCJ(_k2.cropped(prefix + 1), _v2);
		leaf2->insertNodeInDB();
		ret = new TrieBranchNodeCJ(_k1[prefix], leaf1->rlpOrHash(), _k2[prefix], leaf2->rlpOrHash());
	}
	if (prefix)
	{
		// have shared prefix - split.
		ret->insertNodeInDB();
		ret = new TrieInfixNodeCJ(_k1.cropped(0, prefix), ret->rlpOrHash());
	}
	ret->insertNodeInDB();
	return ret;
}

std::string const& TrieBranchNodeCJ::at(bytesConstRef _key) const
{
	//cout << "key at branch: " << _key << endl;
	if (_key.empty())
		return m_value;
	else if (m_nodes[_key[0]] != bytes())
		return lookupNode(m_nodes[_key[0]])->at(_key.cropped(1));
	return c_nullString;
}

TrieNode* TrieBranchNodeCJ::insert(bytesConstRef _key, std::string const& _value)
{
	debug();
	assert(_value.size());
	mark();
	if (_key.empty())
		m_value = _value;
	else
	{
		if (m_nodes[_key[0]].empty())
		{
			TrieLeafNodeCJ* n = new TrieLeafNodeCJ(_key.cropped(1), _value);
			n->insertNodeInDB();
			m_nodes[_key[0]] = n->rlpOrHash();// new TrieLeafNodeCJ(_key.cropped(1), _value); // TODO add to leveldb
		}
		else
		{
			TrieNode* n = lookupNode(m_nodes[_key[0]])->insert(_key.cropped(1), _value);
			n->insertNodeInDB();
			m_nodes[_key[0]] = n->rlpOrHash();
		}
		lookupNode(m_nodes[_key[0]])->insertNodeInDB();
	}
	this->insertNodeInDB();
	debug();
	return this;
}

TrieNode* TrieBranchNodeCJ::remove(bytesConstRef _key)
{
	debug();
	if (_key.empty())
		if (m_value.size())
		{
			m_value.clear();
			this->insertNodeInDB();
			return rejig();
		}
		else {}
	else if (m_nodes[_key[0]] != bytes())
	{
		TrieNode* n1 = lookupNode(m_nodes[_key[0]]);
		assert(n1);
		cout << "KEY: " << _key << endl;
		cout << "KEY_cropped: " << _key.cropped(1) << endl;

		TrieNode* n = n1->remove(_key.cropped(1));
		//assert(n);
		m_nodes[_key[0]] = n ? n->rlpOrHash() : bytes();
		this->insertNodeInDB();
		return rejig();
	}
	this->insertNodeInDB();
	debug();
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
			m_nodes[n] = bytes();
			delete this;
			TrieNode* node = new TrieInfixNodeCJ(bytesConstRef(&n, 1), b->rlpOrHash());
			node->insertNodeInDB();
			return node;
		}
		else
		{
			auto x = dynamic_cast<TrieExtNodeCJ*>(lookupNode(m_nodes[n]));
			x->insertNodeInDB();
			assert(x);
			// include in child
			pushFront(x->m_ext, n);
			m_nodes[n] = bytes();
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
		if (m_nodes[i] != bytes())
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
	debug();
	assert(_value.size());
	mark();
	if (contains(_key))
	{
		m_next = lookupNode(m_next)->insert(_key.cropped(m_ext.size()), _value)->rlpOrHash();
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
			this->insertNodeInDB();
			TrieInfixNodeCJ* ret = new TrieInfixNodeCJ(_key.cropped(0, prefix), insert(_key.cropped(prefix), _value)->rlpOrHash());
			ret->insertNodeInDB();
			return ret; // TODO add to leveldb
		}
		else
		{
			// split here.
			auto f = m_ext[0];
			trimFront(m_ext, 1);
			this->insertNodeInDB();
			TrieNode* n = m_ext.empty() ? lookupNode(m_next) : this;
			n->insertNodeInDB();
			if (n != this)
			{
				m_next = bytes();
				delete this; // TODO remove from leveldb
			}
			TrieBranchNodeCJ* ret = new TrieBranchNodeCJ(f, n->rlpOrHash());
			ret->insert(_key, _value);
			ret->insertNodeInDB();
			return ret;
		}
	}
}

TrieNode* TrieInfixNodeCJ::remove(bytesConstRef _key)
{
	debug();
	if (contains(_key))
	{
		mark();
		m_next = lookupNode(m_next)->remove(_key.cropped(m_ext.size()))->rlpOrHash();
		this->insertNodeInDB();
		if (auto p = dynamic_cast<TrieExtNodeCJ*>(lookupNode(m_next)))
		{
			// merge with child...
			m_ext.reserve(m_ext.size() + p->m_ext.size());
			for (auto i: p->m_ext)
				m_ext.push_back(i);
			p->m_ext = m_ext;
			p->mark();
			m_next = bytes();
			delete this;
			p->insertNodeInDB();
			this->insertNodeInDB();
			return p;
		}
		if (m_next.empty())
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
	debug();
	assert(_value.size());
	mark();
	if (contains(_key))
	{
		//cout << "contain branch\n";
		m_value = _value;
		this->insertNodeInDB();
		return this; // TODO add to leveldb
	}
	else
	{
		//cout << "create branchnode\n";
		// create new trie.
		auto n = TrieNode::newBranch(_key, _value, bytesConstRef(&m_ext), m_value);
		delete this;
		n->insertNodeInDB();
		return n;
	}
}

TrieNode* TrieLeafNodeCJ::remove(bytesConstRef _key)
{
	debug();
	if (contains(_key))
	{
		delete this;
		return nullptr;
	}
	this->insertNodeInDB();
	debug();
	return this;
}
}
