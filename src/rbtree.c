/*
Copyright (c) 2019 ko han

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "core.h"

#include <Python.h>

#define RBTree_RED 1
#define RBTree_BLACK 0
#define RBTreeNode_CAST(node) ((CtsRBTreeNode *)(node))
#define RBTreeNode_Color(node) (RBTreeNode_CAST(node)->color)
#define RBTreeNode_SetRed(node) (RBTreeNode_Color(node) = RBTree_RED)
#define RBTreeNode_SetBlack(node) (RBTreeNode_Color(node) = RBTree_BLACK)
#define RBTreeNode_IsRed(node) (RBTreeNode_Color(node) == RBTree_RED)
#define RBTreeNode_IsBlack(node) (!RBTreeNode_IsRed(node))

/* clang-format off */
struct cts_rbtree_node {
  PyObject_HEAD
  PyObject *key;
  PyObject *value;
  struct cts_rbtree_node *left;
  struct cts_rbtree_node *right;
  struct cts_rbtree_node *parent;
  char color;
};
/* clang-format on */

typedef struct cts_rbtree_node CtsRBTreeNode;

/* clang-format off */
typedef struct {
  PyObject_HEAD
  CtsRBTreeNode *root;
  CtsRBTreeNode *sentinel;
  PyObject *cmpfunc;
  Py_ssize_t length;
} CtsRBTree;
/* clang-format on */

static PyTypeObject RBTreeNode_Type;
static PyTypeObject RBTree_Type;
static CtsRBTreeNode RBTree_SentinelNode;
#define RBTree_Sentinel (&RBTree_SentinelNode)

static CtsRBTreeNode *RBTreeNode_New(PyObject *key, PyObject *value) {
  CtsRBTreeNode *node;
  node = PyObject_GC_New(CtsRBTreeNode, &RBTreeNode_Type);
  ReturnIfNULL(node, NULL);
  Py_XINCREF(key);
  Py_XINCREF(value);
  node->key = key;
  node->value = value;
  node->parent = NULL;
  node->right = NULL;
  node->left = NULL;
  RBTreeNode_SetRed(node);
  PyObject_GC_Track(node);
  return node;
}

static int RBTreeNode_tp_traverse(CtsRBTreeNode *self, visitproc visit,
                                  void *arg) {
  Py_VISIT(self->key);
  Py_VISIT(self->value);
  Py_VISIT(self->left);
  Py_VISIT(self->right);
  /* children node doesn't need to visit parent node */
  return 0;
}

static int RBTreeNode_tp_clear(CtsRBTreeNode *self) {
  Py_CLEAR(self->key);
  Py_CLEAR(self->value);
  Py_CLEAR(self->left);
  Py_CLEAR(self->right);
  Py_CLEAR(self->parent);
  return 0;
}

static void RBTreeNode_tp_dealloc(CtsRBTreeNode *self) {
  PyObject_GC_UnTrack(self);
  RBTreeNode_tp_clear(self);
  PyObject_GC_Del(self);
}

static PyObject *RBTreeNode_tp_new(PyTypeObject *Py_UNUSED(type),
                                   PyObject *args, PyObject *kwds) {
  PyObject *key;
  PyObject *value;
  static char *kwlist[] = {"key", "value", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "OO", kwlist, &key, &value)) {
    return NULL;
  }
  return PyObjectCast(RBTreeNode_New(key, value));
}

static PyTypeObject RBTreeNode_Type = {
    /* clang-format off */
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "ctools.SortedMapNode",
    /* clang-format on */
    .tp_basicsize = sizeof(CtsRBTreeNode),
    .tp_dealloc = (destructor)RBTreeNode_tp_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)RBTreeNode_tp_traverse,
    .tp_clear = (inquiry)RBTreeNode_tp_clear,
    .tp_new = (newfunc)RBTreeNode_tp_new,
};

static void RBTreeSentinel_dealloc(PyObject *Py_UNUSED(ignore)) {
  /* This should never get called, but we also don't want to SEGV if
   * we accidentally decref None out of existence.
   */
  Py_FatalError("dealloc SortedMapSentinel");
}

static PyObject *RBTreeSentinel_tp_repr(PyObject *self) {
  return PyUnicode_FromFormat("<SortedMapSentinel at %p>", self);
}

static PyObject *RBTreeSentinel_tp_new(PyObject *Py_UNUSED(ignore),
                                       PyObject *Py_UNUSED(unused)) {
  Py_INCREF(RBTree_Sentinel);
  return PyObjectCast(RBTree_Sentinel);
}

