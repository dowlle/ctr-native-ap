#ifndef CTR_NATIVE_NAMESPACE_LIST_H
#define CTR_NATIVE_NAMESPACE_LIST_H

// complete struct
struct Item
{
	struct Item *next;
	struct Item *prev;
};

// complete struct
struct LinkedList
{
	struct Item *first;
	struct Item *last;
	int count;
};

CTR_STATIC_ASSERT(sizeof(struct LinkedList) == 0xC);

#endif
