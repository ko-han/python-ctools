import unittest
import uuid
import sys

import ctools


class TestCacheEntry(unittest.TestCase):

    def assertRefEqual(self, a, b):
        self.assertEqual(sys.getrefcount(a), sys.getrefcount(b))

    def test_ref(self):
        class A:
            def __init__(self, a):
                self.a = a

            def get_value(self):
                return self.a

        entry = ctools.CacheEntry(uuid.uuid1())
        a = A(uuid.uuid1())
        self.assertRefEqual(entry, a)

        v1 = entry.get_value()
        v2 = entry.get_value()
        self.assertRefEqual(v1, v2)

        del entry
        del a
        self.assertRefEqual(v1, v2)


def set_random(mp):
    key = str(uuid.uuid1())
    val = str(uuid.uuid1())
    mp[key] = val
    return key


class TestCacheMapRefCount(unittest.TestCase):

    def assertRefEqual(self, v1, v2, msg=None):
        self.assertEqual(sys.getrefcount(v1), sys.getrefcount(v2), msg)

    def assert_ref_equal(self, cache, lkey, d, dkey):
        lval1 = cache[lkey]
        dval1 = d[dkey]
        lval = cache[lkey]
        dval = d[dkey]
        self.assertEqual(sys.getrefcount(lval), sys.getrefcount(dval))

        del cache[lkey]
        del d[dkey]
        self.assertEqual(sys.getrefcount(lval), sys.getrefcount(dval))

        del d
        del cache
        self.assertEqual(sys.getrefcount(lval), sys.getrefcount(dval))

    def test_one_ref_eq(self):
        cache = ctools.CacheMap(10)
        mp = {}

        lkey = set_random(cache)
        dkey = set_random(mp)

        self.assert_ref_equal(cache, lkey, mp, dkey)

        lkey = set_random(cache)
        dkey = set_random(mp)

        lval = cache[lkey]
        dval = mp[dkey]

        del cache
        del mp
        self.assertRefEqual(lval, dval)

    def test_replace_ref_eq(self):
        cache = ctools.CacheMap(10)
        mp = {}

        lkey = set_random(cache)
        dkey = set_random(mp)

        lval = cache[lkey]
        dval = mp[dkey]
        lval = cache[lkey]
        dval = mp[dkey]
        self.assertRefEqual(lval, dval)

        cache[lkey] = str(uuid.uuid1())
        mp[dkey] = str(uuid.uuid1())

        self.assertRefEqual(lval, dval)
        self.assertRefEqual(cache[lkey], mp[dkey])
        self.assertRefEqual(lkey, dkey)

    def test_many_ref_eq(self):

        lfu_cache = ctools.CacheMap(257)
        d = {}
        for i in range(1024):
            set_random(lfu_cache)
            set_random(d)

        while True:
            try:
                lkey = list(lfu_cache.keys())[0]
            except IndexError:
                break
            dkey = list(d.keys())[0]
            self.assert_ref_equal(lfu_cache, lkey, d, dkey)

    def test_raw_ref_eq(self):
        cache = ctools.CacheMap(10)
        store = cache._storage()
        d = {}

        lkey = set_random(cache)
        dkey = set_random(d)

        lval = cache[lkey]
        lval = store[lkey]
        lval = cache[lkey]
        dval = d[dkey]

        self.assertRefEqual(lval, dval)

        del cache[lkey]
        del d[dkey]
        self.assertRefEqual(lval, dval)

        del cache
        del d
        self.assertRefEqual(lval, dval)


class TestCacheMap(unittest.TestCase):

    def test_get_set(self):
        d = ctools.CacheMap(2)
        d['a'] = 1
        d['c'] = 2
        d['e'] = 3
        self.assertEqual(len(d), 2)

        for i in range(10):
            self.assertEqual(d['c'], 2)

    def test_len(self):
        for m in range(254, 1024):
            d = ctools.CacheMap(m)
            for i in range(m + 1):
                d[str(i)] = str(i)
                if i < m and len(d) != i + 1:
                    self.assertEqual(len(d), i)

            self.assertEqual(len(d), m)

    def test_keys(self):
        cache = ctools.CacheMap(257)
        keys = []
        for m in range(1024):
            keys.append(set_random(cache))

        for k in cache.keys():
            self.assertIn(k, keys)

    def test_values(self):
        cache = ctools.CacheMap(257)
        values = []
        for m in range(1024):
            values.append(cache[set_random(cache)])

        for v in cache.values():
            self.assertIn(v, values)

    def test_items(self):
        cache = ctools.CacheMap(257)
        values = []
        keys = []
        for m in range(1024):
            k = set_random(cache)
            keys.append(k)
            values.append(cache[k])

        for k, v in cache.items():
            self.assertIn(k, keys)
            self.assertIn(v, values)

    def test_contains(self):
        cache = ctools.CacheMap(2)
        keys = []
        for m in range(2):
            keys.append(set_random(cache))

        for k in keys:
            self.assertIn(k, cache)

    def test_setdefault(self):
        cache = ctools.CacheMap(2)
        for m in range(2):
            set_random(cache)
        k = str(uuid.uuid1())
        v = str(uuid.uuid1())
        val = cache.setdefault(k, v)
        self.assertEqual(v, val)
        self.assertIn(k, cache)
        self.assertEqual(cache[k], v)

    def test_get(self):
        cache = ctools.CacheMap(2)
        for m in range(2):
            set_random(cache)
        k = str(uuid.uuid1())
        v = str(uuid.uuid1())
        self.assertEqual(v, cache.get(k, v))
        self.assertNotIn(k, cache)
        cache[k] = v
        self.assertEqual(v, cache.get(k, 1))

    def test_update(self):
        d = {'1': '1'}
        cache = ctools.CacheMap(len(d))
        cache.update(d)
        for k in d:
            self.assertIn(k, cache)
        self.assertEqual(len(cache), len(d))

        d = {'abc': '1'}
        cache = ctools.CacheMap(len(d))
        cache.update(**d)
        for k in d:
            self.assertIn(k, cache)
        self.assertEqual(len(cache), len(d))

    def test_setnx(self):
        cache = ctools.CacheMap(10)
        key = str(uuid.uuid1())
        val = str(uuid.uuid1())

        with self.assertRaises(TypeError):
            cache.setnx(1, 1)

        v = cache.setnx(key, lambda: val)
        self.assertEqual(val, v)

        v = cache.setnx(key, lambda: 1)
        self.assertEqual(v, val)

    def test_iter(self):
        cache = ctools.CacheMap(257)
        keys = []
        for m in range(1024):
            k = set_random(cache)
            keys.append(k)

        for i, k in enumerate(cache):
            self.assertIn(k, keys)


if __name__ == '__main__':
    unittest.main()