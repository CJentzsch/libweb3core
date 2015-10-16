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
/** @file rlp.cpp
 * @author Christoph Jentzsch <cj@ethdev.com>
 * @date 2015
 * trie common tests.
 */

#include <test/test.h>
#include <libdevcore/Common.h>
#include <libdevcore/TrieCommon.h>

using namespace std;
using namespace dev;

BOOST_AUTO_TEST_SUITE(triecommon)

BOOST_AUTO_TEST_CASE(testHexPrefixEncode)
{
	BOOST_CHECK_EQUAL(toHex(asBytes(hexPrefixEncode(bytes{ 0,0,1,2,3,4,5 }))) , "10012345" );
	BOOST_CHECK_EQUAL(toHex(asBytes(hexPrefixEncode(bytes{ 0,1,2,3,4,5 }))) , "00012345" );
	BOOST_CHECK_EQUAL(toHex(asBytes(hexPrefixEncode(bytes{ 1,2,3,4,5 }))) , "112345" );
	BOOST_CHECK_EQUAL(toHex(asBytes(hexPrefixEncode(bytes{ 0,0,1,2,3,4 }))) , "00001234" );
	BOOST_CHECK_EQUAL(toHex(asBytes(hexPrefixEncode(bytes{ 0,1,2,3,4 }))) , "101234" );
	BOOST_CHECK_EQUAL(toHex(asBytes(hexPrefixEncode(bytes{ 1,2,3,4 }))) , "001234" );
	BOOST_CHECK_EQUAL(toHex(asBytes(hexPrefixEncode(bytes{ 0,0,1,2,3,4,5 }, true))) , "30012345" );
	BOOST_CHECK_EQUAL(toHex(asBytes(hexPrefixEncode(bytes{ 0,0,1,2,3,4 }, true))) , "20001234" );
	BOOST_CHECK_EQUAL(toHex(asBytes(hexPrefixEncode(bytes{ 0,1,2,3,4,5 }, true))) , "20012345" );
	BOOST_CHECK_EQUAL(toHex(asBytes(hexPrefixEncode(bytes{ 1,2,3,4,5 }, true))) , "312345" );
	BOOST_CHECK_EQUAL(toHex(asBytes(hexPrefixEncode(bytes{ 1,2,3,4 }, true))) , "201234" );
	BOOST_CHECK_EQUAL(toHex(asBytes(hexPrefixEncode(bytes{ 15,1,12,11,8 }, true))) , "3f1cb8" );
}

BOOST_AUTO_TEST_SUITE_END()