static PyTypeObject RBTreeSentinel_Type = {
    /* clang-format off */
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "ctools.SortedMapSentinel",
    /* clang-format on */
    .tp_basicsize = sizeof(CtsRBTreeNode),
    .tp_dealloc = (destructor)RBTreeSentinel_dealloc,
    .tp_repr = (reprfunc)RBTreeSentinel_tp_repr,
    .tp_str = (reprfunc)RBTreeSentinel_tp_repr,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = (newfunc)RBTreeSentinel_tp_new,
};

/* clang-format off */
static CtsRBTreeNode RBTree_SentinelNode = {
    PyObject_HEAD_INIT(&RBTreeSentinel_Type)
    .color = RBTree_BLACK, /* Sentinel should alway be black */
};
/* clang-format on */

#define RBTree_SetSentinel(v)                                                  \
  do {                                                                         \
    Py_INCREF(RBTree_Sentinel);                                                \
    (v) = RBTree_Sentinel;                                                     \
  } while (0)

#define RBTree_EQ 0
#define RBTree_LT 1
#define RBTree_GT 2

static int rbtree_key_compare(CtsRBTree *tree, PyObject *key1, PyObject *key2) {
  int flag;
  long long cmp;
  PyObject *cmp_o = NULL;

  if (tree->cmpfunc == NULL) {
    flag = PyObject_RichCompareBool(key1, key2, Py_LT);
    if (flag < 0) {
      return -1;
    } else if (flag > 0) {
      return RBTree_LT;
    } else {
      flag = PyObject_RichCompareBool(key1, key2, Py_GT);
      if (flag < 0) {
        return -1;
      } else if (flag > 0) {
        return RBTree_GT;
      } else {
        return RBTree_EQ;
      }
    }
  }

  cmp_o = PyObject_CallFunctionObjArgs(tree->cmpfunc, key1, key2, NULL);
  if (!cmp_o) {
    flag = -1;
    goto finish;
  }
  cmp = PyLong_AsLongLong(cmp_o);
  if (cmp == -1 && PyErr_Occurred()) {
    PyErr_Format(
        PyExc_TypeError,
        "SortedMap cmp function return value expecting a integer but got %S",
        cmp_o);
    flag = -1;
    goto finish;
  }
  if (cmp > 0) {
    flag = RBTree_GT;
  } else if (cmp < 0) {
    flag = RBTree_LT;
  } else {
    flag = RBTree_EQ;
  }

finish:
  Py_XDECREF(cmp_o);
  return flag;
}

static int rbtree_node_compare(CtsRBTree *tree, CtsRBTreeNode *a,
                               CtsRBTreeNode *b) {
  assert(tree->sentinel != a);
  assert(tree->sentinel != b);
  return rbtree_key_compare(tree, a->key, b->key);
}
/*
 *          root            root              root            root
 *          / \             /  \              /  \            /  \
 *         x   d    ->     x    d   ->    x  y   d    ->     y    d
 *        / \             /\\            /\ / \             / \
 *       a   y           a b-y          a  b   c           x   c
 *          / \               \                           /\
 *         b   c               c                         a b
 *
 *  Note: [a, b, c d] means any sub tree.
 */
static void left_rotate(CtsRBTree *tree, CtsRBTreeNode *x) {
  CtsRBTreeNode *y = x->right;
  CtsRBTreeNode *sentinel = tree->sentinel;
  /* First step */
  x->right = y->left;
  if (y->left != sentinel) {
    y->left->parent = x;
  }

  /* Second step */
  y->parent = x->parent;
  if (x->parent == sentinel) {
    tree->root = y;
  } else if (x == x->parent->left) {
    x->parent->left = y;
  } else {
    x->parent->right = y;
  }
  /* Third step */
  y->left = x;
  x->parent = y;
}

static void right_rotate(CtsRBTree *tree, CtsRBTreeNode *x) {
  CtsRBTreeNode *y = x->left;
  CtsRBTreeNode *sentinel = tree->sentinel;

  x->left = y->right;
  if (y->right != sentinel) {
    y->right->parent = x;
  }
  y->parent = x->parent;
  if (x->parent == sentinel) {
    tree->root = y;
  } else if (x == x->parent->right) {
    x->parent->right = y;
  } else {
    x->parent->left = y;
  }
  y->right = x;
  x->parent = y;
}

