#include <logdb/logdb_base.h>
#include <logdb/red_black_tree.h>

#include "../../../test/utest.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/*  this file has functions to test a red-black tree of integers */

void free_key(void* a) {
  free((int*)a);
}

void free_value(void *a){
   free((int*)a);
}

int IntComp(const void* a,const void* b) {
  if( *(int*)a > *(int*)b) return(1);
  if( *(int*)a < *(int*)b) return(-1);
  return(0);
}

void IntPrint(const void* a) {
  printf("%i",*(int*)a);
}

void InfoPrint(void* a) {
  UNUSED(a);
}


void test_red_black_tree() {
    char *akey;
    char *avalue;
    char *akey2;
    char *avalue2;
    rb_red_blk_node* newNode;
    rb_red_blk_node* newNode2;
    rb_red_blk_tree* tree;
    size_t size;

    tree=RBTreeCreate(IntComp,free_key,free_value,IntPrint,InfoPrint);


    akey = malloc(10);
    memcpy(akey, (void *)"akey", 4);
    avalue = malloc(10);
    memcpy(avalue, (void *)"avalue", 6);
    RBTreeInsert(tree,akey,avalue);

    akey2 = malloc(10);
    memcpy(akey2, (void *)"bkey", 4);
    avalue2 = malloc(10);
    memcpy(avalue2, (void *)"bvalue", 6);
    RBTreeInsert(tree,akey2,avalue2);

    newNode2 = RBExactQuery(tree,akey2);
    newNode = RBExactQuery(tree,akey);
    newNode = TreeSuccessor(tree,newNode);
    newNode = TreePredecessor(tree,newNode);

    size = rbtree_count(tree);
    while((newNode2 = rbtree_enumerate_next(tree)))
    {
        size--;
    }
    /* test reset */
    while((newNode2 = rbtree_enumerate_next(tree)))
    {
        size++;
    }
    while((newNode2 = rbtree_enumerate_next(tree)))
    {
        size--;
    }

    u_assert_int_eq(size, 0);

    RBDelete(tree, newNode);

    RBTreePrint(tree);
    RBTreeDestroy(tree);
}
