#!/usr/bin/env python3

import unittest
import cffi
from ptest import load

NODE_FLAGS_LEAF = 1
NODE_FLAGS_INNER_4 = 2
NODE_FLAGS_INNER_16 = 4
NODE_FLAGS_INNER_48 = 8
NODE_FLAGS_INNER_256 = 16

class TestAdaptiveRadixTree(unittest.TestCase):
    def setUp(self):
        self.module, self.ffi = load("art")

    def max_height(self, node, height=1):
        """
        Calculate the maximum height of the trie.
        """
        if node.flags & NODE_FLAGS_LEAF:
            return height

        m = height
        for i in range(0, node.key_len):
            next = self.make_node()
            h = self.max_height(self.ffi.cast("struct radix_tree_node *", node.arr[i]), height + 1)
            if h > m:
                m = h
        return m

    def make_root(self):
        return self.ffi.new("struct radix_tree_node **")

    def make_node(self):
        return self.ffi.new("struct radix_tree_node *")

    def make_stream(self, key):
        s = self.ffi.new("struct stream *")
        k = self.ffi.new(f"char[]", str.encode(key))
        s[0].data = k
        s[0].end = k + len(key) # Point 1-byte past the end
        return s
    
    def make_nodes(self, key):
        return self.make_root(), self.make_stream(key), self.make_node()

    def _test_one_insert(self):
        key = "ABC"
        root, stream, leaf = self.make_nodes(key)
        self.module.insert(root, stream, leaf, 0)

        node = root[0]
        self.assertEqual(node.flags, NODE_FLAGS_LEAF)
        self.assertEqual(node.key_len, len(key))
    
    def _test_two_inserts(self):
        key1 = "ABC"
        key2 = "ABCDE"
        r, s, l = self.make_nodes(key1)
        self.module.insert(r, s, l, 0)

        s2, l2 = self.make_stream(key2), self.make_node()
        self.module.insert(r, s2, l2, 0)

        root = r[0]
        self.assertEqual(root.key_len, 1)
        self.assertEqual(self.max_height(root), 2)

    def test_many_inserts(self):
        keys = ["ABCDE", "ACDE", "ADEFGHIJ"]
        root = self.make_root()

        s = self.make_stream("ABCDE")
        l = self.make_node()
        self.module.insert(root, s, l, 0)
        self.assertEqual(root[0][0].flags, NODE_FLAGS_LEAF)
        print(root[0].key_len)

        s2 = self.make_stream("ACDE")
        print(s, s2)
        s3 = self.ffi.new("struct stream *")
        s4 = self.ffi.new("struct stream *")
        k1 = self.ffi.new("char []",str.encode("foo"))
        k2 = self.ffi.new("char []",str.encode("foobar"))
        print(s3[0], s4[0])
        print(len(k1), len(k2))
        print(self.make_stream("foo")[0].data, self.make_stream("bar")[0].data)
        l = self.make_node()
        self.module.insert(root, s2, l, 0)
        print(root[0].key_len)
        self.assertEqual(root[0][0].flags, NODE_FLAGS_INNER_4)

        self.assertEqual(self.max_height(root[0]), 2)

        r = root[0]
        self.assertEqual(r.flags, NODE_FLAGS_INNER_4)
        self.assertEqual(r.key[2], "A")
        self.assertEqual(r.key_len, 1)

if __name__ == '__main__':
    unittest.main()