static int rbtree_insert_fix(CtsRBTree *tree, CtsRBTreeNode *z) {
  CtsRBTreeNode *y;
  CtsRBTreeNode *root = tree->root;
  /* sentinel is always black */
  while (z != root && RBTreeNode_IsRed(z->parent)) {
    if (z->parent == z->parent->parent->left) {
      /* z is grandfather's left branch */
      y = z->parent->parent->right;
      if (RBTreeNode_IsRed(y)) {
        RBTreeNode_SetBlack(z->parent);       /* case 1 */
        RBTreeNode_SetBlack(y);               /* case 1 */
        RBTreeNode_SetRed(z->parent->parent); /* case 1 */
        z = z->parent->parent;
      } else {
        if (z == z->parent->right) {
          z = z->parent;
          left_rotate(tree, z);
        }
        z->parent->color = RBTree_BLACK;
        z->parent->parent->color = RBTree_RED;
        right_rotate(tree, z->parent->parent);
      }

    } else {
      /* z is grandfather's right branch */
      y = z->parent->parent->left;
      if (RBTreeNode_IsRed(y)) {
        RBTreeNode_SetBlack(z->parent);       /* case 1 */
        RBTreeNode_SetBlack(y);               /* case 1 */
        RBTreeNode_SetRed(z->parent->parent); /* case 1 */
        z = z->parent->parent;
      } else {
        if (z == z->parent->left) {
          z = z->parent;
          right_rotate(tree, z);
        }
        RBTreeNode_SetBlack(z->parent);
        RBTreeNode_SetRed(z->parent->parent);
        left_rotate(tree, z->parent->parent);
      }
    }
  }
  RBTreeNode_SetBlack(tree->root);
  return 0;
}

/* steal the reference of z */
static int RBTree_PutNode(CtsRBTree *tree, CtsRBTreeNode *z) {
  CtsRBTreeNode *sentinel = tree->sentinel;
  CtsRBTreeNode *x = tree->root;
  CtsRBTreeNode *y = sentinel;
  int flag;

  while (x != sentinel) {
    y = x;
    flag = rbtree_node_compare(tree, z, x);
    if (flag < 0) {
      goto fail;
    }
    if (flag == RBTree_LT) {
      x = x->left;
    } else if (flag == RBTree_GT) {
      x = x->right;
    } else { /* already has key, replace value */
      Py_INCREF(z->value);
      Py_SETREF(x->value, z->value);
      Py_DECREF(z);
      return 0;
    }
  }
  z->parent = y;
  if (y == sentinel) { /* tree is empty */
    Py_SETREF(tree->root, z);
    goto success;
  }
  flag = rbtree_node_compare(tree, z, y);
  if (flag < 0) {
    goto fail;
  } else if (flag == RBTree_LT) {
    y->left = z;
  } else if (flag == RBTree_GT) {
    y->right = z;
  } else {
    assert(0);
    /* Can't be here. */
    return 0;
  }
success:
  RBTree_SetSentinel(z->left);
  RBTree_SetSentinel(z->right);
  RBTreeNode_SetRed(z);
  tree->length++;
  return rbtree_insert_fix(tree, z);
fail:
  Py_DECREF(z);
  return -1;
}

static int RBTree_Put(CtsRBTree *tree, PyObject *key, PyObject *value) {
  CtsRBTreeNode *node;
  node = RBTreeNode_New(key, value);
  ReturnIfNULL(node, -1);
  return RBTree_PutNode(tree, node);
}

static int RBTree_Get(CtsRBTree *tree, PyObject *key, PyObject **value) {
  CtsRBTreeNode *x = tree->root;
  CtsRBTreeNode *sentinel = tree->sentinel;
  int flag;

  while (x != sentinel) {
    flag = rbtree_key_compare(tree, key, x->key);
    if (flag < 0) {
      return -1;
    }
    if (flag == RBTree_LT) {
      x = x->left;
    } else if (flag == RBTree_GT) {
      x = x->right;
    } else {
      Py_INCREF(x->value);
      *value = x->value;
      return 1;
    }
  }
  return 0;
}

static CtsRBTree *RBTree_New(PyObject *cmp) {
  CtsRBTree *tree;
  if (cmp && !PyCallable_Check(cmp)) {
    PyErr_SetString(PyExc_TypeError, "cmp must be a callable object");
    return NULL;
  }

  tree = PyObject_GC_New(CtsRBTree, &RBTree_Type);
  ReturnIfNULL(tree, NULL);
  Py_XINCREF(cmp);
  RBTree_SetSentinel(tree->root);
  RBTree_SetSentinel(tree->sentinel);
  tree->cmpfunc = cmp;
  tree->length = 0;
  PyObject_GC_Track(tree);
  return tree;
}

