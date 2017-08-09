#include "list.h"
#include "interrupt.h"

// 初始化双向链表
void list_init(struct list *list) {
    list->head.prev = NULL;
    list->head.next = &list->tail;
    list->tail.prev = &list->head;
    list->tail.next = NULL;
}

// 把链表元素插入到元素before之前
void list_insert_before(struct list_elem *before, struct list_elem *elem) {
    enum intr_status old_status = intr_disable();
    before->prev->next = elem;
    elem->prev = before->prev;
    elem->next = before;
    before->prev = elem;
    intr_set_status(old_status);
}

// 追加元素到链表头部
void list_push(struct list *plist, struct list_elem *elem) {
    list_insert_before(plist->head.next, elem);
}

// void list_iterate(struct list *plist)

// 追加元素到链表尾部
void list_append(struct list *plist, struct list_elem *elem) {
    list_insert_before(&plist->tail, elem);
}

// pelem从链表中移除
void list_remove(struct list_elem *pelem) {
    enum intr_status old_status = intr_disable();
    pelem->prev->next = pelem->next;
    pelem->next->prev = pelem->prev;
    intr_set_status(old_status);
}

// 弹出链表中的第一个元素
struct list_elem *list_pop(struct list *plist) {
    struct list_elem *elem = plist->head.next;
    list_remove(elem);
    return elem;
}

// 判断链表是否为空
bool list_empty(struct list *plist) {
    return (plist->head.next == &plist->tail ? true : false);
}

// 返回链表的长度
uint32_t list_len(struct list *plist) {
    struct list_elem *elem = plist->head.next;
    uint32_t length = 0;
    while (elem != &plist->tail) {
        length++;
        elem = elem->next;
    }
    return length;
}

// 遍历链表，返回符合条件的首个链表元素，如果没有则返回NULL
struct list_elem *list_traversal(struct list *plist, function func, int arg) {
    struct list_elem *elem = plist->head.next;
    if (list_empty(plist)) {
        return NULL;
    }
    while (elem != &plist->tail) {
        if (func(elem, arg)) {
            return elem;
        }
        elem = elem->next;
    }
    return NULL;
}

// 链表中查找元素obj_elem，成功返回true，失败返回false
bool elem_find(struct list *plist, struct list_elem *obj_elem) {
    struct list_elem *elem = plist->head.next;
    while (elem != &plist->tail) {
        if (elem == obj_elem) {
            return true;
        }
        elem = elem->next;
    }
    return false;
}