static PyObject *RBTree_tp_new(PyTypeObject *Py_UNUSED(type), PyObject *args,
                               PyObject *Py_UNUSED(kwds)) {
  PyObject *cmp = NULL;
  if (!PyArg_ParseTuple(args, "|O", &cmp)) {
    return NULL;
  }
  return PyObjectCast(RBTree_New(cmp));
}

static int RBTree_tp_traverse(CtsRBTree *self, visitproc visit, void *arg) {
  Py_VISIT(self->root);
  Py_VISIT(self->sentinel);
  return 0;
}

static int RBTree_tp_clear(CtsRBTree *self) {
  Py_CLEAR(self->root);
  return 0;
}

static void RBTree_tp_dealloc(CtsRBTree *self) {
  PyObject_GC_UnTrack(self);
  RBTree_tp_clear(self);
  PyObject_GC_Del(self);
}

static Py_ssize_t RBTree_size(CtsRBTree *tree) { return tree->length; }

/* __getitem__ */
static PyObject *RBTree_mp_subscript(CtsRBTree *tree, PyObject *key) {
  PyObject *value;
  int find;
  find = RBTree_Get(tree, key, &value);
  if (find < 0) {
    return NULL;
  } else if (find == 0) {
    return PyErr_Format(PyExc_KeyError, "%S", key);
  } else {
    return value;
  }
}

/* __setitem__, __delitem__ */
static int RBTree_mp_ass_sub(CtsRBTree *tree, PyObject *key, PyObject *value) {
  if (!value) {
    /* TODO */
    PyErr_SetString(PyExc_NotImplementedError, "");
    return -1;
  }
  return RBTree_Put(tree, key, value);
}

static PyMappingMethods RBTree_as_mapping = {
    (lenfunc)RBTree_size,             /*mp_length*/
    (binaryfunc)RBTree_mp_subscript,  /*mp_subscript*/
    (objobjargproc)RBTree_mp_ass_sub, /*mp_ass_subscript*/
};

static int RBTree_Contains(CtsRBTree *tree, PyObject *key) {
  PyObject *value;
  int find;
  find = RBTree_Get(tree, key, &value);
  if (find < 0) {
    return -1;
  } else if (find == 0) {
    return 0;
  } else {
    Py_DECREF(value);
    return 1;
  }
}

static PySequenceMethods RBTree_as_sequence = {
    0,                           /* sq_length */
    0,                           /* sq_concat */
    0,                           /* sq_repeat */
    0,                           /* sq_item */
    0,                           /* sq_slice */
    0,                           /* sq_ass_item */
    0,                           /* sq_ass_slice */
    (objobjproc)RBTree_Contains, /* sq_contains */
    0,                           /* sq_inplace_concat */
    0,                           /* sq_inplace_repeat */
};

static PyTypeObject RBTree_Type = {
    /* clang-format off */
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "ctools.SortedMap",
    /* clang-format on */
    .tp_basicsize = sizeof(CtsRBTree),
    .tp_dealloc = (destructor)RBTree_tp_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)RBTree_tp_traverse,
    .tp_clear = (inquiry)RBTree_tp_clear,
    .tp_new = (newfunc)RBTree_tp_new,
    .tp_as_mapping = &RBTree_as_mapping,
    .tp_as_sequence = &RBTree_as_sequence,
};

EXTERN_C_START
int ctools_init_rbtree(PyObject *module) {
  if (PyType_Ready(&RBTreeNode_Type) < 0) {
    return -1;
  }
  if (PyType_Ready(&RBTreeSentinel_Type)) {
    return -1;
  }
  Py_INCREF(&RBTreeNode_Type);
  if (PyModule_AddObject(module, "SortedMapNode",
                         (PyObject *)&RBTreeNode_Type) < 0) {
    Py_DECREF(&RBTreeNode_Type);
    return -1;
  }
  Py_INCREF(RBTree_Sentinel);
  if (PyModule_AddObject(module, "SortedMapSentinel",
                         PyObjectCast(RBTree_Sentinel))) {
    Py_DECREF(RBTree_Sentinel);
    return -1;
  }

  if (PyType_Ready(&RBTree_Type) < 0) {
    return -1;
  }
  Py_INCREF(&RBTree_Type);
  if (PyModule_AddObject(module, "SortedMap", (PyObject *)&RBTree_Type) < 0) {
    Py_DECREF(&RBTree_Type);
    return -1;
  }
  return 0;
}
EXTERN_C_END
